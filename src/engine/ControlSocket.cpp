#include <filezilla.h>
#include "ControlSocket.h"
#include "directorycache.h"
#include "engineprivate.h"
#include "local_path.h"
#include "logging_private.h"
#include "proxy.h"
#include "servercapabilities.h"
#include "sizeformatting_base.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>

#include <string.h>

#ifndef FZ_WINDOWS
	#include <sys/stat.h>

	#define mutex mutex_override // Sadly on some platforms system headers include conflicting names
	#include <netdb.h>
	#undef mutex
	#ifndef AI_IDN
		#include <idna.h>
		extern "C" {
			#include <idn-free.h>
		}
	#endif
#endif

struct obtain_lock_event_type;
typedef fz::simple_event<obtain_lock_event_type> CObtainLockEvent;

std::list<CControlSocket::t_lockInfo> CControlSocket::m_lockInfoList;

CControlSocket::CControlSocket(CFileZillaEnginePrivate & engine)
	: CLogging(engine)
	, event_handler(engine.event_loop_)
	, engine_(engine)
{
}

CControlSocket::~CControlSocket()
{
	remove_handler();

	DoClose();
}

int CControlSocket::Disconnect()
{
	LogMessage(MessageType::Status, _("Disconnected from server"));

	DoClose();
	return FZ_REPLY_OK;
}

Command CControlSocket::GetCurrentCommandId() const
{
	if (!operations_.empty()) {
		return operations_.back()->opId;
	}

	return engine_.GetCurrentCommandId();
}

void CControlSocket::LogTransferResultMessage(int nErrorCode, CFileTransferOpData *pData)
{
	bool tmp{};

	CTransferStatus const status = engine_.transfer_status_.Get(tmp);
	if (!status.empty() && (nErrorCode == FZ_REPLY_OK || status.madeProgress)) {
		int elapsed = static_cast<int>((fz::datetime::now() - status.started).get_seconds());
		if (elapsed <= 0) {
			elapsed = 1;
		}
		std::wstring time = fz::sprintf(fztranslate("%d second", "%d seconds", elapsed), elapsed);

		int64_t transferred = status.currentOffset - status.startOffset;
		std::wstring size = CSizeFormatBase::Format(&engine_.GetOptions(), transferred, true);

		MessageType msgType = MessageType::Error;
		std::wstring msg;
		if (nErrorCode == FZ_REPLY_OK) {
			msgType = MessageType::Status;
			msg = _("File transfer successful, transferred %s in %s");
		}
		else if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
			msg = _("File transfer aborted by user after transferring %s in %s");
		}
		else if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR) {
			msg = _("Critical file transfer error after transferring %s in %s");
		}
		else {
			msg = _("File transfer failed after transferring %s in %s");
		}
		LogMessage(msgType, msg, size, time);
	}
	else {
		if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
			LogMessage(MessageType::Error, _("File transfer aborted by user"));
		}
		else if (nErrorCode == FZ_REPLY_OK) {
			if (pData->transferInitiated_) {
				LogMessage(MessageType::Status, _("File transfer successful"));
			}
			else {
				LogMessage(MessageType::Status, _("File transfer skipped"));
			}
		}
		else if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR) {
			LogMessage(MessageType::Error, _("Critical file transfer error"));
		}
		else {
			LogMessage(MessageType::Error, _("File transfer failed"));
		}
	}
}

void CControlSocket::Push(std::unique_ptr<COpData> && operation)
{
	operations_.emplace_back(std::move(operation));
}

int CControlSocket::ResetOperation(int nErrorCode)
{
	LogMessage(MessageType::Debug_Verbose, L"CControlSocket::ResetOperation(%d)", nErrorCode);

	if (nErrorCode & FZ_REPLY_WOULDBLOCK) {
		LogMessage(MessageType::Debug_Warning, L"ResetOperation with FZ_REPLY_WOULDBLOCK in nErrorCode (%d)", nErrorCode);
	}

	std::unique_ptr<COpData> oldOperation;
	if (!operations_.empty()) {
		if (operations_.back()->holdsLock_) {
			UnlockCache();
		}
		oldOperation = std::move(operations_.back());
		operations_.pop_back();		
	}
	if (!operations_.empty()) {
		int ret;
		if (nErrorCode == FZ_REPLY_OK ||
			nErrorCode == FZ_REPLY_ERROR ||
			nErrorCode == FZ_REPLY_CRITICALERROR)
		{
			ret = ParseSubcommandResult(nErrorCode, *oldOperation);
		}
		else {
			ret = ResetOperation(nErrorCode);
		}
		return ret;
	}

	std::wstring prefix;
	if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR &&
		(!oldOperation || oldOperation->opId != Command::transfer))
	{
		prefix = _("Critical error:") + L" ";
	}

	if (oldOperation) {
		const Command commandId = oldOperation->opId;
		switch (commandId)
		{
		case Command::none:
			if (!prefix.empty()) {
				LogMessage(MessageType::Error, _("Critical error"));
			}
			break;
		case Command::connect:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				LogMessage(MessageType::Error, prefix + _("Connection attempt interrupted by user"));
			}
			else if (nErrorCode != FZ_REPLY_OK) {
				LogMessage(MessageType::Error, prefix + _("Could not connect to server"));
			}
			break;
		case Command::list:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				LogMessage(MessageType::Error, prefix + _("Directory listing aborted by user"));
			}
			else if (nErrorCode != FZ_REPLY_OK) {
				LogMessage(MessageType::Error, prefix + _("Failed to retrieve directory listing"));
			}
			else {
				if (currentPath_.empty()) {
					LogMessage(MessageType::Status, _("Directory listing successful"));
				}
				else {
					LogMessage(MessageType::Status, _("Directory listing of \"%s\" successful"), currentPath_.GetPath());
				}
			}
			break;
		case Command::transfer:
			{
				auto & data = static_cast<CFileTransferOpData &>(*oldOperation);
				if (!data.download_ && data.transferInitiated_) {
					if (!currentServer_) {
						LogMessage(MessageType::Debug_Warning, L"currentServer_ is empty");
					}
					else {
						bool updated = engine_.GetDirectoryCache().UpdateFile(currentServer_, data.remotePath_, data.remoteFile_, true, CDirectoryCache::file, (nErrorCode == FZ_REPLY_OK) ? data.localFileSize_ : -1);
						if (updated) {
							SendDirectoryListingNotification(data.remotePath_, false, false);
						}
					}
				}
				LogTransferResultMessage(nErrorCode, &data);
			}
			break;
		default:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				LogMessage(MessageType::Error, prefix + _("Interrupted by user"));
			}
			break;
		}
	}

	engine_.transfer_status_.Reset();

	SetWait(false);

	if (m_invalidateCurrentPath) {
		currentPath_.clear();
		m_invalidateCurrentPath = false;
	}

	return engine_.ResetOperation(nErrorCode);
}

