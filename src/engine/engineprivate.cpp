#include <filezilla.h>
#include "ControlSocket.h"
#include "directorycache.h"
#include "engineprivate.h"
#include "ftp/ftpcontrolsocket.h"
#include "http/httpcontrolsocket.h"
#include "logging_private.h"
#include "pathcache.h"
#include "ratelimiter.h"
#include "sftp/sftpcontrolsocket.h"

#include <libfilezilla/event_loop.hpp>

#include <algorithm>

fz::mutex CFileZillaEnginePrivate::mutex_;
std::vector<CFileZillaEnginePrivate*> CFileZillaEnginePrivate::m_engineList;
std::atomic_int CFileZillaEnginePrivate::m_activeStatus[2] = {{0}, {0}};
std::list<CFileZillaEnginePrivate::t_failedLogins> CFileZillaEnginePrivate::m_failedLogins;

namespace {
unsigned int get_next_engine_id(fz::mutex& mutex)
{
	fz::scoped_lock lock(mutex);
	static unsigned int id = 0;
	return ++id;
}
}

CFileZillaEnginePrivate::CFileZillaEnginePrivate(CFileZillaEngineContext& context, CFileZillaEngine& parent, EngineNotificationHandler& notificationHandler)
	: event_handler(context.GetEventLoop())
	, transfer_status_(*this)
	, notification_handler_(notificationHandler)
	, m_engine_id(get_next_engine_id(mutex_))
	, m_options(context.GetOptions())
	, m_rateLimiter(context.GetRateLimiter())
	, directory_cache_(context.GetDirectoryCache())
	, path_cache_(context.GetPathCache())
	, parent_(parent)
	, thread_pool_(context.GetThreadPool())
	, encoding_converter_(context.GetCustomEncodingConverter())
{
	m_engineList.push_back(this);

	m_pLogging = new CLogging(*this);

	{
		bool queue_logs = ShouldQueueLogsFromOptions();
		fz::scoped_lock lock(notification_mutex_);
		queue_logs_ = queue_logs;
	}

	RegisterOption(OPTION_LOGGING_SHOW_DETAILED_LOGS);
	RegisterOption(OPTION_LOGGING_DEBUGLEVEL);
	RegisterOption(OPTION_LOGGING_RAWLISTING);
}

bool CFileZillaEnginePrivate::ShouldQueueLogsFromOptions() const
{
	return
		m_options.GetOptionVal(OPTION_LOGGING_RAWLISTING) == 0 &&
		m_options.GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 0 &&
		m_options.GetOptionVal(OPTION_LOGGING_SHOW_DETAILED_LOGS) == 0;
}

CFileZillaEnginePrivate::~CFileZillaEnginePrivate()
{
	remove_handler();
	m_maySendNotificationEvent = false;

	m_pControlSocket.reset();
	m_pCurrentCommand.reset();

	// Delete notification list
	for (auto & notification : m_NotificationList) {
		delete notification;
	}

	// Remove ourself from the engine list
	m_engineList.erase(std::remove(m_engineList.begin(), m_engineList.end(), this), m_engineList.end());
	for (auto iter = m_engineList.begin(); iter != m_engineList.end(); ++iter) {
		if (*iter == this) {
			m_engineList.erase(iter);
			break;
		}
	}

	delete m_pLogging;

	if (m_engineList.empty()) {
		CSocket::Cleanup(true);
	}
}

void CFileZillaEnginePrivate::OnEngineEvent(EngineNotificationType type)
{
	switch (type)
	{
	case engineCancel:
		DoCancel();
		break;
	default:
		break;
	}
}

bool CFileZillaEnginePrivate::IsBusy() const
{
	fz::scoped_lock lock(mutex_);
	return m_pCurrentCommand != 0;
}

bool CFileZillaEnginePrivate::IsConnected() const
{
	fz::scoped_lock lock(mutex_);
	if (!m_pControlSocket) {
		return false;
	}

	return m_pControlSocket->Connected();
}

const CCommand *CFileZillaEnginePrivate::GetCurrentCommand() const
{
	fz::scoped_lock lock(mutex_);
	return m_pCurrentCommand.get();
}

