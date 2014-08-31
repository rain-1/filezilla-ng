#ifndef FILEZILLA_ENGINE_EVENT_LOOP_HEADER
#define FILEZILLA_ENGINE_EVENT_LOOP_HEADER

#include "apply.h"
#include "event.h"
#include "event_handler.h"

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
