// FileZillaEngine.cpp: Implementierung der Klasse CFileZillaEngine.
//
//////////////////////////////////////////////////////////////////////

#include <filezilla.h>
#include "ControlSocket.h"
#include "directorycache.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CFileZillaEngine::CFileZillaEngine(CFileZillaEngineContext& engine_context)
	: CFileZillaEnginePrivate(engine_context)
{
}

CFileZillaEngine::~CFileZillaEngine()
{
	RemoveHandler();
	m_maySendNotificationEvent = false;
}

int CFileZillaEngine::Init(wxEvtHandler *pEventHandler)
{
	wxCriticalSectionLocker lock(mutex_);
	m_pEventHandler = pEventHandler;
	return FZ_REPLY_OK;
}

int CFileZillaEngine::Execute(const CCommand &command)
{
	wxCriticalSectionLocker lock(mutex_);

	int res = CheckCommandPreconditions(command, true);
	if (res != FZ_REPLY_OK) {
		return res;
	}

	m_pCurrentCommand.reset(command.Clone());
	SendEvent<CCommandEvent>();

	return FZ_REPLY_WOULDBLOCK;
}

std::unique_ptr<CNotification> CFileZillaEngine::GetNextNotification()
{
	wxCriticalSectionLocker lock(notification_mutex_);

	if (m_NotificationList.empty()) {
		m_maySendNotificationEvent = true;
		return 0;
	}
	std::unique_ptr<CNotification> pNotification(m_NotificationList.front());
	m_NotificationList.pop_front();

	return pNotification;
}

bool CFileZillaEngine::SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification)
{
	wxCriticalSectionLocker lock(mutex_);
	if (!CheckAsyncRequestReplyPreconditions(pNotification)) {
		return false;
	}

	SendEvent<CAsyncRequestReplyEvent>(std::move(pNotification));

	return true;
}

bool CFileZillaEngine::IsPendingAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> const& pNotification)
{
	if (!pNotification)
		return false;

	if (!IsBusy())
		return false;

	wxCriticalSectionLocker lock(notification_mutex_);
	return pNotification->requestNumber == m_asyncRequestCounter;
}

bool CFileZillaEngine::IsActive(enum CFileZillaEngine::_direction direction)
{
	wxCriticalSectionLocker lock(mutex_);

	if (m_activeStatus[direction] == 2) {
		m_activeStatus[direction] = 1;
		return true;
	}

	m_activeStatus[direction] = 0;
	return false;
}

bool CFileZillaEngine::GetTransferStatus(CTransferStatus &status, bool &changed)
{
	wxCriticalSectionLocker lock(mutex_);

	if (!m_pControlSocket) {
		changed = false;
		return false;
	}

	return m_pControlSocket->GetTransferStatus(status, changed);
}

int CFileZillaEngine::CacheLookup(const CServerPath& path, CDirectoryListing& listing)
{
	// TODO: Possible optimization: Atomically get current server. The cache has its own mutex.
	wxCriticalSectionLocker lock(mutex_);

	if (!IsConnected())
		return FZ_REPLY_ERROR;

	wxASSERT(m_pControlSocket->GetCurrentServer());

	bool is_outdated = false;
	if (!directory_cache_.Lookup(listing, *m_pControlSocket->GetCurrentServer(), path, true, is_outdated))
		return FZ_REPLY_ERROR;

	return FZ_REPLY_OK;
}

int CFileZillaEngine::Cancel()
{
	wxCriticalSectionLocker lock(mutex_);
	if (!IsBusy())
		return FZ_REPLY_OK;

	SendEvent<CFileZillaEngineEvent>(engineCancel);
	return FZ_REPLY_WOULDBLOCK;
}
