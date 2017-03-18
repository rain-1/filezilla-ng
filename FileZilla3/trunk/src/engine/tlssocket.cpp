#include <filezilla.h>

#include "ControlSocket.h"
#include "tlssocket.h"
#include "tlssocket_impl.h"

CTlsSocket::CTlsSocket(fz::event_handler* pEvtHandler, CSocket& pSocket, CControlSocket* pOwner)
	: event_handler(pOwner->event_loop_)
	, CBackend(pEvtHandler)
{
	impl_ = std::make_unique<CTlsSocketImpl>(*this, pSocket, pOwner);
}

CTlsSocket::~CTlsSocket()
{
	remove_handler();
}

bool CTlsSocket::Init()
{
	return impl_->Init();
}

void CTlsSocket::Uninit()
{
	return impl_->Uninit();
}

int CTlsSocket::Handshake(CTlsSocket const* pPrimarySocket, bool try_resume)
{
	return impl_->Handshake(pPrimarySocket ? pPrimarySocket->impl_.get() : 0, try_resume);
}

int CTlsSocket::Read(void *buffer, unsigned int size, int& error)
{
	return impl_->Read(buffer, size, error);
}

int CTlsSocket::Peek(void *buffer, unsigned int size, int& error)
{
	return impl_->Peek(buffer, size, error);
}

int CTlsSocket::Write(const void *buffer, unsigned int size, int& error)
{
	return impl_->Write(buffer, size, error);
}

int CTlsSocket::Shutdown()
{
	return impl_->Shutdown();
}

void CTlsSocket::TrustCurrentCert(bool trusted)
{
	return impl_->TrustCurrentCert(trusted);
}

CTlsSocket::TlsState CTlsSocket::GetState() const
{
	return impl_->GetState();
}

std::wstring CTlsSocket::GetProtocolName()
{
	return impl_->GetProtocolName();
}

std::wstring CTlsSocket::GetKeyExchange()
{
	return impl_->GetKeyExchange();
}

std::wstring CTlsSocket::GetCipherName()
{
	return impl_->GetCipherName();
}

std::wstring CTlsSocket::GetMacName()
{
	return impl_->GetMacName();
}

int CTlsSocket::GetAlgorithmWarnings()
{
	return impl_->GetAlgorithmWarnings();
}

bool CTlsSocket::ResumedSession() const
{
	return impl_->ResumedSession();
}

std::string CTlsSocket::ListTlsCiphers(std::string const& priority)
{
	return CTlsSocketImpl::ListTlsCiphers(priority);
}

bool CTlsSocket::SetClientCertificate(fz::native_string const& keyfile, fz::native_string const& certs, fz::native_string const& password)
{
	return impl_->SetClientCertificate(keyfile, certs, password);
}

void CTlsSocket::operator()(fz::event_base const& ev)
{
	return impl_->operator()(ev);
}

void CTlsSocket::OnRateAvailable(CRateLimiter::rate_direction direction)
{
	return impl_->OnRateAvailable(direction);
}

std::wstring CTlsSocket::GetGnutlsVersion()
{
	return CTlsSocketImpl::GetGnutlsVersion();
}
