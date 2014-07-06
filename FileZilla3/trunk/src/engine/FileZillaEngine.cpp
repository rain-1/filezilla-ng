// FileZillaEngine.cpp: Implementierung der Klasse CFileZillaEngine.
//
//////////////////////////////////////////////////////////////////////

#include <filezilla.h>
#include "ControlSocket.h"
#include "directorycache.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CFileZillaEngine::CFileZillaEngine()
{
}

CFileZillaEngine::~CFileZillaEngine()
{
}

int CFileZillaEngine::Init(wxEvtHandler *pEventHandler, COptionsBase *pOptions)
{
	m_pEventHandler = pEventHandler;
	m_pOptions = pOptions;
	m_pRateLimiter = CRateLimiter::Create(m_pOptions);

	return FZ_REPLY_OK;
}

int CFileZillaEngine::Execute(const CCommand &command)
{
	if (command.GetId() != Command::cancel && IsBusy())
		return FZ_REPLY_BUSY;

	m_bIsInCommand = true;

	int res = FZ_REPLY_INTERNALERROR;
	switch (command.GetId())
	{
	case Command::connect:
		res = Connect(reinterpret_cast<const CConnectCommand &>(command));
		break;
	case Command::disconnect:
		res = Disconnect(reinterpret_cast<const CDisconnectCommand &>(command));
		break;
	case Command::cancel:
		res = Cancel(reinterpret_cast<const CCancelCommand &>(command));
		break;
	case Command::list:
		res = List(reinterpret_cast<const CListCommand &>(command));
		break;
	case Command::transfer:
		res = FileTransfer(reinterpret_cast<const CFileTransferCommand &>(command));
		break;
	case Command::raw:
		res = RawCommand(reinterpret_cast<const CRawCommand&>(command));
		break;
	case Command::del:
		res = Delete(reinterpret_cast<const CDeleteCommand&>(command));
		break;
	case Command::removedir:
		res = RemoveDir(reinterpret_cast<const CRemoveDirCommand&>(command));
		break;
	case Command::mkdir:
		res = Mkdir(reinterpret_cast<const CMkdirCommand&>(command));
		break;
	case Command::rename:
		res = Rename(reinterpret_cast<const CRenameCommand&>(command));
		break;
	case Command::chmod:
		res = Chmod(reinterpret_cast<const CChmodCommand&>(command));
		break;
	default:
		return FZ_REPLY_SYNTAXERROR;
	}

	if (res != FZ_REPLY_WOULDBLOCK)
		ResetOperation(res);

	m_bIsInCommand = false;

	if (command.GetId() != Command::disconnect)
		res |= m_nControlSocketError;
	else if (res & FZ_REPLY_DISCONNECTED)
		res = FZ_REPLY_OK;
	m_nControlSocketError = 0;

	return res;
}

bool CFileZillaEngine::IsBusy() const
{
	return CFileZillaEnginePrivate::IsBusy();
}

bool CFileZillaEngine::IsConnected() const
{
	return CFileZillaEnginePrivate::IsConnected();
}

CNotification* CFileZillaEngine::GetNextNotification()
{
	wxCriticalSectionLocker lock(m_lock);

	if (m_NotificationList.empty()) {
		m_maySendNotificationEvent = true;
		return 0;
	}
	CNotification* pNotification = m_NotificationList.front();
	m_NotificationList.pop_front();

	return pNotification;
}

const CCommand *CFileZillaEngine::GetCurrentCommand() const
{
	return CFileZillaEnginePrivate::GetCurrentCommand();
}

Command CFileZillaEngine::GetCurrentCommandId() const
{
	return CFileZillaEnginePrivate::GetCurrentCommandId();
}

bool CFileZillaEngine::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (!pNotification)
		return false;
	if (!IsBusy())
		return false;

	m_lock.Enter();
	if (pNotification->requestNumber != m_asyncRequestCounter)
	{
		m_lock.Leave();
		return false;
	}
	m_lock.Leave();

	if (!m_pControlSocket)
		return false;

	m_pControlSocket->SetAlive();
	if (!m_pControlSocket->SetAsyncRequestReply(pNotification))
		return false;

	return true;
}

bool CFileZillaEngine::IsPendingAsyncRequestReply(const CAsyncRequestNotification *pNotification)
{
	if (!pNotification)
		return false;
	if (!IsBusy())
		return false;

	wxCriticalSectionLocker lock(m_lock);
	return pNotification->requestNumber == m_asyncRequestCounter;
}

bool CFileZillaEngine::IsActive(enum CFileZillaEngine::_direction direction)
{
	if (m_activeStatus[direction] == 2)
	{
		m_activeStatus[direction] = 1;
		return true;
	}

	m_activeStatus[direction] = 0;
	return false;
}

bool CFileZillaEngine::GetTransferStatus(CTransferStatus &status, bool &changed)
{
	if (!m_pControlSocket)
	{
		changed = false;
		return false;
	}

	return m_pControlSocket->GetTransferStatus(status, changed);
}

int CFileZillaEngine::CacheLookup(const CServerPath& path, CDirectoryListing& listing)
{
	if (!IsConnected())
		return FZ_REPLY_ERROR;

	wxASSERT(m_pControlSocket->GetCurrentServer());

	CDirectoryCache cache;
	bool is_outdated = false;
	if (!cache.Lookup(listing, *m_pControlSocket->GetCurrentServer(), path, true, is_outdated))
		return FZ_REPLY_ERROR;

	return FZ_REPLY_OK;
}