int CControlSocket::DoClose(int nErrorCode)
{
	LogMessage(MessageType::Debug_Debug, L"CControlSocket::DoClose(%d)", nErrorCode);
	if (m_closed) {
		assert(operations_.empty());
		return nErrorCode;
	}

	m_closed = true;

	nErrorCode = ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED | nErrorCode);

	currentServer_.clear();

	return nErrorCode;
}

std::wstring CControlSocket::ConvertDomainName(std::wstring const& domain)
{
#ifdef FZ_WINDOWS
	int len = IdnToAscii(IDN_ALLOW_UNASSIGNED, domain.c_str(), domain.size() + 1, 0, 0);
	if (!len) {
		LogMessage(MessageType::Debug_Warning, L"Could not convert domain name");
		return domain;
	}

	wchar_t* output = new wchar_t[len];
	int res = IdnToAscii(IDN_ALLOW_UNASSIGNED, domain.c_str(), domain.size() + 1, output, len);
	if (!res) {
		delete [] output;
		LogMessage(MessageType::Debug_Warning, L"Could not convert domain name");
		return domain;
	}

	std::wstring ret(output);
	delete [] output;
	return ret;
#elif defined(AI_IDN)
	return domain;
#else
	std::string const utf8 = fz::to_utf8(domain);

	char *output = 0;
	if (idna_to_ascii_8z(utf8.c_str(), &output, IDNA_ALLOW_UNASSIGNED)) {
		LogMessage(MessageType::Debug_Warning, L"Could not convert domain name");
		return domain;
	}

	std::wstring result = fz::to_wstring(std::string(output));
	idn_free(output);
	return result;
#endif
}

void CControlSocket::Cancel()
{
	if (GetCurrentCommandId() != Command::none) {
		if (GetCurrentCommandId() == Command::connect) {
			DoClose(FZ_REPLY_CANCELED);
		}
		else {
			ResetOperation(FZ_REPLY_CANCELED);
		}
	}
}

CServer const& CControlSocket::GetCurrentServer() const
{
	return currentServer_;
}

bool CControlSocket::ParsePwdReply(std::wstring reply, bool unquoted, CServerPath const& defaultPath)
{
	if (!unquoted) {
		size_t pos1 = reply.find('"');
		size_t pos2 = reply.rfind('"');
		// Due to searching the same character, pos1 is npos iff pos2 is npos

		if (pos1 == std::wstring::npos || pos1 >= pos2) {
			pos1 = reply.find('\'');
			pos2 = reply.rfind('\'');

			if (pos1 != std::wstring::npos && pos1 < pos2) {
				LogMessage(MessageType::Debug_Info, L"Broken server sending single-quoted path instead of double-quoted path.");
			}
		}
		if (pos1 == std::wstring::npos || pos1 >= pos2) {
			LogMessage(MessageType::Debug_Info, L"Broken server, no quoted path found in pwd reply, trying first token as path");
			pos1 = reply.find(' ');
			if (pos1 != std::wstring::npos) {
				reply = reply.substr(pos1 + 1);
				pos2 = reply.find(' ');
				if (pos2 != std::wstring::npos)
					reply = reply.substr(0, pos2);
			}
			else {
				reply.clear();
			}
		}
		else {
			reply = reply.substr(pos1 + 1, pos2 - pos1 - 1);
			fz::replace_substrings(reply, L"\"\"", L"\"");
		}
	}

	currentPath_.SetType(currentServer_.GetType());
	if (reply.empty() || !currentPath_.SetPath(reply)) {
		if (reply.empty()) {
			LogMessage(MessageType::Error, _("Server returned empty path."));
		}
		else {
			LogMessage(MessageType::Error, _("Failed to parse returned path."));
		}

		if (!defaultPath.empty()) {
			LogMessage(MessageType::Debug_Warning, L"Assuming path is '%s'.", defaultPath.GetPath());
			currentPath_ = defaultPath;
			return true;
		}
		return false;
	}

	return true;
}

