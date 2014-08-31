#include <filezilla.h>

#include "event_handler.h"
#include "event_loop.h"

CEventHandler::CEventHandler(CEventLoop& loop)
	: event_loop_(loop)
{
}

CEventHandler::~CEventHandler()
{
	RemoveHandler();
}

void CEventHandler::RemoveHandler()
{
	event_loop_.RemoveHandler(this);
}

void CEventHandler::SendEvent(CEventBase const& evt)
{
	event_loop_.SendEvent(this, evt);
}

int CEventHandler::AddTimer(int ms_interval, bool one_shot)
{
	return event_loop_.AddTimer(this, ms_interval, one_shot);
}

void CEventHandler::StopTimer(int timer_id)
{
	event_loop_.StopTimer(this, timer_id);
}