Command CFileZillaEnginePrivate::GetCurrentCommandId() const
{
	fz::scoped_lock lock(mutex_);
	if (!m_pCurrentCommand) {
		return Command::none;
	}
	else {
		return GetCurrentCommand()->GetId();
	}
}

void CFileZillaEnginePrivate::AddNotification(fz::scoped_lock& lock, CNotification *pNotification)
{
	m_NotificationList.push_back(pNotification);

	if (m_maySendNotificationEvent) {
		m_maySendNotificationEvent = false;
		lock.unlock();
		notification_handler_.OnEngineEvent(&parent_);
	}
}

void CFileZillaEnginePrivate::AddNotification(CNotification *pNotification)
{
	fz::scoped_lock lock(notification_mutex_);
	AddNotification(lock, pNotification);
}

void CFileZillaEnginePrivate::AddLogNotification(CLogmsgNotification *pNotification)
{
	fz::scoped_lock lock(notification_mutex_);

	if (pNotification->msgType == MessageType::Error) {
		queue_logs_ = false;

		m_NotificationList.insert(m_NotificationList.end(), queued_logs_.begin(), queued_logs_.end());
		queued_logs_.clear();
		AddNotification(lock, pNotification);
	}
	else if (pNotification->msgType == MessageType::Status) {
		ClearQueuedLogs(lock, false);
		AddNotification(lock, pNotification);
	}
	else if (!queue_logs_) {
		AddNotification(lock, pNotification);
	}
	else {
		queued_logs_.push_back(pNotification);
	}
}

void CFileZillaEnginePrivate::SendQueuedLogs(bool reset_flag)
{
	{
		fz::scoped_lock lock(notification_mutex_);
		m_NotificationList.insert(m_NotificationList.end(), queued_logs_.begin(), queued_logs_.end());
		queued_logs_.clear();

		if (reset_flag) {
			queue_logs_ = ShouldQueueLogsFromOptions();
		}

		if (!m_maySendNotificationEvent || m_NotificationList.empty()) {
			return;
		}
		m_maySendNotificationEvent = false;
	}

	notification_handler_.OnEngineEvent(&parent_);
}

void CFileZillaEnginePrivate::ClearQueuedLogs(fz::scoped_lock&, bool reset_flag)
{
	for (auto msg : queued_logs_) {
		delete msg;
	}
	queued_logs_.clear();

	if (reset_flag) {
		queue_logs_ = ShouldQueueLogsFromOptions();
	}
}

void CFileZillaEnginePrivate::ClearQueuedLogs(bool reset_flag)
{
	fz::scoped_lock lock(notification_mutex_);
	ClearQueuedLogs(lock, reset_flag);
}

