#include <filezilla.h>

#include "backend.h"
#include "socket.h"
#include <errno.h>

CBackend::CBackend(fz::event_handler* pEvtHandler) : m_pEvtHandler(pEvtHandler)
{
}

CBackend::~CBackend()
{
	RemoveSocketEvents(m_pEvtHandler, this);
}

CSocketBackend::CSocketBackend(fz::event_handler* pEvtHandler, CSocket & socket, CRateLimiter& rateLimiter)
	: CBackend(pEvtHandler)
	, socket_(socket)
	, m_rateLimiter(rateLimiter)
{
	socket_.SetEventHandler(pEvtHandler);
	m_rateLimiter.AddObject(this);
}

CSocketBackend::~CSocketBackend()
{
	socket_.SetEventHandler(0);
	m_rateLimiter.RemoveObject(this);
}

int CSocketBackend::Write(const void *buffer, unsigned int len, int& error)
{
	int64_t max = GetAvailableBytes(CRateLimiter::outbound);
	if (max == 0) {
		Wait(CRateLimiter::outbound);
		error = EAGAIN;
		return -1;
	}
	else if (max > 0 && max < len) {
		len = static_cast<unsigned int>(max);
	}

	int written = socket_.Write(buffer, len, error);

	if (written > 0 && max != -1)
		UpdateUsage(CRateLimiter::outbound, written);

	return written;
}

int CSocketBackend::Read(void *buffer, unsigned int len, int& error)
{
	int64_t max = GetAvailableBytes(CRateLimiter::inbound);
	if (max == 0) {
		Wait(CRateLimiter::inbound);
		error = EAGAIN;
		return -1;
	}
	else if (max > 0 && max < len) {
		len = static_cast<unsigned int>(max);
	}

	int read = socket_.Read(buffer, len, error);

	if (read > 0 && max != -1)
		UpdateUsage(CRateLimiter::inbound, read);

	return read;
}

int CSocketBackend::Peek(void *buffer, unsigned int len, int& error)
{
	return socket_.Peek(buffer, len, error);
}

void CSocketBackend::OnRateAvailable(CRateLimiter::rate_direction direction)
{
	if (direction == CRateLimiter::outbound)
		m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::write, 0);
	else
		m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::read, 0);
}
