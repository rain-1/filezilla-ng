#include <filezilla.h>
#include "ControlSocket.h"
#include "event_loop.h"
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
	, path_cache_(context.GetPathCache())
{
	m_engineList.push_back(this);

	static int id = 0;
	m_engine_id = ++id;

	m_pLogging = new CLogging(this);
}

CFileZillaEnginePrivate::~CFileZillaEnginePrivate()
{
	m_pControlSocket.reset();
	m_pCurrentCommand.reset();

	// Delete notification list
	for (auto & notification : m_NotificationList) {
		delete notification;
	}

	// Remove ourself from the engine list
	for (auto iter = m_engineList.begin(); iter != m_engineList.end(); ++iter) {
		if (*iter == this) {
			m_engineList.erase(iter);
			break;
		}
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
		DoCancel();
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
	wxCriticalSectionLocker lock(mutex_);
	return m_pCurrentCommand != 0;
}

bool CFileZillaEnginePrivate::IsConnected() const
{
	wxCriticalSectionLocker lock(mutex_);
	if (!m_pControlSocket)
		return false;

	return m_pControlSocket->Connected();
}

const CCommand *CFileZillaEnginePrivate::GetCurrentCommand() const
{
	wxCriticalSectionLocker lock(mutex_);
	return m_pCurrentCommand.get();
}

Command CFileZillaEnginePrivate::GetCurrentCommandId() const
{
	wxCriticalSectionLocker lock(mutex_);
	if (!m_pCurrentCommand)
		return Command::none;
	else
		return GetCurrentCommand()->GetId();
}

void CFileZillaEnginePrivate::AddNotification(CNotification *pNotification)
{
	{
		wxCriticalSectionLocker lock(notification_mutex_);
		m_NotificationList.push_back(pNotification);

		if (!m_maySendNotificationEvent || !m_pEventHandler) {
			return;
		}
		m_maySendNotificationEvent = false;
	}

	wxFzEvent evt(wxID_ANY);
	evt.engine_ = dynamic_cast<CFileZillaEngine*>(this);
	wxASSERT(evt.engine_);
	wxPostEvent(m_pEventHandler, evt);
}

int CFileZillaEnginePrivate::ResetOperation(int nErrorCode)
{
	wxCriticalSectionLocker lock(mutex_);
	m_pLogging->LogMessage(MessageType::Debug_Debug, _T("CFileZillaEnginePrivate::ResetOperation(%d)"), nErrorCode);

	if (nErrorCode & FZ_REPLY_DISCONNECTED)
		m_lastListDir.clear();

	if (m_pCurrentCommand) {
		if ((nErrorCode & FZ_REPLY_NOTSUPPORTED) == FZ_REPLY_NOTSUPPORTED) {
			wxASSERT(m_bIsInCommand);
			m_pLogging->LogMessage(MessageType::Error, _("Command not supported by this protocol"));
		}

		if (m_pCurrentCommand->GetId() == Command::connect) {
			if (!(nErrorCode & ~(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED | FZ_REPLY_TIMEOUT | FZ_REPLY_CRITICALERROR | FZ_REPLY_PASSWORDFAILED)) &&
				nErrorCode & (FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED))
			{
				const CConnectCommand *pConnectCommand = reinterpret_cast<CConnectCommand*>(m_pCurrentCommand.get());

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

		if (!m_bIsInCommand) {
			COperationNotification *notification = new COperationNotification();
			notification->nReplyCode = nErrorCode;
			notification->commandId = m_pCurrentCommand->GetId();
			AddNotification(notification);
		}
		else
			m_nControlSocketError |= nErrorCode;

		m_pCurrentCommand.reset();
	}
	else if (nErrorCode & FZ_REPLY_DISCONNECTED) {
		if (!m_bIsInCommand) {
			COperationNotification *notification = new COperationNotification();
			notification->nReplyCode = nErrorCode;
			notification->commandId = Command::none;
			AddNotification(notification);
		}
	}

	return nErrorCode;
}

void CFileZillaEnginePrivate::SetActive(int direction) {
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

	m_retryCount = 0;

	// Need to delete before setting m_pCurrentCommand.
	// The destructor can call CFileZillaEnginePrivate::ResetOperation
	// which would delete m_pCurrentCommand
	m_pControlSocket.reset();
	m_nControlSocketError = 0;

	if (command.GetServer().GetPort() != CServer::GetDefaultPort(command.GetServer().GetProtocol())) {
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

	int res = m_pControlSocket->Disconnect();
	if (res == FZ_REPLY_OK) {
		m_pControlSocket.reset();
	}

	return res;
}

int CFileZillaEnginePrivate::List(const CListCommand &command)
{
	int flags = command.GetFlags();
	bool const refresh = (command.GetFlags() & LIST_FLAG_REFRESH) != 0;
	bool const avoid = (command.GetFlags() & LIST_FLAG_AVOID) != 0;

	if (!refresh && !command.GetPath().empty()) {
		const CServer* pServer = m_pControlSocket->GetCurrentServer();
		if (pServer) {
			CServerPath path(path_cache_.Lookup(*pServer, command.GetPath(), command.GetSubDir()));
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

	return m_pControlSocket->List(command.GetPath(), command.GetSubDir(), flags);
}

int CFileZillaEnginePrivate::FileTransfer(const CFileTransferCommand &command)
{
	return m_pControlSocket->FileTransfer(command.GetLocalFile(), command.GetRemotePath(), command.GetRemoteFile(), command.Download(), command.GetTransferSettings());
}

int CFileZillaEnginePrivate::RawCommand(const CRawCommand& command)
{
	return m_pControlSocket->RawCommand(command.GetCommand());
}

int CFileZillaEnginePrivate::Delete(const CDeleteCommand& command)
{
	return m_pControlSocket->Delete(command.GetPath(), command.GetFiles());
}

int CFileZillaEnginePrivate::RemoveDir(const CRemoveDirCommand& command)
{
	return m_pControlSocket->RemoveDir(command.GetPath(), command.GetSubDir());
}

int CFileZillaEnginePrivate::Mkdir(const CMkdirCommand& command)
{
	return m_pControlSocket->Mkdir(command.GetPath());
}

int CFileZillaEnginePrivate::Rename(const CRenameCommand& command)
{
	return m_pControlSocket->Rename(command);
}

int CFileZillaEnginePrivate::Chmod(const CChmodCommand& command)
{
	return m_pControlSocket->Chmod(command);
}

void CFileZillaEnginePrivate::SendDirectoryListingNotification(const CServerPath& path, bool onList, bool modified, bool failed)
{
	wxCriticalSectionLocker lock(mutex_);

	wxASSERT(m_pControlSocket);

	const CServer* const pOwnServer = m_pControlSocket->GetCurrentServer();
	wxASSERT(pOwnServer);

	m_lastListDir = path;

	if (failed) {
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
	for (std::list<CFileZillaEnginePrivate*>::iterator iter = m_engineList.begin(); iter != m_engineList.end(); ++iter) {
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
	wxCriticalSectionLocker lock(mutex_);
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
	failure.critical = critical;
	m_failedLogins.push_back(failure);
}

unsigned int CFileZillaEnginePrivate::GetRemainingReconnectDelay(const CServer& server)
{
	wxCriticalSectionLocker lock(mutex_);
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

	m_pControlSocket.reset();

	ContinueConnect();
}

int CFileZillaEnginePrivate::ContinueConnect()
{
	wxCriticalSectionLocker lock(mutex_);

	const CConnectCommand *pConnectCommand = reinterpret_cast<CConnectCommand *>(m_pCurrentCommand.get());
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
		m_pControlSocket = make_unique<CFtpControlSocket>(this);
		break;
	case SFTP:
		m_pControlSocket = make_unique<CSftpControlSocket>(this);
		break;
	case HTTP:
	case HTTPS:
		m_pControlSocket = make_unique<CHttpControlSocket>(this);
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
	wxCriticalSectionLocker lock(mutex_);

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
	wxCriticalSectionLocker lock(mutex_);

	if (Dispatch<CTimerEvent>(ev, this, &CFileZillaEnginePrivate::OnTimer))
		return;

	Dispatch<CFileZillaEngineEvent>(ev, this, &CFileZillaEnginePrivate::OnEngineEvent);
	Dispatch<CCommandEvent>(ev, this, &CFileZillaEnginePrivate::OnCommandEvent);
}

int CFileZillaEnginePrivate::CheckPreconditions(CCommand const& command)
{
	if (!command.valid()) {
		return FZ_REPLY_SYNTAXERROR;
	}
	else if (command.GetId() != Command::cancel && IsBusy()) {
		return FZ_REPLY_BUSY;
	}
	else if (command.GetId() != Command::connect && command.GetId() != Command::disconnect && command.GetId() != Command::cancel && !IsConnected()) {
		return FZ_REPLY_NOTCONNECTED;
	}
	return FZ_REPLY_OK;
}

void CFileZillaEnginePrivate::OnCommandEvent()
{
	wxCriticalSectionLocker lock(mutex_);

	if (m_pCurrentCommand) {
		CCommand& command = *m_pCurrentCommand;
		Command id = command.GetId();

		int res = CheckPreconditions(*m_pCurrentCommand);
		switch (m_pCurrentCommand->GetId())
		{
		case Command::connect:
			res = Connect(reinterpret_cast<const CConnectCommand &>(command));
			break;
		case Command::disconnect:
			res = Disconnect(reinterpret_cast<const CDisconnectCommand &>(command));
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
			res = FZ_REPLY_SYNTAXERROR;
		}

		if (id != Command::disconnect)
			res |= m_nControlSocketError;
		else if (res & FZ_REPLY_DISCONNECTED)
			res = FZ_REPLY_OK;
		m_nControlSocketError = 0;

		if (res != FZ_REPLY_WOULDBLOCK)
			ResetOperation(res);
	}
}

void CFileZillaEnginePrivate::DoCancel()
{
	wxCriticalSectionLocker lock(mutex_);
	if (!IsBusy())
		return;

	if (m_retryTimer != -1) {
		wxASSERT(m_pCurrentCommand && m_pCurrentCommand->GetId() == Command::connect);

		m_pControlSocket.reset();

		m_pCurrentCommand.reset();

		StopTimer(m_retryTimer);
		m_retryTimer = -1;

		m_pLogging->LogMessage(MessageType::Error, _("Connection attempt interrupted by user"));
		COperationNotification *notification = new COperationNotification();
		notification->nReplyCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_CANCELED;
		notification->commandId = Command::connect;
		AddNotification(notification);
	}
	else {
		if (m_pControlSocket)
			m_pControlSocket->Cancel();
		else
			ResetOperation(FZ_REPLY_CANCELED);
	}
}
