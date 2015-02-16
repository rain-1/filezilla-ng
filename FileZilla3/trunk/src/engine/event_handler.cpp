#include <filezilla.h>

#include "event_handler.h"
#include "event_loop.h"

CEventHandler::CEventHandler(CEventLoop& loop)
	: event_loop_(loop)
{
}

CEventHandler::~CEventHandler()
{
	wxASSERT(removing_); // To avoid races, the base class must have removed us already
}

void CEventHandler::RemoveHandler()
{
	event_loop_.RemoveHandler(this);
}

void CEventHandler::ChangeHandler(CEventHandler* newHandler, void const* derived_type)
{
	if (newHandler) {
		event_loop_.ChangeHandler(this, newHandler, derived_type);
	}
	else {
		event_loop_.RemoveEvents(this, derived_type);
	}
}

void CEventHandler::RemoveEvents(void const* derived_type)
{
	event_loop_.RemoveEvents(this, derived_type);
}

timer_id CEventHandler::AddTimer(int ms_interval, bool one_shot)
{
	return event_loop_.AddTimer(this, ms_interval, one_shot);
}

void CEventHandler::StopTimer(timer_id id)
{
	event_loop_.StopTimer(id);
}
