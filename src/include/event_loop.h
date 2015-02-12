#ifndef FILEZILLA_ENGINE_EVENT_LOOP_HEADER
#define FILEZILLA_ENGINE_EVENT_LOOP_HEADER

#include "apply.h"
#include "event.h"
#include "mutex.h"
#include "timeex.h"

class CEventHandler;
struct timer_data final
{
	CEventHandler* handler_{};
	timer_id id_{};
	CDateTime deadline_;
	int ms_interval_{};
	bool one_shot_{true};
};

class CEventLoop final : private wxThread
{
public:
	CEventLoop();
	virtual ~CEventLoop();

	CEventLoop(CEventLoop const&) = delete;
	CEventLoop& operator=(CEventLoop const&) = delete;

	void RemoveHandler(CEventHandler* handler);

	timer_id AddTimer(CEventHandler* handler, int ms_interval, bool one_shot);
	void StopTimer(timer_id id);

protected:
	friend class CEventHandler;
	void SendEvent(CEventHandler* handler, CEventBase* evt);

	bool ProcessTimers(scoped_lock & l);
	int GetNextWaitInterval();

	virtual wxThread::ExitCode Entry();

	typedef std::deque<std::pair<CEventHandler*, CEventBase*>> Events;
	typedef std::vector<timer_data> Timers;

	Events pending_events_;
	Timers timers_;

	mutex sync_;
	condition cond_;

	bool signalled_{};
	bool quit_{};

	CEventHandler * active_handler_{};

	bool ProcessEvent(scoped_lock & l);
};

template<typename T, typename H, typename F>
bool Dispatch(CEventBase const& ev, F&& f)
{
	bool const same = same_type<T>(ev);
	if (same) {
		T const* e = reinterpret_cast<T const*>(&ev);
		apply(std::forward<F>(f), e->v_);
	}
	return same;
}

template<typename T, typename H, typename F>
bool Dispatch(CEventBase const& ev, H* h, F&& f)
{
	bool const same = same_type<T>(ev);
	if (same) {
		T const* e = reinterpret_cast<T const*>(&ev);
		apply(h, std::forward<F>(f), e->v_);
	}
	return same;
}

#endif