int CControlSocket::CheckOverwriteFile()
{
	if (operations_.empty() || operations_.back()->opId != Command::transfer) {
		LogMessage(MessageType::Debug_Info, L"CheckOverwriteFile called without active transfer.");
		return FZ_REPLY_INTERNALERROR;
	}

	auto & data = static_cast<CFileTransferOpData &>(*operations_.back());

	if (data.download_) {
		if (fz::local_filesys::get_file_type(fz::to_native(data.localFile_), true) != fz::local_filesys::file) {
			return FZ_REPLY_OK;
		}
	}

	CDirentry entry;
	bool dirDidExist;
	bool matchedCase;
	CServerPath remotePath;
	if (data.tryAbsolutePath_ || currentPath_.empty()) {
		remotePath = data.remotePath_;
	}
	else {
		remotePath = currentPath_;
	}
	bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, remotePath, data.remoteFile_, dirDidExist, matchedCase);

	// Ignore entries with wrong case
	if (found && !matchedCase)
		found = false;

	if (!data.download_) {
		if (!found && data.remoteFileSize_ < 0 && data.fileTime_.empty()) {
			return FZ_REPLY_OK;
		}
	}

	CFileExistsNotification *pNotification = new CFileExistsNotification;

	pNotification->download = data.download_;
	pNotification->localFile = data.localFile_;
	pNotification->remoteFile = data.remoteFile_;
	pNotification->remotePath = data.remotePath_;
	pNotification->localSize = data.localFileSize_;
	pNotification->remoteSize = data.remoteFileSize_;
	pNotification->remoteTime = data.fileTime_;
	pNotification->ascii = !data.transferSettings_.binary;

	if (data.download_ && pNotification->localSize >= 0) {
		pNotification->canResume = true;
	}
	else if (!data.download_ && pNotification->remoteSize >= 0) {
		pNotification->canResume = true;
	}
	else {
		pNotification->canResume = false;
	}

	pNotification->localTime = fz::local_filesys::get_modification_time(fz::to_native(data.localFile_));

	if (found) {
		if (pNotification->remoteTime.empty() && entry.has_date()) {
			pNotification->remoteTime = entry.time;
			data.fileTime_ = entry.time;
		}
	}

	SendAsyncRequest(pNotification);

	return FZ_REPLY_WOULDBLOCK;
}

CFileTransferOpData::CFileTransferOpData(bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path)
	: COpData(Command::transfer)
	, localFile_(local_file), remoteFile_(remote_file), remotePath_(remote_path)
	, download_(is_download)
{
}

std::wstring CControlSocket::ConvToLocal(char const* buffer, size_t len)
{
	std::wstring ret;

	if (!len) {
		return ret;
	}

	if (m_useUTF8) {
		ret = fz::to_wstring_from_utf8(buffer, len);
		if (!ret.empty()) {
			return ret;
		}
			
		if (currentServer_.GetEncodingType() != ENCODING_UTF8) {
			LogMessage(MessageType::Status, _("Invalid character sequence received, disabling UTF-8. Select UTF-8 option in site manager to force UTF-8."));
			m_useUTF8 = false;
		}
	}

	if (currentServer_.GetEncodingType() == ENCODING_CUSTOM) {
		ret = engine_.GetEncodingConverter().toLocal(currentServer_.GetCustomEncoding(), buffer, len);
		if (!ret.empty()) {
			return ret;
		}
	}

#ifdef FZ_WINDOWS
	// Only for Windows as other platforms should be UTF-8 anyhow.
	ret = fz::to_wstring(std::string(buffer, len));
	if (!ret.empty()) {
		return ret;
	}
#endif

	// Treat it as ISO8859-1
	ret.assign(reinterpret_cast<unsigned char const*>(buffer), reinterpret_cast<unsigned char const*>(buffer + len));

	return ret;
}

std::string CControlSocket::ConvToServer(std::wstring const& str, bool force_utf8)
{
	std::string ret;
	if (m_useUTF8 || force_utf8) {
		ret = fz::to_utf8(str);
		if (!ret.empty() || force_utf8) {
			return ret;
		}
	}

	if (currentServer_.GetEncodingType() == ENCODING_CUSTOM) {
		ret = engine_.GetEncodingConverter().toServer(currentServer_.GetCustomEncoding(), str.c_str(), str.size());
		if (!ret.empty()) {
			return ret;
		}
	}

	ret = fz::to_string(str);
	return ret;
}

