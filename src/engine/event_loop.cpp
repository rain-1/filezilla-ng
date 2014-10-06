#include <filezilla.h>

#include "event_loop.h"

#include <algorithm>

CEventLoop::CEventLoop()
	: wxThread(wxTHREAD_JOINABLE)
	, cond_(sync_)
{
	Create();
	Run();
}

CEventLoop::~CEventLoop()
{
	{
		wxMutexLocker lock(sync_);
		quit_ = true;
		signalled_ = true;
		cond_.Signal();
	}

	Wait(wxTHREAD_WAIT_BLOCK);

	wxMutexLocker lock(sync_);
	for (auto & v : pending_events_) {
		delete v.second;
	}
}

void CEventLoop::SendEvent(CEventHandler* handler, CEventBase const& evt)
{
	{
		wxMutexLocker lock(sync_);
		pending_events_.emplace_back(handler, evt.clone());
		signalled_ = true;
		cond_.Signal();
	}
}

void CEventLoop::RemoveHandler(CEventHandler* handler)
{
	wxMutexLocker lock(sync_);

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
}

timer_id CEventLoop::AddTimer(CEventHandler* handler, int ms_interval, bool one_shot)
{
	timer_data d;
	d.handler_ = handler;
	d.ms_interval_ = ms_interval;
	d.one_shot_ = one_shot;
	d.deadline_ = CDateTime::Now() + wxTimeSpan::Milliseconds(ms_interval);


	wxMutexLocker lock(sync_);
	static timer_id id{};
	d.id_ = ++id; // 64bit, can this really ever overflow?
	
	timers_.emplace_back(d);
	signalled_ = true;
	cond_.Signal();
	return d.id_;
}

void CEventLoop::StopTimer(CEventHandler* handler, timer_id id)
{
	if (id) {
		wxMutexLocker lock(sync_);
		for (auto it = timers_.begin(); it != timers_.end(); ++it) {
			if (it->id_ == id) {
				timers_.erase(it);
				break;
			}
		}
	}
}

bool CEventLoop::ProcessEvent()
{
	Events::value_type ev{};
	bool requestMore = false;

	if (pending_events_.empty()) {
		return false;
	}
	ev = pending_events_.front();
	pending_events_.pop_front();
	requestMore = !pending_events_.empty();
	
	sync_.Unlock();
	if (ev.first && ev.second) {
		(*ev.first)(*ev.second);
	}
	delete ev.second;
	sync_.Lock();

	return requestMore;
}

wxThread::ExitCode CEventLoop::Entry()
{
	sync_.Lock();
	while (!quit_) {
		if (!signalled_) {
			int wait = GetNextWaitInterval();
			if (wait == std::numeric_limits<int>::max()) {
				cond_.Wait();
			}
			else if (wait) {
				cond_.WaitTimeout(wait);
			}
		}
		signalled_ = false;

		if (!ProcessTimers()) {
			if (ProcessEvent()) {
				signalled_ = true;
			}
		}
	}
	sync_.Unlock();

	return 0;
}

bool CEventLoop::ProcessTimers()
{
	CDateTime const now(CDateTime::Now());
	for (auto it = timers_.begin(); it != timers_.end(); ++it) {
		int diff = static_cast<int>((it->deadline_ - now).GetMilliseconds().GetValue());
		if (diff <= -60000) {
			// Did the system time change? Update deadline
			it->deadline_ = now + it->ms_interval_;
			continue;
		}
		else if (diff <= 0) {
			CEventHandler *const handler = it->handler_;
			auto const id = it->id_;
			if (it->one_shot_) {
				timers_.erase(it);
			}
			else {
				it->deadline_ = now + wxTimeSpan::Milliseconds(it->ms_interval_);
			}

			sync_.Unlock();
			(*handler)(CTimerEvent(id));
			sync_.Lock();
			return true;
		}
	}

	return false;
}

int CEventLoop::GetNextWaitInterval()
{
	int wait = std::numeric_limits<int>::max();

	CDateTime const now(CDateTime::Now());
	for (auto & timer : timers_) {
		int diff = static_cast<int>((timer.deadline_ - now).GetMilliseconds().GetValue());
		if (diff <= -60000) {
			// Did the system time change? Update deadline
			timer.deadline_ = now + timer.ms_interval_;
			diff = timer.ms_interval_;
		}
		
		if (diff < wait) {
			wait = diff;
		}
	}

	if (wait < 0) {
		wait = 0;
	}

	return wait;
}
