#include <filezilla.h>

#include "backend.h"
#include "socket.h"
#include <errno.h>

CBackend::CBackend(CSocketEventHandler* pEvtHandler) : m_pEvtHandler(pEvtHandler)
{
}

CSocketBackend::CSocketBackend(CSocketEventHandler* pEvtHandler, CSocket* pSocket, CRateLimiter& rateLimiter)
	: CBackend(pEvtHandler)
	, CSocketEventSource(pEvtHandler->dispatcher_)
	, m_pSocket(pSocket)
	, m_rateLimiter(rateLimiter)
{
	m_pSocket->SetEventHandler(pEvtHandler);
	m_rateLimiter.AddObject(this);
}

CSocketBackend::~CSocketBackend()
{
	m_pSocket->SetEventHandler(0);
	m_rateLimiter.RemoveObject(this);
}

int CSocketBackend::Write(const void *buffer, unsigned int len, int& error)
{
	wxLongLong max = GetAvailableBytes(CRateLimiter::outbound);
	if (max == 0)
	{
		Wait(CRateLimiter::outbound);
		error = EAGAIN;
		return -1;
	}
	else if (max > 0 && max < len)
		len = max.GetLo();

	int written = m_pSocket->Write(buffer, len, error);

	if (written > 0 && max != -1)
		UpdateUsage(CRateLimiter::outbound, written);

	return written;
}

int CSocketBackend::Read(void *buffer, unsigned int len, int& error)
{
	wxLongLong max = GetAvailableBytes(CRateLimiter::inbound);
	if (max == 0)
	{
		Wait(CRateLimiter::inbound);
		error = EAGAIN;
		return -1;
	}
	else if (max > 0 && max < len)
		len = max.GetLo();

	int read = m_pSocket->Read(buffer, len, error);

	if (read > 0 && max != -1)
		UpdateUsage(CRateLimiter::inbound, read);

	return read;
}

int CSocketBackend::Peek(void *buffer, unsigned int len, int& error)
{
	return m_pSocket->Peek(buffer, len, error);
}

void CSocketBackend::OnRateAvailable(enum CRateLimiter::rate_direction direction)
{
	CSocketEvent *evt;
	if (direction == CRateLimiter::outbound)
		evt = new CSocketEvent(m_pEvtHandler, this, CSocketEvent::write);
	else
		evt = new CSocketEvent(m_pEvtHandler, this, CSocketEvent::read);

	dispatcher_.SendEvent(evt);
}
