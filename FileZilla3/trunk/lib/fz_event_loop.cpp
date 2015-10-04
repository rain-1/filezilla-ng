#include "fz_event_loop.hpp"
#include "fz_event_handler.hpp"

#include "fz_util.hpp"

#include <algorithm>
#include <cassert>

namespace fz {

event_loop::event_loop()
	: sync_(false)
{
	run();
}

event_loop::~event_loop()
{
	{
		scoped_lock lock(sync_);
		quit_ = true;
		cond_.signal(lock);
	}

	join();

	scoped_lock lock(sync_);
	for (auto & v : pending_events_) {
		delete v.second;
	}
}

void event_loop::send_event(event_handler* handler, event_base* evt)
{
	{
		scoped_lock lock(sync_);
		if (!handler->removing_) {
			if (pending_events_.empty()) {
				cond_.signal(lock);
			}
			pending_events_.emplace_back(handler, evt);
			return;
		}
	}

	delete evt;
}

void event_loop::remove_handler(event_handler* handler)
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
		deadline_ = monotonic_clock();
	}

	while (active_handler_ == handler) {
		l.unlock();
		sleep(duration::from_milliseconds(1));
		l.lock();
	}
}

void event_loop::filter_events(std::function<bool(Events::value_type &)> const& filter)
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

timer_id event_loop::add_timer(event_handler* handler, duration const& interval, bool one_shot)
{
	timer_data d;
	d.handler_ = handler;
	if (!one_shot) {
		d.interval_ = interval;
	}
	d.deadline_ = monotonic_clock::now() + interval;

	scoped_lock lock(sync_);
	if (!handler->removing_) {
		d.id_ = ++next_timer_id_; // 64bit, can this really ever overflow?

		timers_.emplace_back(d);
		if (!deadline_ || d.deadline_ < deadline_) {
			// Our new time is the next timer to trigger
			deadline_ = d.deadline_;
			cond_.signal(lock);
		}
	}
	return d.id_;
}

void event_loop::stop_timer(timer_id id)
{
	if (id) {
		scoped_lock lock(sync_);
		for (auto it = timers_.begin(); it != timers_.end(); ++it) {
			if (it->id_ == id) {
				timers_.erase(it);
				if (timers_.empty()) {
					deadline_ = monotonic_clock();
				}
				break;
			}
		}
	}
}

bool event_loop::process_event(scoped_lock & l)
{
	Events::value_type ev{};

	if (pending_events_.empty()) {
		return false;
	}
	ev = pending_events_.front();
	pending_events_.pop_front();

	assert(ev.first);
	assert(ev.second);
	assert(!ev.first->removing_);

	active_handler_ = ev.first;

	l.unlock();
	(*ev.first)(*ev.second);
	delete ev.second;
	l.lock();

	active_handler_ = 0;

	return true;
}

void event_loop::entry()
{
	scoped_lock l(sync_);
	while (!quit_) {
		monotonic_clock const now(monotonic_clock::now());
		if (process_timers(l, now)) {
			continue;
		}
		if (process_event(l)) {
			continue;
		}

		// Nothing to do, now we wait
		if (deadline_) {
			int wait = static_cast<int>((deadline_ - now).get_milliseconds());
			cond_.wait(l, wait);
		}
		else {
			cond_.wait(l);
		}
	}
}

bool event_loop::process_timers(scoped_lock & l, monotonic_clock const& now)
{
	if (!deadline_ || now < deadline_) {
		// There's no deadline or deadline has not yet expired
		return false;
	}

	// Update deadline_, stop at first expired timer
	deadline_ = monotonic_clock();
	auto it = timers_.begin();
	for (; it != timers_.end(); ++it) {
		if (!deadline_ || it->deadline_ < deadline_) {
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
			if (!deadline_ || it2->deadline_ < deadline_) {
				deadline_ = it2->deadline_;
			}
		}

		event_handler *const handler = it->handler_;
		auto const id = it->id_;

		// Update the expired timer
		if (!it->interval_) {
			timers_.erase(it);
		}
		else {
			it->deadline_ = std::move(now + it->interval_);
			if (!deadline_ || it->deadline_ < deadline_) {
				deadline_ = it->deadline_;
			}
		}

		// Call event handler
		assert(!handler->removing_);

		active_handler_ = handler;

		l.unlock();
		(*handler)(timer_event(id));
		l.lock();

		active_handler_ = 0;

		return true;
	}

	return false;
}

}