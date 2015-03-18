#include <filezilla.h>

#include "event_loop.h"

#include <algorithm>

CEventLoop::CEventLoop()
	: wxThread(wxTHREAD_JOINABLE)
	, sync_(false)
{
	Create();
	Run();
}

CEventLoop::~CEventLoop()
{
	{
		scoped_lock lock(sync_);
		quit_ = true;
		cond_.signal(lock);
	}

	Wait(wxTHREAD_WAIT_BLOCK);

	scoped_lock lock(sync_);
	for (auto & v : pending_events_) {
		delete v.second;
	}
}

void CEventLoop::SendEvent(CEventHandler* handler, CEventBase* evt)
{
	{
		scoped_lock lock(sync_);
		if (!handler->removing_) {
			if (pending_events_.empty()) {
				cond_.signal(lock);
			}
			pending_events_.emplace_back(handler, evt);			
		}
		else {
			delete evt;
		}
	}
}

void CEventLoop::RemoveHandler(CEventHandler* handler)
{
	scoped_lock l(sync_);

	handler->removing_ = true;

	pending_events_.erase(
		std::remove_if(pending_events_.begin(), pending_events_.end(),
			[&](Events::value_type const& v) {
				if (v.first == handler) {
					delete v.second;
				}
				return v.first == handler;
			}
		),
		pending_events_.end()
	);

	timers_.erase(
		std::remove_if(timers_.begin(), timers_.end(),
			[&](timer_data const& v) {
				return v.handler_ == handler;
			}
		),
		timers_.end()
	);
	if (timers_.empty()) {
		deadline_ = wxDateTime();
	}

	while (active_handler_ == handler) {
		l.unlock();
		wxMilliSleep(1);
		l.lock();
	}
}

void CEventLoop::FilterEvents(std::function<bool(Events::value_type &)> filter)
{
	scoped_lock l(sync_);

	pending_events_.erase(
		std::remove_if(pending_events_.begin(), pending_events_.end(),
			[&](Events::value_type & v) {
				bool const remove = filter(v);
				if (remove) {
					delete v.second;
				}
				return remove;
			}
		),
		pending_events_.end()
	);
}

timer_id CEventLoop::AddTimer(CEventHandler* handler, int ms_interval, bool one_shot)
{
	timer_data d;
	d.handler_ = handler;
	d.ms_interval_ = ms_interval;
	d.one_shot_ = one_shot;
	d.deadline_ = wxDateTime::UNow() + wxTimeSpan::Milliseconds(ms_interval);


	scoped_lock lock(sync_);
	static timer_id id{};
	if (!handler->removing_) {
		d.id_ = ++id; // 64bit, can this really ever overflow?

		timers_.emplace_back(d);
		if (!deadline_.IsValid() || d.deadline_ < deadline_) {
			// Our new time is the next timer to trigger
			deadline_ = d.deadline_;
			cond_.signal(lock);
		}
	}
	return d.id_;
}

void CEventLoop::StopTimer(timer_id id)
{
	if (id) {
		scoped_lock lock(sync_);
		for (auto it = timers_.begin(); it != timers_.end(); ++it) {
			if (it->id_ == id) {
				timers_.erase(it);
				if (timers_.empty()) {
					deadline_ = wxDateTime();
				}
				break;
			}
		}
	}
}

bool CEventLoop::ProcessEvent(scoped_lock & l)
{
	Events::value_type ev{};

	if (pending_events_.empty()) {
		return false;
	}
	ev = pending_events_.front();
	pending_events_.pop_front();

	if (ev.first && !ev.first->removing_) {
		active_handler_ = ev.first;
		l.unlock();
		if (ev.second) {
			(*ev.first)(*ev.second);
		}
		delete ev.second;
		l.lock();
		active_handler_ = 0;
	}
	else {
		delete ev.second;
	}

	return true;
}

wxThread::ExitCode CEventLoop::Entry()
{
	scoped_lock l(sync_);
	while (!quit_) {
		wxDateTime const now(wxDateTime::UNow());
		if (ProcessTimers(l, now)) {
			continue;
		}
		if (ProcessEvent(l)) {
			continue;
		}

		// Nothing to do, now we wait
		if (deadline_.IsValid()) {
			int wait = static_cast<int>((deadline_ - now).GetMilliseconds().GetValue());
			cond_.wait(l, wait);
		}
		else {
			cond_.wait(l);
		}
	}

	return 0;
}

bool CEventLoop::ProcessTimers(scoped_lock & l, wxDateTime const& now)
{
	if (!deadline_.IsValid() || now < deadline_) {
		// There's no deadline or deadline has not yet expired
		return false;
	}

	if (deadline_.IsValid() && (now - deadline_).GetMilliseconds() <= -60000) {
		// Did the system time change? Update timers
		deadline_ = wxDateTime();
		for (auto & timer : timers_) {
			timer.deadline_ = now + wxTimeSpan::Milliseconds(timer.ms_interval_);
			if (!deadline_.IsValid() || timer.deadline_ < deadline_) {
				deadline_ = timer.deadline_;
			}
		}
		return false;
	}

	// Update deadline_, stop at first expired timer
	deadline_ = wxDateTime();
	auto it = timers_.begin();
	for (; it != timers_.end(); ++it) {
		if (!deadline_.IsValid() || it->deadline_ < deadline_) {
			if (it->deadline_ <= now) {
				break;
			}
			deadline_ = it->deadline_;
		}
	}

	if (it != timers_.end()) {
		// 'it' is now expired
		// deadline_ has been updated with prior timers
		// go through remaining elements to update deadline_
		for (auto it2 = std::next(it); it2 != timers_.end(); ++it2) {
			if (!deadline_.IsValid() || it2->deadline_ < deadline_) {
				deadline_ = it2->deadline_;
			}
		}

		CEventHandler *const handler = it->handler_;
		auto const id = it->id_;
			
		// Update the expired timer
		if (it->one_shot_) {
			timers_.erase(it);
		}
		else {
			it->deadline_ = now + wxTimeSpan::Milliseconds(it->ms_interval_);
			if (!deadline_.IsValid() || it->deadline_ < deadline_) {
				deadline_ = it->deadline_;
			}
		}

		// Call event handler
		if (!handler->removing_) {
			active_handler_ = handler;
			l.unlock();
			(*handler)(CTimerEvent(id));
			l.lock();
			active_handler_ = 0;
		}

		return true;
	}

	return false;
}
