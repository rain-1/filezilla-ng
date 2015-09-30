#ifndef LIBFILEZILLA_EVENT_HANDLER
#define LIBFILEZILLA_EVENT_HANDLER

#include "fz_event_loop.hpp"

/*
Simple handler for asynchronous event processing.

Usage example:
	struct foo_event_type{}; // Any uniquely named type that's not implicitly convertible
	typedef fz::simple_event<foo_event_type, int, std::string> foo_event;

	struct bar_event_type{};
	typedef fz::simple_event<bar_event_type> bar_event;

	class my_handler final : public fz::event_handler
	{
	public:
		my_handler(fz::event_loop& loop)
			: fz::event_handler(loop)
		{}

		virtual ~my_handler()
		{
			remove_handler();
		}

		void foo(int v, std::string const& s) {
			std::cout << "foo called with:" << s << v;
		}

		void bar() {
			std::cout << "bar called";
		}

		virtual void operator()(event_base const& ev) {
			// Tip: Put in order of decreasing frequency
			fz::dispatch<foo_event, bar_event>(ev, this, &my_handler::foo, &my_handler::bar);
		}
	};

	fz::event_loop loop;
	my_handler h(loop);
	h.SendEvent<foo_event>(42, "Don't Panic");
*/

namespace fz {
class event_handler
{
public:
	event_handler() = delete;

	explicit event_handler(event_loop& loop);
	virtual ~event_handler();

	event_handler(event_handler const&) = delete;
	event_handler& operator=(event_handler const&) = delete;

	// Deactivates handler, removes all pending events and stops all timers for this handler.
	// When function returns, handler is not in its callback anymore.
	//
	// You _MUST_ call remove_handler no later than inside the destructor of the most derived class.
	//
	// This may deadlock if a handler removes itself inside its own callback
	void remove_handler();

	// Called by the event loop in the worker thread with the event to process
	//
	// Override in your derived class.
	// Consider using the dispatch function inside.
	virtual void operator()(event_base const&) = 0;

	// Sends the passed event asynchronously to the handler.
	// Can be called from any thread.
	// All events are processed in-order.
	//
	// See also operator()(event_base const&)
	template<typename T, typename... Args>
	void send_event(Args&&... args) {
		event_loop_.send_event(this, new T(std::forward<Args>(args)...));
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
	timer_id add_timer(fz::duration const& interval, bool one_shot);

	// Stops the given timer.
	// One-shot timers that have fired do not need to be stopped.
	void stop_timer(timer_id id);

	event_loop & event_loop_;
private:
	friend class event_loop;
	bool removing_{};
};
}

#endif