int CFileZillaEnginePrivate::ResetOperation(int nErrorCode)
{
	fz::scoped_lock lock(mutex_);
	m_pLogging->LogMessage(MessageType::Debug_Debug, L"CFileZillaEnginePrivate::ResetOperation(%d)", nErrorCode);

	if (m_pCurrentCommand) {
		if ((nErrorCode & FZ_REPLY_NOTSUPPORTED) == FZ_REPLY_NOTSUPPORTED) {
			m_pLogging->LogMessage(MessageType::Error, _("Command not supported by this protocol"));
		}

		if (m_pCurrentCommand->GetId() == Command::connect) {
			if (!(nErrorCode & ~(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED | FZ_REPLY_TIMEOUT | FZ_REPLY_CRITICALERROR | FZ_REPLY_PASSWORDFAILED)) &&
				nErrorCode & (FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED))
			{
				CConnectCommand const& connectCommand = static_cast<CConnectCommand const&>(*m_pCurrentCommand.get());

				RegisterFailedLoginAttempt(connectCommand.GetServer(), (nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR);

				if ((nErrorCode & FZ_REPLY_CRITICALERROR) != FZ_REPLY_CRITICALERROR) {
					++m_retryCount;
					if (m_retryCount < m_options.GetOptionVal(OPTION_RECONNECTCOUNT) && connectCommand.RetryConnecting()) {
						fz::duration delay = GetRemainingReconnectDelay(connectCommand.GetServer());
						if (!delay) {
							delay = fz::duration::from_seconds(1);
						}
						m_pLogging->LogMessage(MessageType::Status, _("Waiting to retry..."));
						stop_timer(m_retryTimer);
						m_retryTimer = add_timer(delay, true);
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
		else {
			m_nControlSocketError |= nErrorCode;
		}

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

	if (nErrorCode != FZ_REPLY_OK) {
		SendQueuedLogs(true);
	}
	else {
		ClearQueuedLogs(true);
	}

	return nErrorCode;
}

unsigned int CFileZillaEnginePrivate::GetNextAsyncRequestNumber()
{
	fz::scoped_lock lock(notification_mutex_);
	return ++m_asyncRequestCounter;
}

// Command handlers
int CFileZillaEnginePrivate::Connect(CConnectCommand const& command)
{
	if (IsConnected()) {
		return FZ_REPLY_ALREADYCONNECTED;
	}

	m_retryCount = 0;

	// Need to delete before setting m_pCurrentCommand.
	// The destructor can call CFileZillaEnginePrivate::ResetOperation
	// which would delete m_pCurrentCommand
	m_pControlSocket.reset();
	m_nControlSocketError = 0;

	if (command.GetServer().GetPort() != CServer::GetDefaultPort(command.GetServer().GetProtocol())) {
		ServerProtocol protocol = CServer::GetProtocolFromPort(command.GetServer().GetPort(), true);
		if (protocol != UNKNOWN && protocol != command.GetServer().GetProtocol()) {
			m_pLogging->LogMessage(MessageType::Status, _("Selected port usually in use by a different protocol."));
		}
	}

	return ContinueConnect();
}

int CFileZillaEnginePrivate::Disconnect(CDisconnectCommand const&)
{
	int res = FZ_REPLY_OK;
	if (m_pControlSocket) {
		res = m_pControlSocket->Disconnect();
		m_pControlSocket.reset();
	}

	return res;
}

int CFileZillaEnginePrivate::List(CListCommand const& command)
{
	int flags = command.GetFlags();
	bool const refresh = (command.GetFlags() & LIST_FLAG_REFRESH) != 0;
	bool const avoid = (command.GetFlags() & LIST_FLAG_AVOID) != 0;

	if (!refresh && !command.GetPath().empty()) {
		CServer const& server =m_pControlSocket->GetCurrentServer();
		if (server) {
			CServerPath path(path_cache_.Lookup(server, command.GetPath(), command.GetSubDir()));
			if (path.empty() && command.GetSubDir().empty()) {
				path = command.GetPath();
			}
			if (!path.empty()) {
				CDirectoryListing *pListing = new CDirectoryListing;
				bool is_outdated = false;
				bool found = directory_cache_.Lookup(*pListing, server, path, true, is_outdated);
				if (found && !is_outdated) {
					if (pListing->get_unsure_flags()) {
						flags |= LIST_FLAG_REFRESH;
					}
					else {
						if (!avoid) {
							CDirectoryListingNotification *pNotification = new CDirectoryListingNotification(pListing->path);
							AddNotification(pNotification);
						}
						delete pListing;
						return FZ_REPLY_OK;
					}
				}
				if (is_outdated) {
					flags |= LIST_FLAG_REFRESH;
				}
				delete pListing;
			}
		}
	}

	m_pControlSocket->List(command.GetPath(), command.GetSubDir(), flags);
	return FZ_REPLY_CONTINUE;
}

int CFileZillaEnginePrivate::FileTransfer(CFileTransferCommand const& command)
{
	m_pControlSocket->FileTransfer(command.GetLocalFile(), command.GetRemotePath(), command.GetRemoteFile(), command.Download(), command.GetTransferSettings());
	return FZ_REPLY_CONTINUE;
}

int CFileZillaEnginePrivate::RawCommand(CRawCommand const& command)
{
	{
		fz::scoped_lock lock(notification_mutex_);
		queue_logs_ = false;
	}
	m_pControlSocket->RawCommand(command.GetCommand());
	return FZ_REPLY_CONTINUE;
}

int CFileZillaEnginePrivate::Delete(CDeleteCommand& command)
{
	if (command.GetFiles().size() == 1) {
		m_pLogging->LogMessage(MessageType::Status, _("Deleting \"%s\""), command.GetPath().FormatFilename(command.GetFiles().front()));
	}
	else {
		m_pLogging->LogMessage(MessageType::Status, _("Deleting %u files from \"%s\""), static_cast<unsigned int>(command.GetFiles().size()), command.GetPath().GetPath());
	}
	m_pControlSocket->Delete(command.GetPath(), command.ExtractFiles());
	return FZ_REPLY_CONTINUE;
}

int CFileZillaEnginePrivate::RemoveDir(CRemoveDirCommand const& command)
{
	m_pControlSocket->RemoveDir(command.GetPath(), command.GetSubDir());
	return FZ_REPLY_CONTINUE;
}

int CFileZillaEnginePrivate::Mkdir(CMkdirCommand const& command)
{
	m_pControlSocket->Mkdir(command.GetPath());
	return FZ_REPLY_CONTINUE;
}

int CFileZillaEnginePrivate::Rename(CRenameCommand const& command)
{
	m_pControlSocket->Rename(command);
	return FZ_REPLY_CONTINUE;
}

int CFileZillaEnginePrivate::Chmod(CChmodCommand const& command)
{
	m_pControlSocket->Chmod(command);
	return FZ_REPLY_CONTINUE;
}

void CFileZillaEnginePrivate::RegisterFailedLoginAttempt(const CServer& server, bool critical)
{
	fz::scoped_lock lock(mutex_);
	std::list<t_failedLogins>::iterator iter = m_failedLogins.begin();
	while (iter != m_failedLogins.end()) {
		fz::duration const span = fz::monotonic_clock::now() - iter->time;
		if (span.get_seconds() >= m_options.GetOptionVal(OPTION_RECONNECTDELAY) ||
			iter->server == server || (!critical && (iter->server.GetHost() == server.GetHost() && iter->server.GetPort() == server.GetPort())))
		{
			std::list<t_failedLogins>::iterator prev = iter;
			++iter;
			m_failedLogins.erase(prev);
		}
		else {
			++iter;
		}
	}

	t_failedLogins failure;
	failure.server = server;
	failure.time = fz::monotonic_clock::now();
	failure.critical = critical;
	m_failedLogins.push_back(failure);
}

fz::duration CFileZillaEnginePrivate::GetRemainingReconnectDelay(CServer const& server)
{
	fz::scoped_lock lock(mutex_);
	std::list<t_failedLogins>::iterator iter = m_failedLogins.begin();
	while (iter != m_failedLogins.end()) {
		fz::duration const span = fz::monotonic_clock::now() - iter->time;
		fz::duration const delay = fz::duration::from_seconds(m_options.GetOptionVal(OPTION_RECONNECTDELAY));
		if (span >= delay) {
			std::list<t_failedLogins>::iterator prev = iter;
			++iter;
			m_failedLogins.erase(prev);
		}
		else if (!iter->critical && iter->server.GetHost() == server.GetHost() && iter->server.GetPort() == server.GetPort()) {
			return delay - span;
		}
		else if (iter->server == server) {
			return delay - span;
		}
		else {
			++iter;
		}
	}

	return fz::duration();
}

void CFileZillaEnginePrivate::OnTimer(fz::timer_id)
{
	if (!m_retryTimer) {
		return;
	}
	m_retryTimer = 0;

	if (!m_pCurrentCommand || m_pCurrentCommand->GetId() != Command::connect) {
		m_pLogging->LogMessage(MessageType::Debug_Warning, L"CFileZillaEnginePrivate::OnTimer called without pending Command::connect");
		return;
	}

	m_pControlSocket.reset();

	int res = ContinueConnect();
	res |= m_nControlSocketError;
	m_nControlSocketError = 0;

	if (res == FZ_REPLY_CONTINUE) {
		assert(m_pControlSocket);
		m_pControlSocket->SendNextCommand();
	}
	else if (res != FZ_REPLY_WOULDBLOCK) {
		ResetOperation(res);
	}
}

int CFileZillaEnginePrivate::ContinueConnect()
{
	fz::scoped_lock lock(mutex_);

	if (!m_pCurrentCommand || m_pCurrentCommand->GetId() != Command::connect) {
		m_pLogging->LogMessage(MessageType::Debug_Warning, L"CFileZillaEnginePrivate::ContinueConnect called without pending Command::connect");
		return ResetOperation(FZ_REPLY_INTERNALERROR);
	}

	const CConnectCommand *pConnectCommand = static_cast<CConnectCommand *>(m_pCurrentCommand.get());
	const CServer& server = pConnectCommand->GetServer();
	fz::duration const& delay = GetRemainingReconnectDelay(server);
	if (delay) {
		m_pLogging->LogMessage(MessageType::Status, fztranslate("Delaying connection for %d second due to previously failed connection attempt...", "Delaying connection for %d seconds due to previously failed connection attempt...", (delay.get_milliseconds() + 999) / 1000), (delay.get_milliseconds() + 999) / 1000);
		stop_timer(m_retryTimer);
		m_retryTimer = add_timer(delay, true);
		return FZ_REPLY_WOULDBLOCK;
	}

	switch (server.GetProtocol())
	{
	case FTP:
	case FTPS:
	case FTPES:
	case INSECURE_FTP:
		m_pControlSocket = std::make_unique<CFtpControlSocket>(*this);
		break;
	case SFTP:
		m_pControlSocket = std::make_unique<CSftpControlSocket>(*this);
		break;
	case HTTP:
	case HTTPS:
		m_pControlSocket = std::make_unique<CHttpControlSocket>(*this);
		break;
	default:
		m_pLogging->LogMessage(MessageType::Debug_Warning, L"Not a valid protocol: %d", server.GetProtocol());
		return FZ_REPLY_SYNTAXERROR|FZ_REPLY_DISCONNECTED;
	}

	m_pControlSocket->Connect(server);
	return FZ_REPLY_CONTINUE;
}

void CFileZillaEnginePrivate::InvalidateCurrentWorkingDirs(const CServerPath& path)
{
	fz::scoped_lock lock(mutex_);

	assert(m_pControlSocket);
	CServer const& ownServer  = m_pControlSocket->GetCurrentServer();
	assert(ownServer);

	for (auto & engine : m_engineList) {
		if (!engine || engine == this) {
			continue;
		}

		if (!engine->m_pControlSocket) {
			continue;
		}

		CServer const& server = engine->m_pControlSocket->GetCurrentServer();
		if (server != ownServer) {
			continue;
		}

		engine->m_pControlSocket->InvalidateCurrentWorkingDir(path);
	}
}

void CFileZillaEnginePrivate::operator()(fz::event_base const& ev)
{
	fz::scoped_lock lock(mutex_);

	fz::dispatch<CFileZillaEngineEvent, CCommandEvent, CAsyncRequestReplyEvent, fz::timer_event>(ev, this,
		&CFileZillaEnginePrivate::OnEngineEvent,
		&CFileZillaEnginePrivate::OnCommandEvent,
		&CFileZillaEnginePrivate::OnSetAsyncRequestReplyEvent,
		&CFileZillaEnginePrivate::OnTimer
		);
}

int CFileZillaEnginePrivate::CheckCommandPreconditions(CCommand const& command, bool checkBusy)
{
	if (!command.valid()) {
		return FZ_REPLY_SYNTAXERROR;
	}
	else if (checkBusy && IsBusy()) {
		return FZ_REPLY_BUSY;
	}
	else if (command.GetId() != Command::connect && command.GetId() != Command::disconnect && !IsConnected()) {
		return FZ_REPLY_NOTCONNECTED;
	}
	else if (command.GetId() == Command::connect && m_pControlSocket) {
		return FZ_REPLY_ALREADYCONNECTED;
	}
	return FZ_REPLY_OK;
}

void CFileZillaEnginePrivate::OnCommandEvent()
{
	fz::scoped_lock lock(mutex_);

	if (m_pCurrentCommand) {
		CCommand & command = *m_pCurrentCommand;
		Command id = command.GetId();

		int res = CheckCommandPreconditions(command, false);
		if (res == FZ_REPLY_OK) {
			switch (command.GetId())
			{
			case Command::connect:
				res = Connect(static_cast<CConnectCommand const&>(command));
				break;
			case Command::disconnect:
				res = Disconnect(static_cast<CDisconnectCommand const&>(command));
				break;
			case Command::list:
				res = List(static_cast<CListCommand const&>(command));
				break;
			case Command::transfer:
				res = FileTransfer(static_cast<CFileTransferCommand const&>(command));
				break;
			case Command::raw:
				res = RawCommand(static_cast<CRawCommand const&>(command));
				break;
			case Command::del:
				res = Delete(static_cast<CDeleteCommand &>(command));
				break;
			case Command::removedir:
				res = RemoveDir(static_cast<CRemoveDirCommand const&>(command));
				break;
			case Command::mkdir:
				res = Mkdir(static_cast<CMkdirCommand const&>(command));
				break;
			case Command::rename:
				res = Rename(static_cast<CRenameCommand const&>(command));
				break;
			case Command::chmod:
				res = Chmod(static_cast<CChmodCommand const&>(command));
				break;
			default:
				res = FZ_REPLY_SYNTAXERROR;
			}
		}

		if (id != Command::disconnect) {
			res |= m_nControlSocketError;
		}
		else if (res & FZ_REPLY_DISCONNECTED) {
			res = FZ_REPLY_OK;
		}
		m_nControlSocketError = 0;

		if (res == FZ_REPLY_CONTINUE) {
			assert(m_pControlSocket);
			m_pControlSocket->SendNextCommand();
		}
		else if (res != FZ_REPLY_WOULDBLOCK) {
			ResetOperation(res);
		}
	}
}

void CFileZillaEnginePrivate::DoCancel()
{
	fz::scoped_lock lock(mutex_);
	if (!IsBusy()) {
		return;
	}

	if (m_retryTimer) {
		assert(m_pCurrentCommand && m_pCurrentCommand->GetId() == Command::connect);

		m_pControlSocket.reset();

		m_pCurrentCommand.reset();

		stop_timer(m_retryTimer);
		m_retryTimer = 0;

		m_pLogging->LogMessage(MessageType::Error, _("Connection attempt interrupted by user"));
		COperationNotification *notification = new COperationNotification();
		notification->nReplyCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_CANCELED;
		notification->commandId = Command::connect;
		AddNotification(notification);

		ClearQueuedLogs(true);
	}
	else {
		if (m_pControlSocket) {
			m_pControlSocket->Cancel();
		}
		else {
			ResetOperation(FZ_REPLY_CANCELED);
		}
	}
}

bool CFileZillaEnginePrivate::CheckAsyncRequestReplyPreconditions(std::unique_ptr<CAsyncRequestNotification> const& reply)
{
	if (!reply) {
		return false;
	}
	if (!IsBusy()) {
		return false;
	}

	bool match;
	{
		fz::scoped_lock l(notification_mutex_);
		match = reply->requestNumber == m_asyncRequestCounter;
	}

	return match && m_pControlSocket;
}

void CFileZillaEnginePrivate::OnSetAsyncRequestReplyEvent(std::unique_ptr<CAsyncRequestNotification> const& reply)
{
	fz::scoped_lock lock(mutex_);
	if (!CheckAsyncRequestReplyPreconditions(reply)) {
		return;
	}

	m_pControlSocket->SetAlive();
	m_pControlSocket->SetAsyncRequestReply(reply.get());
}

int CFileZillaEnginePrivate::Execute(const CCommand &command)
{
	fz::scoped_lock lock(mutex_);

	int res = CheckCommandPreconditions(command, true);
	if (res != FZ_REPLY_OK) {
		return res;
	}

	m_pCurrentCommand.reset(command.Clone());
	send_event<CCommandEvent>();

	return FZ_REPLY_WOULDBLOCK;
}

std::unique_ptr<CNotification> CFileZillaEnginePrivate::GetNextNotification()
{
	fz::scoped_lock lock(notification_mutex_);

	if (m_NotificationList.empty()) {
		m_maySendNotificationEvent = true;
		return 0;
	}
	std::unique_ptr<CNotification> pNotification(m_NotificationList.front());
	m_NotificationList.pop_front();

	return pNotification;
}

bool CFileZillaEnginePrivate::SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification)
{
	fz::scoped_lock lock(mutex_);
	if (!CheckAsyncRequestReplyPreconditions(pNotification)) {
		return false;
	}

	send_event<CAsyncRequestReplyEvent>(std::move(pNotification));

	return true;
}

bool CFileZillaEnginePrivate::IsPendingAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> const& pNotification)
{
	if (!pNotification) {
		return false;
	}

	if (!IsBusy()) {
		return false;
	}

	fz::scoped_lock lock(notification_mutex_);
	return pNotification->requestNumber == m_asyncRequestCounter;
}

void CFileZillaEnginePrivate::SetActive(int direction)
{
	int const old_status = m_activeStatus[direction].fetch_or(0x1);
	if (!old_status) {
		AddNotification(new CActiveNotification(direction));
	}
}

bool CFileZillaEnginePrivate::IsActive(CFileZillaEngine::_direction direction)
{
	int const old = m_activeStatus[direction].exchange(0x2);
	if (!(old & 0x1)) {
		// Race: We might lose updates between the first exchange and this assignment.
		// It is harmless though
		m_activeStatus[direction] = 0;
		return false;
	}
	return true;
}

CTransferStatus CFileZillaEnginePrivate::GetTransferStatus(bool &changed)
{
	return transfer_status_.Get(changed);
}

int CFileZillaEnginePrivate::CacheLookup(const CServerPath& path, CDirectoryListing& listing)
{
	// TODO: Possible optimization: Atomically get current server. The cache has its own mutex.
	fz::scoped_lock lock(mutex_);

	if (!IsConnected()) {
		return FZ_REPLY_ERROR;
	}

	assert(m_pControlSocket->GetCurrentServer());

	bool is_outdated = false;
	if (!directory_cache_.Lookup(listing, m_pControlSocket->GetCurrentServer(), path, true, is_outdated)) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_OK;
}

int CFileZillaEnginePrivate::Cancel()
{
	fz::scoped_lock lock(mutex_);
	if (!IsBusy()) {
		return FZ_REPLY_OK;
	}

	send_event<CFileZillaEngineEvent>(engineCancel);
	return FZ_REPLY_WOULDBLOCK;
}

void CFileZillaEnginePrivate::OnOptionsChanged(changed_options_t const&)
{
	bool queue_logs = ShouldQueueLogsFromOptions();
	if (queue_logs) {
		fz::scoped_lock lock(notification_mutex_);
		queue_logs_ = true;
	}
	else {
		SendQueuedLogs(true);
	}
}


CTransferStatusManager::CTransferStatusManager(CFileZillaEnginePrivate& engine)
	: engine_(engine)
{
}

void CTransferStatusManager::Reset()
{
	{
		fz::scoped_lock lock(mutex_);
		status_.clear();
		send_state_ = 0;
	}

	engine_.AddNotification(new CTransferStatusNotification());
}

void CTransferStatusManager::Init(int64_t totalSize, int64_t startOffset, bool list)
{
	fz::scoped_lock lock(mutex_);
	if (startOffset < 0)
		startOffset = 0;

	status_ = CTransferStatus(totalSize, startOffset, list);
	currentOffset_ = 0;
}

void CTransferStatusManager::SetStartTime()
{
	fz::scoped_lock lock(mutex_);
	if (!status_)
		return;

	status_.started = fz::datetime::now();
}

void CTransferStatusManager::SetMadeProgress()
{
	fz::scoped_lock lock(mutex_);
	if (!status_)
		return;

	status_.madeProgress = true;
}

void CTransferStatusManager::Update(int64_t transferredBytes)
{
	CNotification* notification = 0;

	{
		int64_t oldOffset = currentOffset_.fetch_add(transferredBytes);
		if (!oldOffset) {
			fz::scoped_lock lock(mutex_);
			if (!status_) {
				return;
			}

			if (!send_state_) {
				status_.currentOffset += currentOffset_.exchange(0);
				notification = new CTransferStatusNotification(status_);
			}
			send_state_ = 2;
		}
	}

	if (notification) {
		engine_.AddNotification(notification);
	}
}

CTransferStatus CTransferStatusManager::Get(bool &changed)
{
	fz::scoped_lock lock(mutex_);
	if (!status_) {
		changed = false;
		send_state_ = 0;
	}
	else {
		status_.currentOffset += currentOffset_.exchange(0);
		if (send_state_ == 2) {
			changed = true;
			send_state_ = 1;
		}
		else {
			changed = false;
			send_state_ = 0;
		}
	}
	return status_;
}

bool CTransferStatusManager::empty()
{
	fz::scoped_lock lock(mutex_);
	return status_.empty();
}