void CControlSocket::OnTimer(fz::timer_id)
{
	m_timer = 0; // It's a one-shot timer, no need to stop it

	int const timeout = engine_.GetOptions().GetOptionVal(OPTION_TIMEOUT);
	if (timeout > 0) {
		fz::duration elapsed = fz::monotonic_clock::now() - m_lastActivity;

		if ((operations_.empty() || !operations_.back()->waitForAsyncRequest) && !IsWaitingForLock()) {
			if (elapsed > fz::duration::from_seconds(timeout)) {
				LogMessage(MessageType::Error, fztranslate("Connection timed out after %d second of inactivity", "Connection timed out after %d seconds of inactivity", timeout), timeout);
				DoClose(FZ_REPLY_TIMEOUT);
				return;
			}
		}
		else {
			elapsed = fz::duration();
		}

		m_timer = add_timer(fz::duration::from_milliseconds(timeout * 1000) - elapsed, true);
	}
}

void CControlSocket::SetAlive()
{
	m_lastActivity = fz::monotonic_clock::now();
}

void CControlSocket::SetWait(bool wait)
{
	if (wait) {
		if (m_timer) {
			return;
		}

		m_lastActivity = fz::monotonic_clock::now();

		int timeout = engine_.GetOptions().GetOptionVal(OPTION_TIMEOUT);
		if (!timeout) {
			return;
		}

		m_timer = add_timer(fz::duration::from_milliseconds(timeout * 1000 + 100), true); // Add a bit of slack
	}
	else {
		stop_timer(m_timer);
		m_timer = 0;
	}
}

