#include <filezilla.h>
#include "ControlSocket.h"
#include "ftpcontrolsocket.h"
#include "sftpcontrolsocket.h"
#include "directorycache.h"
#include "logging_private.h"
#include "httpcontrolsocket.h"
#include "ratelimiter.h"
#include "pathcache.h"

wxCriticalSection CFileZillaEnginePrivate::mutex_;
std::list<CFileZillaEnginePrivate*> CFileZillaEnginePrivate::m_engineList;
int CFileZillaEnginePrivate::m_activeStatus[2] = {0, 0};
std::list<CFileZillaEnginePrivate::t_failedLogins> CFileZillaEnginePrivate::m_failedLogins;

CFileZillaEnginePrivate::CFileZillaEnginePrivate(CFileZillaEngineContext& context)
	: CEventHandler(context.GetEventLoop())
	, event_loop_(context.GetEventLoop())
	, socket_event_dispatcher_(context.GetSocketEventDispatcher())
	, m_options(context.GetOptions())
	, m_rateLimiter(context.GetRateLimiter())
	, directory_cache_(context.GetDirectoryCache())
{
	m_engineList.push_back(this);

	static int id = 0;
	m_engine_id = ++id;

	m_pLogging = new CLogging(this);
}

CFileZillaEnginePrivate::~CFileZillaEnginePrivate()
{
	delete m_pControlSocket;
	delete m_pCurrentCommand;

	// Delete notification list
	for (auto iter = m_NotificationList.begin(); iter != m_NotificationList.end(); ++iter)
		delete *iter;

	// Remove ourself from the engine list
	for (std::list<CFileZillaEnginePrivate*>::iterator iter = m_engineList.begin(); iter != m_engineList.end(); ++iter)
		if (*iter == this)
		{
			m_engineList.erase(iter);
			break;
		}

	delete m_pLogging;

	if (m_engineList.empty())
		CSocket::Cleanup(true);
}

void CFileZillaEnginePrivate::OnEngineEvent(EngineNotificationType type)
{
	switch (type)
	{
	case engineCancel:
		if (!IsBusy())
			break;
		if (m_pControlSocket)
			m_pControlSocket->Cancel();
		else if (m_pCurrentCommand)
			ResetOperation(FZ_REPLY_CANCELED);
		break;
	case engineTransferEnd:
		if (m_pControlSocket)
			m_pControlSocket->TransferEnd();
	default:
		break;
	}
}

bool CFileZillaEnginePrivate::IsBusy() const
{
	return m_pCurrentCommand != 0;
}

bool CFileZillaEnginePrivate::IsConnected() const
{
	if (!m_pControlSocket)
		return false;

	return m_pControlSocket->Connected();
}

const CCommand *CFileZillaEnginePrivate::GetCurrentCommand() const
{
	return m_pCurrentCommand;
}

Command CFileZillaEnginePrivate::GetCurrentCommandId() const
{
	if (!m_pCurrentCommand)
		return Command::none;

	else
		return GetCurrentCommand()->GetId();
}

void CFileZillaEnginePrivate::AddNotification(CNotification *pNotification)
{
	notification_mutex_.Enter();
	m_NotificationList.push_back(pNotification);

	if (m_maySendNotificationEvent && m_pEventHandler) {
		m_maySendNotificationEvent = false;
		notification_mutex_.Leave();
		wxFzEvent evt(wxID_ANY);
		evt.engine_ = dynamic_cast<CFileZillaEngine*>(this);
		wxASSERT(evt.engine_);
		wxPostEvent(m_pEventHandler, evt);
	}
	else
		notification_mutex_.Leave();
}

