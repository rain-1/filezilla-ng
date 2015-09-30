#include "fz_event_handler.hpp"

#include <cassert>

namespace fz {

event_handler::event_handler(event_loop& loop)
	: event_loop_(loop)
{
}

event_handler::~event_handler()
{
	assert(removing_); // To avoid races, the base class must have removed us already
}

void event_handler::remove_handler()
{
	event_loop_.remove_handler(this);
}

timer_id event_handler::add_timer(duration const& interval, bool one_shot)
{
	return event_loop_.add_timer(this, interval, one_shot);
}

void event_handler::stop_timer(timer_id id)
{
	event_loop_.stop_timer(id);
}

}