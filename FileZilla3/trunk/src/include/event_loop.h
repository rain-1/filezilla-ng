#ifndef LIBFILEZILLA_EVENT_LOOP_HEADER
#define LIBFILEZILLA_EVENT_LOOP_HEADER

#include "fz_apply.hpp"
#include "event.h"
#include "fz_mutex.hpp"
#include "fz_time.hpp"
#include "fz_thread.hpp"

#include <deque>
#include <functional>
#include <vector>

namespace fz {

class CEventHandler;
struct timer_data final
{
	CEventHandler* handler_{};
	timer_id id_{};
	monotonic_clock deadline_;
	duration interval_{};
};

// Timers have precedence over queued events. Too many or too frequent timers can starve processing queued events.
// If the deadline of multiple timers have expired, they get processed in an unspecified order
class CEventLoop final : private thread
{
public:
	typedef std::deque<std::pair<CEventHandler*, CEventBase*>> Events;

	// Spawns a thread
	CEventLoop();
	virtual ~CEventLoop();

	CEventLoop(CEventLoop const&) = delete;
	CEventLoop& operator=(CEventLoop const&) = delete;

	// Puts all queued events through the filter function.
	// The filter function can freely change the passed events.
	// If the filter function returns true, the corresponding event
	// gets removed.
	void FilterEvents(std::function<bool (Events::value_type&)> const& filter);

protected:
	friend class CEventHandler;

	void RemoveHandler(CEventHandler* handler);

	timer_id AddTimer(CEventHandler* handler, duration const& interval, bool one_shot);
	void StopTimer(timer_id id);

	void SendEvent(CEventHandler* handler, CEventBase* evt);

	// Process timers. Returns true if a timer has been triggered
	bool ProcessTimers(scoped_lock & l, monotonic_clock const& now);

	virtual void entry();

	typedef std::vector<timer_data> Timers;

	Events pending_events_;
	Timers timers_;

	mutex sync_;
	condition cond_;

	bool quit_{};

	CEventHandler * active_handler_{};

	// Process the next (if any) event. Returns true if an event has been processed
	bool ProcessEvent(scoped_lock & l);

	monotonic_clock deadline_;

	timer_id next_timer_id_{};
};


// Dispatch for CEvent<> based events. See event_handler.h for usage example
template<typename T, typename H, typename F>
bool dispatch(CEventBase const& ev, F&& f)
{
	bool const same = same_type<T>(ev);
	if (same) {
		T const* e = static_cast<T const*>(&ev);
		apply(std::forward<F>(f), e->v_);
	}
	return same;
}


template<typename T, typename H, typename F>
bool dispatch(CEventBase const& ev, H* h, F&& f)
{
	bool const same = same_type<T>(ev);
	if (same) {
		T const* e = static_cast<T const*>(&ev);
		apply(h, std::forward<F>(f), e->v_);
	}
	return same;
}

template<typename T, typename ... Ts, typename H, typename F, typename ... Fs>
bool dispatch(CEventBase const& ev, H* h, F&& f, Fs&& ... fs)
{
	if (dispatch<T>(ev, h, std::forward<F>(f))) {
		return true;
	}

	return dispatch<Ts...>(ev, h, std::forward<Fs>(fs)...);
}

}
#endif
