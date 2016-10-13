#include <filezilla.h>
#include "ControlSocket.h"
#include "directorycache.h"
#include "engineprivate.h"

CFileZillaEngine::CFileZillaEngine(CFileZillaEngineContext& engine_context, EngineNotificationHandler& notificationHandler)
	: impl_(new CFileZillaEnginePrivate(engine_context, *this, notificationHandler))
{
}

CFileZillaEngine::~CFileZillaEngine()
{
	delete impl_;
}

int CFileZillaEngine::Execute(const CCommand &command)
{
	return impl_->Execute(command);
}

std::unique_ptr<CNotification> CFileZillaEngine::GetNextNotification()
{
	return impl_->GetNextNotification();
}

bool CFileZillaEngine::SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification)
{
	return impl_->SetAsyncRequestReply(std::move(pNotification));
}

bool CFileZillaEngine::IsPendingAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> const& pNotification)
{
	return impl_->IsPendingAsyncRequestReply(pNotification);
}

bool CFileZillaEngine::IsActive(CFileZillaEngine::_direction direction)
{
	return CFileZillaEnginePrivate::IsActive(direction);
}

CTransferStatus CFileZillaEngine::GetTransferStatus(bool &changed)
{
	return impl_->GetTransferStatus(changed);
}

int CFileZillaEngine::CacheLookup(const CServerPath& path, CDirectoryListing& listing)
{
	return impl_->CacheLookup(path, listing);
}

int CFileZillaEngine::Cancel()
{
	return impl_->Cancel();
}

bool CFileZillaEngine::IsBusy() const
{
	return impl_->IsBusy();
}

bool CFileZillaEngine::IsConnected() const
{
	return impl_->IsConnected();
}
