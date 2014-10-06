#ifndef FILEZILLA_ENGINE_EVENT_HANDLER
#define FILEZILLA_ENGINE_EVENT_HANDLER

class CEventBase;
class CEventLoop;

class CEventHandler
{
public:
	CEventHandler(CEventLoop& loop);
	virtual ~CEventHandler();

	void RemoveHandler();

	virtual void operator()(CEventBase const&) = 0;

	void SendEvent(CEventBase const& evt);

	timer_id AddTimer(int ms_interval, bool one_shot);
	void StopTimer(timer_id id);

	CEventLoop & event_loop_;
};

#endif