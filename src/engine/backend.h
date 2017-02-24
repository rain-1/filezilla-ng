#ifndef FILEZILLA_ENGINE_BACKEND_HEADER
#define FILEZILLA_ENGINE_BACKEND_HEADER

#include "ratelimiter.h"
#include "socket.h"

class CBackend : public CRateLimiterObject, public CSocketEventSource
{
public:
	explicit CBackend(fz::event_handler* pEvtHandler);
	virtual ~CBackend();

	CBackend(CBackend const&) = delete;
	CBackend& operator=(CBackend const&) = delete;

	virtual int Read(void *buffer, unsigned int size, int& error) = 0;
	virtual int Peek(void *buffer, unsigned int size, int& error) = 0;
	virtual int Write(const void *buffer, unsigned int size, int& error) = 0;

	virtual void OnRateAvailable(CRateLimiter::rate_direction direction) = 0;

protected:
	fz::event_handler* const m_pEvtHandler;
};

class CSocket;
class CSocketBackend final : public CBackend
{
public:
	CSocketBackend(fz::event_handler* pEvtHandler, CSocket & socket, CRateLimiter& rateLimiter);
	virtual ~CSocketBackend();
	// Backend definitions
	virtual int Read(void *buffer, unsigned int size, int& error) override;
	virtual int Peek(void *buffer, unsigned int size, int& error) override;
	virtual int Write(const void *buffer, unsigned int size, int& error) override;

protected:
	virtual void OnRateAvailable(CRateLimiter::rate_direction direction) override;

	CSocket &socket_;
	CRateLimiter& m_rateLimiter;
};

#endif
