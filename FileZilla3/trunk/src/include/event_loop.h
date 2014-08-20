#ifndef FILEZILLA_ENGINE_EVENT_LOOP_HEADER
#define FILEZILLA_ENGINE_EVENT_LOOP_HEADER

#include <functional>

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
class CEvent : public CEventBase
{
public:
	virtual CEventBase* clone() const
	{
		return new Derived(static_cast<Derived const&>(*this));
	}
};

template<typename Value = int>
class CSimpleEvent : public CEvent<CSimpleEvent<Value>>
{
public:
	typedef Value value_type;

	CSimpleEvent(value_type v = value_type())
		: v_(v)
	{}

	CSimpleEvent(CSimpleEvent const& e)
		: v_(e.v_)
	{}

	CSimpleEvent& operator=(CSimpleEvent const& e)
	{
		v_ = e.v_;
	}

	value_type const& value() const { return v_; }

protected:
	value_type v_;
};

// Strongly typed typedefs would be very handy.
class CTimerEvent : public CEvent<CTimerEvent>
{
public:
	typedef int value_type;

	CTimerEvent(value_type v = value_type())
		: v_(v)
	{}

	CTimerEvent(CTimerEvent const& e)
		: v_(e.v_)
	{}

	CTimerEvent& operator=(CTimerEvent const& e)
	{
		v_ = e.v_;
	}

	value_type const& value() const { return v_; }

protected:
	value_type v_;
};

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

template<typename T>
bool Dispatch(CEventBase const& ev, std::function<void(T const&)> const& f)
{
	T const* e = dynamic_cast<T const*>(&ev);
	if (e) {
		f(*e);
	}
	return e != 0;
}

template<typename T, typename H>
bool Dispatch(CEventBase const& ev, H* h, void(H::* f)(T const&))
{
	T const* e = dynamic_cast<T const*>(&ev);
	if (e) {
		(h->*f)(*e);
	}
	return e != 0;
}

template<typename T>
bool DispatchSimple(CEventBase const& ev, std::function<void(T const&)> const& f)
{
	return Dispatch<CSimpleEvent<T>>(ev, [&](CSimpleEvent<T> const& ev){f(ev.value()); });
}

template<typename T, typename H>
bool DispatchSimple(CEventBase const& ev, H* h, void(H::* f)(T const&))
{
	return Dispatch<CSimpleEvent<T>>(ev, [&](CSimpleEvent<T> const& ev) {(h->*f)(ev.value());});
}
#endif
