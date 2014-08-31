#include <filezilla.h>

#include "event_loop.h"

#include <algorithm>

CEventLoop::CEventLoop()
{
}

CEventLoop::~CEventLoop()
{
	wxCriticalSectionLocker lock(sync_);

	for (auto & v : pending_events_) {
		delete v.second;
	}
}

void CEventLoop::SendEvent(CEventHandler* handler, CEventBase const& evt)
{
	{
		wxCriticalSectionLocker lock(sync_);
		pending_events_.emplace_back(handler, evt.clone());
	}

	wxCommandEvent tmp(0, 0);
	wxEvtHandler::AddPendingEvent(tmp);
}

void CEventLoop::RemoveHandler(CEventHandler* handler)
{
	wxCriticalSectionLocker lock(sync_);

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

	auto timers = active_timers_.equal_range(handler);
	for (auto timer = timers.first; timer != timers.second; ++timer) {
		delete timer->second;
	}
	active_timers_.erase(timers.first, timers.second);
}

int CEventLoop::AddTimer(CEventHandler* handler, int ms_interval, bool one_shot)
{
	wxTimer* timer = new wxTimer(this);
	timer->Start(ms_interval, one_shot);

	wxCriticalSectionLocker lock(sync_);
	active_timers_.insert(std::make_pair(handler, timer));
	return timer->GetId();
}

void CEventLoop::StopTimer(CEventHandler* handler, int timer_id)
{
	wxCriticalSectionLocker lock(sync_);
	auto timers = active_timers_.equal_range(handler);
	for (auto timer = timers.first; timer != timers.second; ++timer) {
		if (timer->second->GetId() == timer_id) {
			delete timer->second;
			active_timers_.erase(timer);
			break;
		}
	}
}

bool CEventLoop::ProcessEvent(wxEvent& e)
{
	Events::value_type ev{};
	bool requestMore = false;

	if (e.GetEventType() == wxEVT_TIMER) {
		wxCriticalSectionLocker lock(sync_);
		for (auto it = active_timers_.begin(); it != active_timers_.end(); ++it) {
			if (it->second->GetId() == e.GetId()) {
				ev.first = it->first;
				ev.second = new CTimerEvent(it->second->GetId());
				if (it->second->IsOneShot()) {
					delete it->second;
					active_timers_.erase(it);
				}
				break;
			}
		}
	}
	else {
		wxCriticalSectionLocker lock(sync_);
		if (pending_events_.empty()) {
			return true;
		}
		ev = pending_events_.front();
		pending_events_.pop_front();
		requestMore = !pending_events_.empty();
	}

	if (ev.first && ev.second) {
		(*ev.first)(*ev.second);
	}
	delete ev.second;

	if (requestMore) {
		wxCommandEvent tmp(0, 0);
		wxEvtHandler::AddPendingEvent(tmp);
	}
	return true;
}
