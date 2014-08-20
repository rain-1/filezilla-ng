#ifndef FILEZILLA_ENGINE_EVENT_LOOP_HEADER
#define FILEZILLA_ENGINE_EVENT_LOOP_HEADER

#include <functional>

#include "apply.h"

using namespace std::placeholders;

class CEventBase
{
public:
	CEventBase() {}
	virtual ~CEventBase() {}

	CEventBase(CEventBase const&) = delete;
	CEventBase& operator=(CEventBase const&) = delete;

	virtual CEventBase* clone() const = 0;
};

template<typename Derived>
class CClonableEvent : public CEventBase
{
public:
	virtual CEventBase* clone() const
	{
		return new Derived(static_cast<Derived const&>(*this));
	}
};

template<typename UniqueType, typename...Values>
class CEvent final : public CClonableEvent<CEvent<UniqueType, Values...>>
{
public:
	typedef UniqueType unique_type;
	typedef std::tuple<Values...> tuple_type;

	CEvent()
	{
	}

	template<typename Value, typename...Values>
	explicit CEvent(Value&& value, Values&& ...values)
		: v_(std::forward<Value>(value), std::forward<Values>(values)...)
	{
	}

	CEvent(CEvent const& op)
		: v_(op.v_)
	{
	}

	CEvent& operator=(CEvent const& op) {
		if (this != &op) {
			v_ = op.v_;
		}
		return *this;
	}

	std::tuple<Values...> v_;
};

struct timer_event_type{};
typedef CEvent<timer_event_type, int> CTimerEvent;

class CEventLoop;

class CEventHandler
{
public:
	CEventHandler(CEventLoop& loop);
	virtual ~CEventHandler();

	void RemoveHandler();

	virtual void operator()(CEventBase const&) = 0;

	void SendEvent(CEventBase const& evt);

	int AddTimer(int ms_interval, bool one_shot);
	void StopTimer(int timer_id);

	CEventLoop & event_loop_;
};

class CEventLoop : private wxEvtHandler
{
public:
	CEventLoop();
	virtual ~CEventLoop();

	CEventLoop(CEventLoop const&) = delete;
	CEventLoop& operator=(CEventLoop const&) = delete;

	void SendEvent(CEventHandler* handler, CEventBase const& evt);

	void RemoveHandler(CEventHandler* handler);

	int AddTimer(CEventHandler* handler, int ms_interval, bool one_shot);
	void StopTimer(CEventHandler* handler, int timer_id);

protected:
	typedef std::list<std::pair<CEventHandler*, CEventBase*>> Events;
	typedef std::multimap<CEventHandler*, wxTimer*> Timers;

	Events pending_events_;
	Timers active_timers_;

	wxCriticalSection sync_;

	virtual bool ProcessEvent(wxEvent& event);
};

template<typename T, typename H, typename F>
bool Dispatch(CEventBase const& ev, F&& f)
{
	T const* e = dynamic_cast<T const*>(&ev);
	if (e) {
		apply(std::forward<F>(f), e->v_);
	}
	return e != 0;
}

template<typename T, typename H, typename F>
bool Dispatch(CEventBase const& ev, H* h, F&& f)
{
	T const* e = dynamic_cast<T const*>(&ev);
	if (e) {
		apply(h, std::forward<F>(f), e->v_);
	}
	return e != 0;
}

#endif