int CFileZillaEnginePrivate::ResetOperation(int nErrorCode)
{
	m_pLogging->LogMessage(MessageType::Debug_Debug, _T("CFileZillaEnginePrivate::ResetOperation(%d)"), nErrorCode);

	if (nErrorCode & FZ_REPLY_DISCONNECTED)
		m_lastListDir.clear();

	if (m_pCurrentCommand)
	{
		if ((nErrorCode & FZ_REPLY_NOTSUPPORTED) == FZ_REPLY_NOTSUPPORTED)
		{
			wxASSERT(m_bIsInCommand);
			m_pLogging->LogMessage(MessageType::Error, _("Command not supported by this protocol"));
		}

		if (m_pCurrentCommand->GetId() == Command::connect)
		{
			if (!(nErrorCode & ~(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED | FZ_REPLY_TIMEOUT | FZ_REPLY_CRITICALERROR | FZ_REPLY_PASSWORDFAILED)) &&
				nErrorCode & (FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED))
			{
				const CConnectCommand *pConnectCommand = (CConnectCommand *)m_pCurrentCommand;

				RegisterFailedLoginAttempt(pConnectCommand->GetServer(), (nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR);

				if ((nErrorCode & FZ_REPLY_CRITICALERROR) != FZ_REPLY_CRITICALERROR) {
					++m_retryCount;
					if (m_retryCount < m_options.GetOptionVal(OPTION_RECONNECTCOUNT) && pConnectCommand->RetryConnecting()) {
						unsigned int delay = GetRemainingReconnectDelay(pConnectCommand->GetServer());
						if (!delay)
							delay = 1;
						m_pLogging->LogMessage(MessageType::Status, _("Waiting to retry..."));
						if (m_retryTimer != -1)
							StopTimer(m_retryTimer);
						m_retryTimer = AddTimer(delay, true);
						return FZ_REPLY_WOULDBLOCK;
					}
				}
			}
		}

		if (!m_bIsInCommand)
		{
			COperationNotification *notification = new COperationNotification();
			notification->nReplyCode = nErrorCode;
			notification->commandId = m_pCurrentCommand->GetId();
			AddNotification(notification);
		}
		else
			m_nControlSocketError |= nErrorCode;

		delete m_pCurrentCommand;
		m_pCurrentCommand = 0;
	}
	else if (nErrorCode & FZ_REPLY_DISCONNECTED)
	{
		if (!m_bIsInCommand)
		{
			COperationNotification *notification = new COperationNotification();
			notification->nReplyCode = nErrorCode;
			notification->commandId = Command::none;
			AddNotification(notification);
		}
	}

	return nErrorCode;
}

void CFileZillaEnginePrivate::SetActive(int direction)
{
	wxCriticalSectionLocker lock(mutex_);
	if (!m_activeStatus[direction])
		AddNotification(new CActiveNotification(direction));
	m_activeStatus[direction] = 2;
}

unsigned int CFileZillaEnginePrivate::GetNextAsyncRequestNumber()
{
	wxCriticalSectionLocker lock(notification_mutex_);
	return ++m_asyncRequestCounter;
}

// Command handlers
int CFileZillaEnginePrivate::Connect(const CConnectCommand &command)
{
	if (IsConnected())
		return FZ_REPLY_ALREADYCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	m_retryCount = 0;

	if (m_pControlSocket)
	{
		// Need to delete before setting m_pCurrentCommand.
		// The destructor can call CFileZillaEnginePrivate::ResetOperation
		// which would delete m_pCurrentCommand
		delete m_pControlSocket;
		m_pControlSocket = 0;
		m_nControlSocketError = 0;
	}

	m_pCurrentCommand = command.Clone();

	if (command.GetServer().GetPort() != CServer::GetDefaultPort(command.GetServer().GetProtocol()))
	{
		ServerProtocol protocol = CServer::GetProtocolFromPort(command.GetServer().GetPort(), true);
		if (protocol != UNKNOWN && protocol != command.GetServer().GetProtocol())
			m_pLogging->LogMessage(MessageType::Status, _("Selected port usually in use by a different protocol."));
	}

	return ContinueConnect();
}

int CFileZillaEnginePrivate::Disconnect(const CDisconnectCommand &command)
{
	if (!IsConnected())
		return FZ_REPLY_OK;

	m_pCurrentCommand = command.Clone();
	int res = m_pControlSocket->Disconnect();
	if (res == FZ_REPLY_OK)
	{
		delete m_pControlSocket;
		m_pControlSocket = 0;
	}

	return res;
}

int CFileZillaEnginePrivate::Cancel(const CCancelCommand &)
{
	if (!IsBusy())
		return FZ_REPLY_OK;

	if (m_retryTimer != -1) {
		wxASSERT(m_pCurrentCommand && m_pCurrentCommand->GetId() == Command::connect);

		delete m_pControlSocket;
		m_pControlSocket = 0;

		delete m_pCurrentCommand;
		m_pCurrentCommand = 0;

		StopTimer(m_retryTimer);
		m_retryTimer = -1;

		m_pLogging->LogMessage(MessageType::Error, _("Connection attempt interrupted by user"));
		COperationNotification *notification = new COperationNotification();
		notification->nReplyCode = FZ_REPLY_DISCONNECTED|FZ_REPLY_CANCELED;
		notification->commandId = Command::connect;
		AddNotification(notification);

		return FZ_REPLY_WOULDBLOCK;
	}

	SendEvent(CFileZillaEngineEvent(engineCancel));

	return FZ_REPLY_WOULDBLOCK;
}

int CFileZillaEnginePrivate::List(const CListCommand &command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (command.GetPath().empty() && !command.GetSubDir().empty())
		return FZ_REPLY_SYNTAXERROR;

	if (command.GetFlags() & LIST_FLAG_LINK && command.GetSubDir().empty())
		return FZ_REPLY_SYNTAXERROR;

	int flags = command.GetFlags();
	bool const refresh = (command.GetFlags() & LIST_FLAG_REFRESH) != 0;
	bool const avoid = (command.GetFlags() & LIST_FLAG_AVOID) != 0;
	if (refresh && avoid)
		return FZ_REPLY_SYNTAXERROR;

	if (!refresh && !command.GetPath().empty()) {
		const CServer* pServer = m_pControlSocket->GetCurrentServer();
		if (pServer) {
			CServerPath path(CPathCache::Lookup(*pServer, command.GetPath(), command.GetSubDir()));
			if (path.empty() && command.GetSubDir().empty())
				path = command.GetPath();
			if (!path.empty()) {
				CDirectoryListing *pListing = new CDirectoryListing;
				bool is_outdated = false;
				bool found = directory_cache_.Lookup(*pListing, *pServer, path, true, is_outdated);
				if (found && !is_outdated) {
					if (pListing->get_unsure_flags())
						flags |= LIST_FLAG_REFRESH;
					else {
						if (!avoid) {
							m_lastListDir = pListing->path;
							m_lastListTime = CDateTime::Now();
							CDirectoryListingNotification *pNotification = new CDirectoryListingNotification(pListing->path);
							AddNotification(pNotification);
						}
						delete pListing;
						return FZ_REPLY_OK;
					}
				}
				if (is_outdated)
					flags |= LIST_FLAG_REFRESH;
				delete pListing;
			}
		}
	}
	if (IsBusy())
		return FZ_REPLY_BUSY;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->List(command.GetPath(), command.GetSubDir(), flags);
}

int CFileZillaEnginePrivate::FileTransfer(const CFileTransferCommand &command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->FileTransfer(command.GetLocalFile(), command.GetRemotePath(), command.GetRemoteFile(), command.Download(), command.GetTransferSettings());
}

int CFileZillaEnginePrivate::RawCommand(const CRawCommand& command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	if (command.GetCommand().empty())
		return FZ_REPLY_SYNTAXERROR;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->RawCommand(command.GetCommand());
}

int CFileZillaEnginePrivate::Delete(const CDeleteCommand& command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	if (command.GetPath().empty() ||
		command.GetFiles().empty())
		return FZ_REPLY_SYNTAXERROR;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->Delete(command.GetPath(), command.GetFiles());
}

int CFileZillaEnginePrivate::RemoveDir(const CRemoveDirCommand& command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	if (command.GetPath().empty() ||
		command.GetSubDir().empty())
		return FZ_REPLY_SYNTAXERROR;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->RemoveDir(command.GetPath(), command.GetSubDir());
}

int CFileZillaEnginePrivate::Mkdir(const CMkdirCommand& command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	if (command.GetPath().empty() || !command.GetPath().HasParent())
		return FZ_REPLY_SYNTAXERROR;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->Mkdir(command.GetPath());
}

int CFileZillaEnginePrivate::Rename(const CRenameCommand& command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	if (command.GetFromPath().empty() || command.GetToPath().empty() ||
		command.GetFromFile().empty() || command.GetToFile().empty())
		return FZ_REPLY_SYNTAXERROR;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->Rename(command);
}

int CFileZillaEnginePrivate::Chmod(const CChmodCommand& command)
{
	if (!IsConnected())
		return FZ_REPLY_NOTCONNECTED;

	if (IsBusy())
		return FZ_REPLY_BUSY;

	if (command.GetPath().empty() || command.GetFile().empty() ||
		command.GetPermission().empty())
		return FZ_REPLY_SYNTAXERROR;

	m_pCurrentCommand = command.Clone();
	return m_pControlSocket->Chmod(command);
}

void CFileZillaEnginePrivate::SendDirectoryListingNotification(const CServerPath& path, bool onList, bool modified, bool failed)
{
	wxASSERT(m_pControlSocket);

	const CServer* const pOwnServer = m_pControlSocket->GetCurrentServer();
	wxASSERT(pOwnServer);

	m_lastListDir = path;

	if (failed)
	{
		CDirectoryListingNotification *pNotification = new CDirectoryListingNotification(path, false, true);
		AddNotification(pNotification);
		m_lastListTime = CMonotonicTime::Now();

		// On failed messages, we don't notify other engines
		return;
	}

	CMonotonicTime changeTime;
	if (!directory_cache_.GetChangeTime(changeTime, *pOwnServer, path))
		return;

	CDirectoryListingNotification *pNotification = new CDirectoryListingNotification(path, !onList);
	AddNotification(pNotification);
	m_lastListTime = changeTime;

	if (!modified)
		return;

	// Iterate over the other engine, send notification if last listing
	// directory is the same
	for (std::list<CFileZillaEnginePrivate*>::iterator iter = m_engineList.begin(); iter != m_engineList.end(); ++iter)
	{
		CFileZillaEnginePrivate* const pEngine = *iter;
		if (!pEngine->m_pControlSocket || pEngine->m_pControlSocket == m_pControlSocket)
			continue;

		const CServer* const pServer = pEngine->m_pControlSocket->GetCurrentServer();
		if (!pServer || *pServer != *pOwnServer)
			continue;

		if (pEngine->m_lastListDir != path)
			continue;

		if (pEngine->m_lastListTime.GetTime().IsValid() && changeTime <= pEngine->m_lastListTime)
			continue;

		pEngine->m_lastListTime = changeTime;
		CDirectoryListingNotification *pNotification = new CDirectoryListingNotification(path, true);
		pEngine->AddNotification(pNotification);
	}
}

void CFileZillaEnginePrivate::RegisterFailedLoginAttempt(const CServer& server, bool critical)
{
	std::list<t_failedLogins>::iterator iter = m_failedLogins.begin();
	while (iter != m_failedLogins.end())
	{
		const wxTimeSpan span = wxDateTime::UNow() - iter->time;
		if (span.GetSeconds() >= m_options.GetOptionVal(OPTION_RECONNECTDELAY) ||
			iter->server == server || (!critical && (iter->server.GetHost() == server.GetHost() && iter->server.GetPort() == server.GetPort())))
		{
			std::list<t_failedLogins>::iterator prev = iter;
			++iter;
			m_failedLogins.erase(prev);
		}
		else
			++iter;
	}

	t_failedLogins failure;
	failure.server = server;
	failure.time = wxDateTime::UNow();
	m_failedLogins.push_back(failure);
}

unsigned int CFileZillaEnginePrivate::GetRemainingReconnectDelay(const CServer& server)
{
	std::list<t_failedLogins>::iterator iter = m_failedLogins.begin();
	while (iter != m_failedLogins.end())
	{
		const wxTimeSpan span = wxDateTime::UNow() - iter->time;
		const int delay = m_options.GetOptionVal(OPTION_RECONNECTDELAY);
		if (span.GetSeconds() >= delay)
		{
			std::list<t_failedLogins>::iterator prev = iter;
			++iter;
			m_failedLogins.erase(prev);
		}
		else if (!iter->critical && iter->server.GetHost() == server.GetHost() && iter->server.GetPort() == server.GetPort())
			return delay * 1000 - span.GetMilliseconds().GetLo();
		else if (iter->server == server)
			return delay * 1000 - span.GetMilliseconds().GetLo();
		else
			++iter;
	}

	return 0;
}

void CFileZillaEnginePrivate::OnTimer(int)
{
	if (m_retryTimer == -1) {
		return;
	}
	m_retryTimer = -1;

	if (!m_pCurrentCommand || m_pCurrentCommand->GetId() != Command::connect) {
		wxFAIL_MSG(_T("CFileZillaEnginePrivate::OnTimer called without pending Command::connect"));
		return;
	}
	wxASSERT(!IsConnected());

	delete m_pControlSocket;
	m_pControlSocket = 0;

	ContinueConnect();
}

int CFileZillaEnginePrivate::ContinueConnect()
{
	const CConnectCommand *pConnectCommand = (CConnectCommand *)m_pCurrentCommand;
	const CServer& server = pConnectCommand->GetServer();
	unsigned int delay = GetRemainingReconnectDelay(server);
	if (delay) {
		m_pLogging->LogMessage(MessageType::Status, wxPLURAL("Delaying connection for %d second due to previously failed connection attempt...", "Delaying connection for %d seconds due to previously failed connection attempt...", (delay + 999) / 1000), (delay + 999) / 1000);
		if (m_retryTimer != -1)
			StopTimer(m_retryTimer);
		m_retryTimer = AddTimer(delay, true);
		return FZ_REPLY_WOULDBLOCK;
	}

	switch (server.GetProtocol())
	{
	case FTP:
	case FTPS:
	case FTPES:
	case INSECURE_FTP:
		m_pControlSocket = new CFtpControlSocket(this);
		break;
	case SFTP:
		m_pControlSocket = new CSftpControlSocket(this);
		break;
	case HTTP:
	case HTTPS:
		m_pControlSocket = new CHttpControlSocket(this);
		break;
	default:
		m_pLogging->LogMessage(MessageType::Debug_Warning, _T("Not a valid protocol: %d"), server.GetProtocol());
		return FZ_REPLY_SYNTAXERROR|FZ_REPLY_DISCONNECTED;
	}

	int res = m_pControlSocket->Connect(server);
	if (m_retryTimer != -1)
		return FZ_REPLY_WOULDBLOCK;

	return res;
}

void CFileZillaEnginePrivate::InvalidateCurrentWorkingDirs(const CServerPath& path)
{
	wxASSERT(m_pControlSocket);
	const CServer* const pOwnServer = m_pControlSocket->GetCurrentServer();
	wxASSERT(pOwnServer);

	for (std::list<CFileZillaEnginePrivate*>::iterator iter = m_engineList.begin(); iter != m_engineList.end(); ++iter)
	{
		if (*iter == this)
			continue;

		CFileZillaEnginePrivate* pEngine = *iter;
		if (!pEngine->m_pControlSocket)
			continue;

		const CServer* const pServer = pEngine->m_pControlSocket->GetCurrentServer();
		if (!pServer || *pServer != *pOwnServer)
			continue;

		pEngine->m_pControlSocket->InvalidateCurrentWorkingDir(path);
	}
}

void CFileZillaEnginePrivate::operator()(CEventBase const& ev)
{
	if (Dispatch<CTimerEvent>(ev, this, &CFileZillaEnginePrivate::OnTimer))
		return;

	Dispatch<CFileZillaEngineEvent>(ev, this, &CFileZillaEnginePrivate::OnEngineEvent);
}