int CControlSocket::SendNextCommand()
{
	LogMessage(MessageType::Debug_Verbose, L"CControlSocket::SendNextCommand()");
	if (operations_.empty()) {
		LogMessage(MessageType::Debug_Warning, L"SendNextCommand called without active operation");
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	while (!operations_.empty()) {
		auto & data = *operations_.back();
		if (data.waitForAsyncRequest) {
			LogMessage(MessageType::Debug_Info, L"Waiting for async request, ignoring SendNextCommand...");
			return FZ_REPLY_WOULDBLOCK;
		}

		if (!CanSendNextCommand()) {
			SetWait(true);
			return FZ_REPLY_WOULDBLOCK;
		}

		int res = data.Send();
		if (res != FZ_REPLY_CONTINUE) {
			if (res == FZ_REPLY_OK) {
				return ResetOperation(res);
			}
			else if (res & FZ_REPLY_DISCONNECTED) {
				return DoClose(res);
			}
			else if (res & FZ_REPLY_ERROR) {
				return ResetOperation(res);
			}
			else if (res == FZ_REPLY_WOULDBLOCK) {
				return FZ_REPLY_WOULDBLOCK;
			}
			else if (res != FZ_REPLY_CONTINUE) {
				LogMessage(MessageType::Debug_Warning, L"Unknown result %d returned by COpData::Send()", res);
				return ResetOperation(FZ_REPLY_INTERNALERROR);
			}
		}
	}

	return FZ_REPLY_OK;
}

int CControlSocket::ParseSubcommandResult(int prevResult, COpData const& opData)
{
	LogMessage(MessageType::Debug_Verbose, L"CControlSocket::ParseSubcommandResult(%d)", prevResult);
	if (operations_.empty()) {
		LogMessage(MessageType::Debug_Warning, L"ParseSubcommandResult called without active operation");
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	auto & data = *operations_.back();
	int res = data.SubcommandResult(prevResult, opData);
	if (res == FZ_REPLY_WOULDBLOCK) {
		return FZ_REPLY_WOULDBLOCK;
	}
	else if (res == FZ_REPLY_CONTINUE) {
		return SendNextCommand();
	}
	else {
		return ResetOperation(res);
	}
}

const std::list<CControlSocket::t_lockInfo>::iterator CControlSocket::GetLockStatus()
{
	std::list<t_lockInfo>::iterator iter;
	for (iter = m_lockInfoList.begin(); iter != m_lockInfoList.end(); ++iter) {
		if (iter->pControlSocket == this) {
			break;
		}
	}

	return iter;
}

bool CControlSocket::TryLockCache(locking_reason reason, CServerPath const& directory)
{
	assert(currentServer_);
	assert(!operations_.empty());

	std::list<t_lockInfo>::iterator own = GetLockStatus();
	if (own == m_lockInfoList.end()) {
		t_lockInfo info;
		info.directory = directory;
		info.pControlSocket = this;
		info.waiting = true;
		info.reason = reason;
		info.lockcount = 0;
		m_lockInfoList.push_back(info);
		own = --m_lockInfoList.end();
	}
	else {
		if (own->lockcount) {
			if (!operations_.back()->holdsLock_) {
				operations_.back()->holdsLock_ = true;
				own->lockcount++;
			}
			return true;
		}
		assert(own->waiting);
		assert(own->reason == reason);
	}

	// Needs to be set in any case so that ResetOperation
	// unlocks or cancels the lock wait
	operations_.back()->holdsLock_ = true;

	// Try to find other instance holding the lock
	for (auto iter = m_lockInfoList.cbegin(); iter != own; ++iter) {
		if (currentServer_ != iter->pControlSocket->currentServer_) {
			continue;
		}
		if (directory != iter->directory) {
			continue;
		}
		if (reason != iter->reason) {
			continue;
		}

		// Some other instance is holding the lock
		return false;
	}

	own->lockcount++;
	own->waiting = false;
	return true;
}

bool CControlSocket::IsLocked(locking_reason reason, CServerPath const& directory)
{
	assert(currentServer_);

	std::list<t_lockInfo>::iterator own = GetLockStatus();
	if (own != m_lockInfoList.end()) {
		return true;
	}

	// Try to find other instance holding the lock
	for (auto iter = m_lockInfoList.cbegin(); iter != own; ++iter) {
		if (currentServer_ != iter->pControlSocket->currentServer_) {
			continue;
		}
		if (directory != iter->directory) {
			continue;
		}
		if (reason != iter->reason) {
			continue;
		}

		// Some instance is holding the lock
		return true;
	}

	return false;
}

void CControlSocket::UnlockCache()
{
	if (operations_.empty() || !operations_.back()->holdsLock_) {
		return;
	}
	operations_.back()->holdsLock_ = false;

	std::list<t_lockInfo>::iterator iter = GetLockStatus();
	if (iter == m_lockInfoList.end()) {
		return;
	}

	assert(!iter->waiting || iter->lockcount == 0);
	if (!iter->waiting) {
		iter->lockcount--;
		assert(iter->lockcount >= 0);
		if (iter->lockcount) {
			return;
		}
	}

	CServerPath directory = iter->directory;
	locking_reason reason = iter->reason;

	m_lockInfoList.erase(iter);

	// Find other instance waiting for the lock
	if (!currentServer_) {
		LogMessage(MessageType::Debug_Warning, L"UnlockCache called with !currentServer_");
		return;
	}
	for (auto & lockInfo : m_lockInfoList) {
		if (!lockInfo.pControlSocket->currentServer_) {
			LogMessage(MessageType::Debug_Warning, L"UnlockCache found other instance with !currentServer_");
			continue;
		}

		if (currentServer_ != lockInfo.pControlSocket->currentServer_) {
			continue;
		}

		if (lockInfo.directory != directory) {
			continue;
		}

		if (lockInfo.reason != reason) {
			continue;
		}

		// Send notification
		lockInfo.pControlSocket->send_event<CObtainLockEvent>();
		break;
	}
}

CControlSocket::locking_reason CControlSocket::ObtainLockFromEvent()
{
	if (operations_.empty()) {
		return lock_unknown;
	}

	std::list<t_lockInfo>::iterator own = GetLockStatus();
	if (own == m_lockInfoList.end()) {
		return lock_unknown;
	}

	if (!own->waiting) {
		return lock_unknown;
	}

	for (auto iter = m_lockInfoList.cbegin(); iter != own; ++iter) {
		if (currentServer_ != iter->pControlSocket->currentServer_) {
			continue;
		}

		if (iter->directory != own->directory) {
			continue;
		}

		if (iter->reason != own->reason) {
			continue;
		}

		// Another instance comes before us
		return lock_unknown;
	}

	own->waiting = false;
	own->lockcount++;

	return own->reason;
}

void CControlSocket::OnObtainLock()
{
	if (ObtainLockFromEvent() == lock_unknown) {
		return;
	}

	SendNextCommand();

	UnlockCache();
}

bool CControlSocket::IsWaitingForLock()
{
	std::list<t_lockInfo>::iterator own = GetLockStatus();
	if (own == m_lockInfoList.end())
		return false;

	return own->waiting == true;
}

void CControlSocket::InvalidateCurrentWorkingDir(const CServerPath& path)
{
	assert(!path.empty());
	if (currentPath_.empty()) {
		return;
	}

	if (currentPath_ == path || path.IsParentOf(currentPath_, false)) {
		if (!operations_.empty()) {
			m_invalidateCurrentPath = true;
		}
		else {
			currentPath_.clear();
		}
	}
}

fz::duration CControlSocket::GetTimezoneOffset() const
{
	fz::duration ret;
	if (currentServer_) {
		int seconds = 0;
		if (CServerCapabilities::GetCapability(currentServer_, timezone_offset, &seconds) == yes) {
			ret = fz::duration::from_seconds(seconds);
		}
	}
	return ret;
}

void CControlSocket::SendAsyncRequest(CAsyncRequestNotification* pNotification)
{
	assert(pNotification);
	assert(!operations_.empty());

	pNotification->requestNumber = engine_.GetNextAsyncRequestNumber();

	if (!operations_.empty()) {
		operations_.back()->waitForAsyncRequest = true;
	}
	engine_.AddNotification(pNotification);
}

// ------------------
// CRealControlSocket
// ------------------

CRealControlSocket::CRealControlSocket(CFileZillaEnginePrivate & engine)
	: CControlSocket(engine)
{
	m_pSocket = new CSocket(engine.GetThreadPool(), this);

	m_pBackend = new CSocketBackend(this, *m_pSocket, engine_.GetRateLimiter());
}

CRealControlSocket::~CRealControlSocket()
{
	if (m_pSocket) {
		m_pSocket->Close();
	}
	if (m_pProxyBackend && m_pProxyBackend != m_pBackend) {
		delete m_pProxyBackend;
	}
	delete m_pBackend;
	m_pBackend = 0;

	delete m_pSocket;
	delete[] sendBuffer_;
}

bool CRealControlSocket::Connected() const
{
	return m_pSocket ? (m_pSocket->GetState() == CSocket::connected) : false;
}

void CRealControlSocket::AppendToSendBuffer(unsigned char const* data, unsigned int len)
{
	if (sendBufferSize_ + len > sendBufferCapacity_) {
		if (sendBufferSize_ + len - sendBufferPos_ <= sendBufferCapacity_) {
			memmove(sendBuffer_, sendBuffer_ + sendBufferPos_, sendBufferSize_ - sendBufferPos_);
			sendBufferSize_ -= sendBufferPos_;
			sendBufferPos_ = 0;
		}
		else if (!sendBuffer_) {
			assert(!sendBufferSize_ && !sendBufferPos_);
			sendBufferCapacity_ = len;
			sendBuffer_ = new unsigned char[sendBufferCapacity_];
		}
		else {
			unsigned char *old = sendBuffer_;
			sendBufferCapacity_ += len;
			sendBuffer_ = new unsigned char[sendBufferCapacity_];
			memcpy(sendBuffer_, old + sendBufferPos_, sendBufferSize_ - sendBufferPos_);
			sendBufferSize_ -= sendBufferPos_;
			sendBufferPos_ = 0;
			delete[] old;
		}
	}

	memcpy(sendBuffer_ + sendBufferSize_, data, len);
	sendBufferSize_ += len;
}

void CRealControlSocket::SendBufferReserve(unsigned int len)
{
	if (sendBufferCapacity_ < len) {
		if (sendBuffer_) {
			unsigned char *old = sendBuffer_;
			sendBuffer_ = new unsigned char[sendBufferCapacity_ + len];
			memcpy(sendBuffer_, old + sendBufferPos_, sendBufferSize_ - sendBufferPos_);
			sendBufferSize_ -= sendBufferPos_;
			sendBufferPos_ = 0;
			sendBufferCapacity_ += len;
			delete[] old;
		}
		else {
			sendBufferCapacity_ = len;
			sendBuffer_ = new unsigned char[sendBufferCapacity_];
		}
	}
}

int CRealControlSocket::Send(unsigned char const* buffer, unsigned int len)
{
	SetWait(true);
	if (sendBufferSize_) {
		AppendToSendBuffer(buffer, len);
	}
	else {
		int error;
		int written = m_pBackend->Write(buffer, len, error);
		if (written < 0) {
			if (error != EAGAIN) {
				LogMessage(MessageType::Error, _("Could not write to socket: %s"), CSocket::GetErrorDescription(error));
				LogMessage(MessageType::Error, _("Disconnected from server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			written = 0;
		}

		if (written) {
			SetActive(CFileZillaEngine::send);
		}

		if (static_cast<unsigned int>(written) < len) {
			AppendToSendBuffer(buffer + written, len - written);
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

void CRealControlSocket::operator()(fz::event_base const& ev)
{
	if (!fz::dispatch<CSocketEvent, CHostAddressEvent>(ev, this,
		&CRealControlSocket::OnSocketEvent,
		&CRealControlSocket::OnHostAddress))
	{
		CControlSocket::operator()(ev);
	}
}

void CRealControlSocket::OnSocketEvent(CSocketEventSource*, SocketEventType t, int error)
{
	if (!m_pBackend) {
		return;
	}

	switch (t)
	{
	case SocketEventType::connection_next:
		if (error)
			LogMessage(MessageType::Status, _("Connection attempt failed with \"%s\", trying next address."), CSocket::GetErrorDescription(error));
		SetAlive();
		break;
	case SocketEventType::connection:
		if (error) {
			LogMessage(MessageType::Status, _("Connection attempt failed with \"%s\"."), CSocket::GetErrorDescription(error));
			OnClose(error);
		}
		else {
			if (m_pProxyBackend && !m_pProxyBackend->Detached()) {
				m_pProxyBackend->Detach();
				m_pBackend = new CSocketBackend(this, *m_pSocket, engine_.GetRateLimiter());
			}
			OnConnect();
		}
		break;
	case SocketEventType::read:
		OnReceive();
		break;
	case SocketEventType::write:
		OnSend();
		break;
	case SocketEventType::close:
		OnClose(error);
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unhandled socket event %d", t);
		break;
	}
}

void CRealControlSocket::OnHostAddress(CSocketEventSource*, std::string const& address)
{
	if (!m_pBackend) {
		return;
	}

	LogMessage(MessageType::Status, _("Connecting to %s..."), address);
}

void CRealControlSocket::OnConnect()
{
}

void CRealControlSocket::OnReceive()
{
}

int CRealControlSocket::OnSend()
{
	while (sendBufferSize_) {
		assert(sendBufferPos_ < sendBufferSize_);
		assert(sendBuffer_);

		int error;
		int written = m_pBackend->Write(sendBuffer_ + sendBufferPos_, sendBufferSize_ - sendBufferPos_, error);
		if (written < 0) {
			if (error != EAGAIN) {
				LogMessage(MessageType::Error, _("Could not write to socket: %s"), CSocket::GetErrorDescription(error));
				if (GetCurrentCommandId() != Command::connect) {
					LogMessage(MessageType::Error, _("Disconnected from server"));
				}
				DoClose();
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			return FZ_REPLY_WOULDBLOCK;
		}

		if (written) {
			SetActive(CFileZillaEngine::send);
		}

		sendBufferPos_ += static_cast<int>(written);
		if (sendBufferPos_ == sendBufferSize_) {
			sendBufferSize_ = 0;
			sendBufferPos_ = 0;
		}
	}

	return FZ_REPLY_CONTINUE;
}

void CRealControlSocket::OnClose(int error)
{
	LogMessage(MessageType::Debug_Verbose, L"CRealControlSocket::OnClose(%d)", error);

	auto cmd = GetCurrentCommandId();
	if (cmd != Command::connect) {
		auto messageType = (cmd == Command::none) ? MessageType::Status : MessageType::Error;
		if (!error) {
			LogMessage(messageType, _("Connection closed by server"));
		}
		else {
			LogMessage(messageType, _("Disconnected from server: %s"), CSocket::GetErrorDescription(error));
		}
	}
	DoClose();
}

int CRealControlSocket::DoConnect(CServer const& server)
{
	SetWait(true);

	if (server.GetEncodingType() == ENCODING_CUSTOM) {
		LogMessage(MessageType::Debug_Info, L"Using custom encoding: %s", server.GetCustomEncoding());
	}

	return ContinueConnect();
}

int CRealControlSocket::ContinueConnect()
{
	std::wstring host;
	unsigned int port = 0;

	const int proxy_type = engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE);
	if (proxy_type > CProxySocket::unknown && proxy_type < CProxySocket::proxytype_count && !currentServer_.GetBypassProxy()) {
		LogMessage(MessageType::Status, _("Connecting to %s through %s proxy"), currentServer_.Format(ServerFormat::with_optional_port), CProxySocket::Name(static_cast<CProxySocket::ProxyType>(proxy_type)));

		host = engine_.GetOptions().GetOption(OPTION_PROXY_HOST);
		port = engine_.GetOptions().GetOptionVal(OPTION_PROXY_PORT);

		delete m_pBackend;
		m_pProxyBackend = new CProxySocket(this, m_pSocket, this);
		m_pBackend = m_pProxyBackend;
		int res = m_pProxyBackend->Handshake(static_cast<CProxySocket::ProxyType>(proxy_type),
											ConvertDomainName(currentServer_.GetHost()), currentServer_.GetPort(),
											engine_.GetOptions().GetOption(OPTION_PROXY_USER),
											engine_.GetOptions().GetOption(OPTION_PROXY_PASS));

		if (res != EINPROGRESS) {
			LogMessage(MessageType::Error, _("Could not start proxy handshake: %s"), CSocket::GetErrorDescription(res));
			return FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR;
		}
	}
	else {
		if (!operations_.empty() && operations_.back()->opId == Command::connect) {
			auto & data = static_cast<CConnectOpData&>(*operations_.back());
			host = data.host_;
			port = data.port_;
		}
		if (host.empty()) {
			host = currentServer_.GetHost();
			port = currentServer_.GetPort();
		}
	}
	if (fz::get_address_type(host) == fz::address_type::unknown) {
		LogMessage(MessageType::Status, _("Resolving address of %s"), host);
	}

	host = ConvertDomainName(host);
	int res = m_pSocket->Connect(fz::to_native(host), port);

	// Treat success same as EINPROGRESS, we wait for connect notification in any case
	if (res && res != EINPROGRESS) {
		LogMessage(MessageType::Error, _("Could not connect to server: %s"), CSocket::GetErrorDescription(res));
		return FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR; 
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CRealControlSocket::DoClose(int nErrorCode)
{
	ResetSocket();

	return CControlSocket::DoClose(nErrorCode);
}

void CRealControlSocket::ResetSocket()
{
	m_pSocket->Close();

	sendBufferSize_ = 0;
	sendBufferPos_ = 0;

	if (m_pProxyBackend) {
		if (m_pProxyBackend != m_pBackend) {
			delete m_pProxyBackend;
		}
		m_pProxyBackend = 0;
	}
	delete m_pBackend;
	m_pBackend = 0;
}

bool CControlSocket::SetFileExistsAction(CFileExistsNotification *pFileExistsNotification)
{
	assert(pFileExistsNotification);

	if (operations_.empty() || operations_.back()->opId != Command::transfer) {
		LogMessage(MessageType::Debug_Info, L"SetFileExistsAction: No or invalid operation in progress, ignoring request reply %f", pFileExistsNotification->GetRequestID());
		return false;
	}

	auto & data = static_cast<CFileTransferOpData &>(*operations_.back());

	switch (pFileExistsNotification->overwriteAction)
	{
	case CFileExistsNotification::overwrite:
		SendNextCommand();
		break;
	case CFileExistsNotification::overwriteNewer:
		if (pFileExistsNotification->localTime.empty() || pFileExistsNotification->remoteTime.empty()) {
			SendNextCommand();
		}
		else if (pFileExistsNotification->download && pFileExistsNotification->localTime.earlier_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else if (!pFileExistsNotification->download && pFileExistsNotification->localTime.later_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else {
			if (data.download_) {
				std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
				LogMessage(MessageType::Status, _("Skipping download of %s"), filename);
			}
			else {
				LogMessage(MessageType::Status, _("Skipping upload of %s"), data.localFile_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::overwriteSize:
		// First compare flags both size known but different, one size known and the other not (obviously they are different).
		// Second compare flags the remaining case in which we need to send command : both size unknown
		if ((pFileExistsNotification->localSize != pFileExistsNotification->remoteSize) || (pFileExistsNotification->localSize < 0)) {
			SendNextCommand();
		}
		else {
			if (data.download_) {
				std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
				LogMessage(MessageType::Status, _("Skipping download of %s"), filename);
			}
			else {
				LogMessage(MessageType::Status, _("Skipping upload of %s"), data.localFile_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::overwriteSizeOrNewer:
		if (pFileExistsNotification->localTime.empty() || pFileExistsNotification->remoteTime.empty()) {
			SendNextCommand();
		}
		// First compare flags both size known but different, one size known and the other not (obviously they are different).
		// Second compare flags the remaining case in which we need to send command : both size unknown
		else if ((pFileExistsNotification->localSize != pFileExistsNotification->remoteSize) || (pFileExistsNotification->localSize < 0)) {
			SendNextCommand();
		}
		else if (pFileExistsNotification->download && pFileExistsNotification->localTime.earlier_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else if (!pFileExistsNotification->download && pFileExistsNotification->localTime.later_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else {
			if (data.download_) {
				auto const filename = data.remotePath_.FormatFilename(data.remoteFile_);
				LogMessage(MessageType::Status, _("Skipping download of %s"), filename);
			}
			else {
				LogMessage(MessageType::Status, _("Skipping upload of %s"), data.localFile_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::resume:
		if (data.download_ && data.localFileSize_ >= 0) {
			data.resume_ = true;
		}
		else if (!data.download_ && data.remoteFileSize_ >= 0) {
			data.resume_ = true;
		}
		SendNextCommand();
		break;
	case CFileExistsNotification::rename:
		if (data.download_) {
			{
				std::wstring tmp;
				CLocalPath l(data.localFile_, &tmp);
				if (l.empty() || tmp.empty()) {
					ResetOperation(FZ_REPLY_INTERNALERROR);
					return false;
				}
				if (!l.ChangePath(pFileExistsNotification->newName)) {
					ResetOperation(FZ_REPLY_INTERNALERROR);
					return false;
				}
				if (!l.HasParent() || !l.MakeParent(&tmp)) {
					ResetOperation(FZ_REPLY_INTERNALERROR);
					return false;
				}

				data.localFile_ = l.GetPath() + tmp;
			}

			int64_t size;
			bool isLink;
			if (fz::local_filesys::get_file_info(fz::to_native(data.localFile_), isLink, &size, 0, 0) == fz::local_filesys::file) {
				data.localFileSize_ = size;
			}
			else {
				data.localFileSize_ = -1;
			}

			if (CheckOverwriteFile() == FZ_REPLY_OK) {
				SendNextCommand();
			}
		}
		else {
			data.remoteFile_ = pFileExistsNotification->newName;
			data.fileTime_ = fz::datetime();
			data.remoteFileSize_ = -1;

			CDirentry entry;
			bool dir_did_exist;
			bool matched_case;
			if (engine_.GetDirectoryCache().LookupFile(entry, currentServer_, data.tryAbsolutePath_ ? data.remotePath_ : currentPath_, data.remoteFile_, dir_did_exist, matched_case) &&
				matched_case)
			{
				data.remoteFileSize_ = entry.size;
				if (entry.has_date()) {
					data.fileTime_ = entry.time;
				}

				if (CheckOverwriteFile() != FZ_REPLY_OK) {
					break;
				}
			}

			SendNextCommand();
		}
		break;
	case CFileExistsNotification::skip:
		if (data.download_) {
			std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
			LogMessage(MessageType::Status, _("Skipping download of %s"), filename);
		}
		else {
			LogMessage(MessageType::Status, _("Skipping upload of %s"), data.localFile_);
		}
		ResetOperation(FZ_REPLY_OK);
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown file exists action: %d", pFileExistsNotification->overwriteAction);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}

void CControlSocket::CreateLocalDir(std::wstring const & local_file)
{
	std::wstring file;
	CLocalPath local_path(local_file, &file);
	if (local_path.empty() || !local_path.HasParent()) {
		return;
	}

	CLocalPath last_successful;
	local_path.Create(&last_successful);

	if (!last_successful.empty()) {
		// Send out notification
		CLocalDirCreatedNotification *n = new CLocalDirCreatedNotification;
		n->dir = last_successful;
		engine_.AddNotification(n);
	}
}

void CControlSocket::List(CServerPath const&, std::wstring const&, int)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::RawCommand(std::wstring const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Delete(CServerPath const&, std::deque<std::wstring>&&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::RemoveDir(CServerPath const&, std::wstring const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Mkdir(CServerPath const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Rename(CRenameCommand const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Chmod(CChmodCommand const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::timer_event, CObtainLockEvent>(ev, this,
		&CControlSocket::OnTimer,
		&CControlSocket::OnObtainLock);
}

void CControlSocket::SetActive(CFileZillaEngine::_direction direction)
{
	SetAlive();
	engine_.SetActive(direction);
}

void CControlSocket::SendDirectoryListingNotification(CServerPath const& path, bool onList, bool failed)
{
	if (!currentServer_) {
		return;
	}

	engine_.AddNotification(new CDirectoryListingNotification(path, !onList, failed));
}
