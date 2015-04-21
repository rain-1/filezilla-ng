#ifndef FILEZILLA_ENGINE_EVENT_HANDLER
#define FILEZILLA_ENGINE_EVENT_HANDLER

class CEventBase;
class CEventLoop;

#include "event_loop.h"

/*
Simple handler for asynchronous event processing.

Usage example:
	struct foo_event_type{}; // Any uniquely named type that's not implicitly convertible
	typedef CEvent<foo_event_type, int, std::string> CFooEvent;

	struct bar_event_type{};
	typedef CEvent<bar_event_type> CBarEvent;

	class MyHandler final : public CEventHandler
	{
	public:
		MyHandler(CEventLoop& loop)
			: CEventHandler(loop)
		{}

		virtual ~MyHandler()
		{
			RemoveHandler();
		}

		void foo(int v, std::string const& s) {
			std::cout << "foo called with:" << s << v;
		}

		void bar() {
			std::cout << "bar called";
		}

		virtual void operator()(CEventBase const& ev) {
			// Tip: Put in order of decreasing frequency
			Dispatch<CFooEvent, CBarEvent>(ev, this, &MyHandler::foo, &MyHandler::bar);
		}
	};

	CEventLoop loop;
	MyHandler h(loop);
	h.SendEvent<CFooEvent>(42, "Don't Panic");
*/

class CEventHandler
{
public:
	CEventHandler() = delete;

	explicit CEventHandler(CEventLoop& loop);
	virtual ~CEventHandler();

	CEventHandler(CEventHandler const&) = delete;
	CEventHandler& operator=(CEventHandler const&) = delete;

	// Deactivates handler, removes all pending events and stops all timers for this handler.
	// When function returns, handler is not in its callback anymore.
	//
	// You _MUST_ call RemoveHandler no later than inside the destructor of the most derived class.
	//
	// This may deadlock if a handler removes itself inside its own callback
	void RemoveHandler();

	// Called by the event loop in the worker thread with the event to process
	//
	// Override in your derived class.
	// Consider using the Dispatch function inside.
	virtual void operator()(CEventBase const&) = 0;

	// Sends the passed event asynchronously to the handler.
	// Can be called from any thread.
	// All events are processed in-order.
	//
	// See also operator()(CEventBase const&)
	template<typename T, typename... Args>
	void SendEvent(Args&&... args) {
		event_loop_.SendEvent(this, new T(std::forward<Args>(args)...));
	};

	// Adds a timer, returns the timer id.
	// One-shot timers are deleted automatically
	//
	// Once the interval expires, you get a timer event from the event loop.
	// If multiple intervals expire before the timer fires, e.g. under heavy load,
	// only one event is sent.
	// The next timer event is scheduled ms_intervals into the future at the time right before
	// the callback is called.
	//
	// If multiple timers have expired, the order in which the callback is executed is unspecified,
	// there is no fairness guarantee.
	// Timers take precedence over queued events.
	// High-frequency timers doing heavy processing can starve other timers and queued events.
	timer_id AddTimer(duration const& interval, bool one_shot);

	// Stops the given timer.
	// One-shot timers that have fired do not need to be stopped.
	void StopTimer(timer_id id);

	CEventLoop & event_loop_;
private:
	friend class CEventLoop;
	bool removing_{};
};

#endif