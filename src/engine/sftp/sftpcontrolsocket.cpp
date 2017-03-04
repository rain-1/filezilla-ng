#include <filezilla.h>

#include "chmod.h"
#include "connect.h"
#include "cwd.h"
#include "delete.h"
#include "directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "event.h"
#include "filetransfer.h"
#include "list.h"
#include "input_thread.h"
#include "mkd.h"
#include "pathcache.h"
#include "proxy.h"
#include "rename.h"
#include "rmd.h"
#include "servercapabilities.h"
#include "sftpcontrolsocket.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>

#include <algorithm>
#include <cwchar>

CSftpControlSocket::CSftpControlSocket(CFileZillaEnginePrivate & engine)
	: CControlSocket(engine)
{
	m_useUTF8 = true;
}

CSftpControlSocket::~CSftpControlSocket()
{
	remove_handler();
	DoClose();
}

void CSftpControlSocket::Connect(CServer const& server)
{
	LogMessage(MessageType::Status, _("Connecting to %s..."), server.Format(ServerFormat::with_optional_port));
	SetWait(true);

	m_sftpEncryptionDetails = CSftpEncryptionNotification();

	if (server.GetEncodingType() == ENCODING_CUSTOM) {
		LogMessage(MessageType::Debug_Info, L"Using custom encoding: %s", server.GetCustomEncoding());
		m_useUTF8 = false;
	}
	else {
		m_useUTF8 = true;
	}

	currentServer_ = server;

	auto pData = std::make_unique<CSftpConnectOpData>(*this);
	if (currentServer_.GetLogonType() == KEY) {
		pData->keyfiles_ = fz::strtok(currentServer_.GetKeyFile(), L"\r\n");
	}
	else {
		pData->keyfiles_ = fz::strtok(engine_.GetOptions().GetOption(OPTION_SFTP_KEYFILES), L"\r\n");
	}

	pData->keyfiles_.erase(
		std::remove_if(pData->keyfiles_.begin(), pData->keyfiles_.end(),
			[this](std::wstring const& keyfile) {
				if (fz::local_filesys::get_file_type(fz::to_native(keyfile), true) != fz::local_filesys::file) {
					LogMessage(MessageType::Status, _("Skipping non-existing key file \"%s\""), keyfile);
					return true;
				}
				return false;
		}), pData->keyfiles_.end());

	pData->keyfile_ = pData->keyfiles_.cbegin();

	process_ = std::make_unique<fz::process>();

	engine_.GetRateLimiter().AddObject(this);
	Push(std::move(pData));
}

void CSftpControlSocket::OnSftpEvent(sftp_message const& message)
{
	if (!currentServer_) {
		return;
	}

	if (!input_thread_) {
		return;
	}

	switch (message.type)
	{
	case sftpEvent::Reply:
		LogMessageRaw(MessageType::Response, message.text[0]);
		ProcessReply(FZ_REPLY_OK, message.text[0]);
		break;
	case sftpEvent::Done:
		{
			int result;
			if (message.text[0] == L"1") {
				result = FZ_REPLY_OK;
			}
			else if (message.text[0] == L"2") {
				result = FZ_REPLY_CRITICALERROR;
			}
			else {
				result = FZ_REPLY_ERROR;
			}
			ProcessReply(result, std::wstring());
		}
		break;
	case sftpEvent::Error:
		LogMessageRaw(MessageType::Error, message.text[0]);
		break;
	case sftpEvent::Verbose:
		LogMessageRaw(MessageType::Debug_Info, message.text[0]);
		break;
	case sftpEvent::Info:
		LogMessageRaw(MessageType::Command, message.text[0]); // Not exactly the right message type, but it's a silent one.
		break;
	case sftpEvent::Status:
		LogMessageRaw(MessageType::Status, message.text[0]);
		break;
	case sftpEvent::Recv:
		SetActive(CFileZillaEngine::recv);
		break;
	case sftpEvent::Send:
		SetActive(CFileZillaEngine::send);
		break;
	case sftpEvent::Listentry:
		if (operations_.empty() || operations_.back()->opId != Command::list) {
			LogMessage(MessageType::Debug_Warning, L"sftpEvent::Listentry outside list operation, ignoring.");
			break;
		}
		else {
			int res = static_cast<CSftpListOpData&>(*operations_.back()).ParseEntry(std::move(message.text[0]), message.text[1], std::move(message.text[2]));
			if (res != FZ_REPLY_WOULDBLOCK) {
				ResetOperation(res);
			}
		}
		break;
	case sftpEvent::Transfer:
		{
			auto value = fz::to_integral<int64_t>(message.text[0]);

			bool tmp;
			CTransferStatus status = engine_.transfer_status_.Get(tmp);
			if (!status.empty() && !status.madeProgress) {
				if (!operations_.empty() && operations_.back()->opId == Command::transfer) {
					auto & data = static_cast<CSftpFileTransferOpData &>(*operations_.back());
					if (data.download_) {
						if (value > 0) {
							engine_.transfer_status_.SetMadeProgress();
						}
					}
					else {
						if (status.currentOffset > status.startOffset + 65565) {
							engine_.transfer_status_.SetMadeProgress();
						}
					}
				}
			}

			engine_.transfer_status_.Update(value);
		}
		break;
	case sftpEvent::AskHostkey:
	case sftpEvent::AskHostkeyChanged:
		{
			auto port = fz::to_integral<int>(message.text[1]);
			if (port <= 0 || port > 65535) {
				DoClose(FZ_REPLY_INTERNALERROR);
				break;
			}
			SendAsyncRequest(new CHostKeyNotification(message.text[0], port, m_sftpEncryptionDetails, message.type == sftpEvent::AskHostkeyChanged));
		}
		break;
	case sftpEvent::AskHostkeyBetteralg:
		LogMessage(MessageType::Error, L"Got sftpReqHostkeyBetteralg when we shouldn't have. Aborting connection.");
		DoClose(FZ_REPLY_INTERNALERROR);
		break;
	case sftpEvent::AskPassword:
		if (operations_.empty() || operations_.back()->opId != Command::connect) {
			LogMessage(MessageType::Debug_Warning, L"sftpReqPassword outside connect operation, ignoring.");
			break;
		}
		else {
			auto & data = static_cast<CSftpConnectOpData&>(*operations_.back());

			std::wstring const challengeIdentifier = m_requestPreamble + L"\n" + m_requestInstruction + L"\n" + message.text[0];

			CInteractiveLoginNotification::type t = CInteractiveLoginNotification::interactive;
			if (currentServer_.GetLogonType() == INTERACTIVE || m_requestPreamble == L"SSH key passphrase") {
				if (m_requestPreamble == L"SSH key passphrase") {
					t = CInteractiveLoginNotification::keyfile;
				}

				std::wstring challenge;
				if (!m_requestPreamble.empty() && t != CInteractiveLoginNotification::keyfile) {
					challenge += m_requestPreamble + L"\n";
				}
				if (!m_requestInstruction.empty()) {
					challenge += m_requestInstruction + L"\n";
				}
				if (message.text[0] != L"Password:") {
					challenge += message.text[0];
				}
				CInteractiveLoginNotification *pNotification = new CInteractiveLoginNotification(t, challenge, data.lastChallenge == challengeIdentifier);
				pNotification->server = currentServer_;

				SendAsyncRequest(pNotification);
			}
			else {
				if (!data.lastChallenge.empty() && data.lastChallengeType != CInteractiveLoginNotification::keyfile) {
					// Check for same challenge. Will most likely fail as well, so abort early.
					if (data.lastChallenge == challengeIdentifier) {
						LogMessage(MessageType::Error, _("Authentication failed."));
					}
					else {
						LogMessage(MessageType::Error, _("Server sent an additional login prompt. You need to use the interactive login type."));
					}
					DoClose(FZ_REPLY_CRITICALERROR | FZ_REPLY_PASSWORDFAILED);
					return;
				}

				std::wstring const pass = currentServer_.GetPass();
				std::wstring show = L"Pass: ";
				show.append(pass.size(), '*');
				SendCommand(pass, show);
			}
			data.lastChallenge = challengeIdentifier;
			data.lastChallengeType = t;
		}
		break;
	case sftpEvent::RequestPreamble:
		m_requestPreamble = message.text[0];
		break;
	case sftpEvent::RequestInstruction:
		m_requestInstruction = message.text[0];
		break;
	case sftpEvent::UsedQuotaRecv:
		OnQuotaRequest(CRateLimiter::inbound);
		break;
	case sftpEvent::UsedQuotaSend:
		OnQuotaRequest(CRateLimiter::outbound);
		break;
	case sftpEvent::KexAlgorithm:
		m_sftpEncryptionDetails.kexAlgorithm = message.text[0];
		break;
	case sftpEvent::KexHash:
		m_sftpEncryptionDetails.kexHash = message.text[0];
		break;
	case sftpEvent::KexCurve:
		m_sftpEncryptionDetails.kexCurve = message.text[0];
		break;
	case sftpEvent::CipherClientToServer:
		m_sftpEncryptionDetails.cipherClientToServer = message.text[0];
		break;
	case sftpEvent::CipherServerToClient:
		m_sftpEncryptionDetails.cipherServerToClient = message.text[0];
		break;
	case sftpEvent::MacClientToServer:
		m_sftpEncryptionDetails.macClientToServer = message.text[0];
		break;
	case sftpEvent::MacServerToClient:
		m_sftpEncryptionDetails.macServerToClient = message.text[0];
		break;
	case sftpEvent::Hostkey:
		{
			auto tokens = fz::strtok(message.text[0], ' ');
			if (!tokens.empty()) {
				m_sftpEncryptionDetails.hostKeyFingerprintSHA256 = tokens.back();
				tokens.pop_back();
			}
			if (!tokens.empty()) {
				m_sftpEncryptionDetails.hostKeyFingerprintMD5 = tokens.back();
				tokens.pop_back();
			}
			for (auto const& token : tokens) {
				if (!m_sftpEncryptionDetails.hostKeyAlgorithm.empty()) {
					m_sftpEncryptionDetails.hostKeyAlgorithm += ' ';
				}
				m_sftpEncryptionDetails.hostKeyAlgorithm += token;
			}
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"Message type %d not handled", message.type);
		break;
	}
}

void CSftpControlSocket::OnTerminate(std::wstring const& error)
{
	if (!error.empty()) {
		LogMessageRaw(MessageType::Error, error);
	}
	else {
		LogMessageRaw(MessageType::Debug_Info, L"CSftpControlSocket::OnTerminate without error");
	}
	if (process_) {
		DoClose();
	}
}

int CSftpControlSocket::SendCommand(std::wstring const& cmd, std::wstring const& show)
{
	SetWait(true);

	LogMessageRaw(MessageType::Command, show.empty() ? cmd : show);

	// Check for newlines in command
	// a command like "ls\nrm foo/bar" is dangerous
	if (cmd.find('\n') != std::wstring::npos ||
		cmd.find('\r') != std::wstring::npos)
	{
		LogMessage(MessageType::Debug_Warning, L"Command containing newline characters, aborting.");
		return FZ_REPLY_INTERNALERROR;
	}

	return AddToStream(cmd + L"\n");
}

int CSftpControlSocket::AddToStream(std::wstring const& cmd)
{
	std::string const str = ConvToServer(cmd);
	if (str.empty()) {
		LogMessage(MessageType::Error, _("Could not convert command to server encoding"));
		return FZ_REPLY_ERROR;
	}

	return AddToStream(str);
}

int CSftpControlSocket::AddToStream(std::string const& cmd)
{
	if (!process_) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (!process_->write(cmd)) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_WOULDBLOCK;
}

bool CSftpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (operations_.empty() || !operations_.back()->waitForAsyncRequest) {
		LogMessage(MessageType::Debug_Info, L"Not waiting for request reply, ignoring request reply %d", pNotification->GetRequestID());
		return false;
	}

	operations_.back()->waitForAsyncRequest = false;

	RequestId const requestId = pNotification->GetRequestID();
	switch(requestId)
	{
	case reqId_fileexists:
		{
			auto *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
	case reqId_hostkey:
	case reqId_hostkeyChanged:
		{
			if (GetCurrentCommandId() != Command::connect ||
				!currentServer_)
			{
				LogMessage(MessageType::Debug_Info, L"SetAsyncRequestReply called to wrong time");
				return false;
			}

			auto *pHostKeyNotification = static_cast<CHostKeyNotification *>(pNotification);
			std::wstring show;
			if (requestId == reqId_hostkey) {
				show = _("Trust new Hostkey:");
			}
			else {
				show = _("Trust changed Hostkey:");
			}
			show += ' ';
			if (!pHostKeyNotification->m_trust) {
				SendCommand(std::wstring(), show + _("No"));
				if (operations_.back()->opId == Command::connect) {
					auto &data = static_cast<CSftpConnectOpData &>(*operations_.back());
					data.criticalFailure = true;
				}
			}
			else if (pHostKeyNotification->m_alwaysTrust) {
				SendCommand(L"y", show + _("Yes"));
			}
			else {
				SendCommand(L"n", show + _("Once"));
			}
		}
		break;
	case reqId_interactiveLogin:
		{
			auto *pInteractiveLoginNotification = static_cast<CInteractiveLoginNotification *>(pNotification);

			if (!pInteractiveLoginNotification->passwordSet) {
				DoClose(FZ_REPLY_CANCELED);
				return false;
			}
			std::wstring const pass = pInteractiveLoginNotification->server.GetPass();
			currentServer_.SetUser(currentServer_.GetUser(), pass);
			std::wstring show = L"Pass: ";
			show.append(pass.size(), '*');
			SendCommand(pass, show);
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown async request reply id: %d", requestId);
		return false;
	}

	return true;
}

void CSftpControlSocket::List(CServerPath const& path, std::wstring const& subDir, int flags)
{
	CServerPath newPath = currentPath_;
	if (!path.empty()) {
		newPath = path;
	}
	if (!newPath.ChangePath(subDir)) {
		newPath.clear();
	}

	if (newPath.empty()) {
		LogMessage(MessageType::Status, _("Retrieving directory listing..."));
	}
	else {
		LogMessage(MessageType::Status, _("Retrieving directory listing of \"%s\"..."), newPath.GetPath());
	}

	Push(std::make_unique<CSftpListOpData>(*this, path, subDir, flags, operations_.empty()));
}

void CSftpControlSocket::ChangeDir(CServerPath const& path, std::wstring const& subDir, bool link_discovery)
{
	auto pData = std::make_unique<CSftpChangeDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->link_discovery_ = link_discovery;

	if (!operations_.empty() && operations_.back()->opId == Command::transfer &&
		!static_cast<CSftpFileTransferOpData&>(*operations_.back()).download_)
	{
		pData->tryMkdOnFail_ = true;
		assert(subDir.empty());
	}

	Push(std::move(pData));
}

void CSftpControlSocket::ProcessReply(int result, std::wstring const& reply)
{
	result_ = result;
	response_ = reply;

	if (operations_.empty()) {
		LogMessage(MessageType::Debug_Info, L"Skipping reply without active operation.");
		return;
	}

	auto & data = *operations_.back();
	int res = data.ParseResponse();
	if (res == FZ_REPLY_OK) {
		ResetOperation(FZ_REPLY_OK);
	}
	else if (res == FZ_REPLY_CONTINUE) {
		SendNextCommand();
	}
	else if (res & FZ_REPLY_DISCONNECTED) {
		DoClose(res);
	}
	else if (res & FZ_REPLY_ERROR) {
		if (data.opId == Command::connect) {
			DoClose(res | FZ_REPLY_DISCONNECTED);
		}
		else {
			ResetOperation(res);
		}
	}
}

int CSftpControlSocket::ResetOperation(int nErrorCode)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::ResetOperation(%d)", nErrorCode);

	if (!operations_.empty() && operations_.back()->opId == Command::connect) {
		auto &data = static_cast<CSftpConnectOpData &>(*operations_.back());
		if (data.opState == connect_init && (nErrorCode & FZ_REPLY_CANCELED) != FZ_REPLY_CANCELED) {
			LogMessage(MessageType::Error, _("fzsftp could not be started"));
		}
		if (data.criticalFailure) {
			nErrorCode |= FZ_REPLY_CRITICALERROR;
		}
	}
	if (!operations_.empty() && operations_.back()->opId == Command::del && !(nErrorCode & FZ_REPLY_DISCONNECTED)) {
		auto &data = static_cast<CSftpDeleteOpData &>(*operations_.back());
		if (data.needSendListing_) {
			SendDirectoryListingNotification(data.path_, false, false);
		}
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

void CSftpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const& transferSettings)
{
	auto pData = std::make_unique<CSftpFileTransferOpData>(*this, download, localFile, remoteFile, remotePath);
	pData->transferSettings_ = transferSettings;
	Push(std::move(pData));
}

int CSftpControlSocket::DoClose(int nErrorCode)
{
	engine_.GetRateLimiter().RemoveObject(this);

	if (process_) {
		process_->kill();
	}

	if (input_thread_) {
		input_thread_.reset();

		auto threadEventsFilter = [&](fz::event_loop::Events::value_type const& ev) -> bool {
			if (ev.first != this) {
				return false;
			}
			else if (ev.second->derived_type() == CSftpEvent::type() || ev.second->derived_type() == CTerminateEvent::type()) {
				return true;
			}
			return false;
		};

		event_loop_.filter_events(threadEventsFilter);
	}
	process_.reset();
	return CControlSocket::DoClose(nErrorCode);
}

void CSftpControlSocket::Cancel()
{
	if (GetCurrentCommandId() != Command::none) {
		DoClose(FZ_REPLY_CANCELED);
	}
}

void CSftpControlSocket::Mkdir(CServerPath const& path)
{
	/* Directory creation works like this: First find a parent directory into
	 * which we can CWD, then create the subdirs one by one. If either part
	 * fails, try MKD with the full path directly.
	 */

	if (operations_.empty()) {
		LogMessage(MessageType::Status, _("Creating directory '%s'..."), path.GetPath());
	}

	auto pData = std::make_unique<CSftpMkdirOpData>(*this);
	pData->path_ = path;
	Push(std::move(pData));
}

std::wstring CSftpControlSocket::QuoteFilename(std::wstring const& filename)
{
	return L"\"" + fz::replaced_substrings(filename, L"\"", L"\"\"") + L"\"";
}

void CSftpControlSocket::Delete(const CServerPath& path, std::deque<std::wstring>&& files)
{
	// CFileZillaEnginePrivate should have checked this already
	assert(!files.empty());

	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::Delete");
	
	auto pData = std::make_unique<CSftpDeleteOpData>(*this);
	pData->path_ = path;
	pData->files_ = std::move(files);
	Push(std::move(pData));
}

void CSftpControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::RemoveDir");

	auto pData = std::make_unique<CSftpRemoveDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	Push(std::move(pData));
}

void CSftpControlSocket::Chmod(CChmodCommand const& command)
{
	LogMessage(MessageType::Status, _("Setting permissions of '%s' to '%s'"), command.GetPath().FormatFilename(command.GetFile()), command.GetPermission());
	Push(std::make_unique<CSftpChmodOpData>(*this, command));
}

void CSftpControlSocket::Rename(CRenameCommand const& command)
{
	LogMessage(MessageType::Status, _("Renaming '%s' to '%s'"), command.GetFromPath().FormatFilename(command.GetFromFile()), command.GetToPath().FormatFilename(command.GetToFile()));
	Push(std::make_unique<CSftpRenameOpData>(*this, command));
}

std::wstring CSftpControlSocket::WildcardEscape(std::wstring const& file)
{
	std::wstring ret;
	// see src/putty/wildcard.c

	ret.reserve(file.size());
	for (size_t i = 0; i < file.size(); ++i) {
		auto const& c = file[i];
		switch (c)
		{
		case '[':
		case ']':
		case '*':
		case '?':
		case '\\':
			ret.push_back('\\');
			break;
		default:
			break;
		}
		ret.push_back(c);
	}
	return ret;
}

void CSftpControlSocket::OnRateAvailable(CRateLimiter::rate_direction direction)
{
	OnQuotaRequest(direction);
}

void CSftpControlSocket::OnQuotaRequest(CRateLimiter::rate_direction direction)
{
	int64_t bytes = GetAvailableBytes(direction);
	if (bytes > 0) {
		int b;
		if (bytes > std::numeric_limits<int>::max()) {
			b = std::numeric_limits<int>::max();
		}
		else {
			b = static_cast<int>(bytes);
		}
		AddToStream(fz::sprintf(L"-%d%d,%d\n", direction, b, engine_.GetOptions().GetOptionVal(OPTION_SPEEDLIMIT_INBOUND + static_cast<int>(direction))));
		UpdateUsage(direction, b);
	}
	else if (bytes == 0) {
		Wait(direction);
	}
	else if (bytes < 0) {
		AddToStream(fz::sprintf(L"-%d-\n", direction));
	}
}

void CSftpControlSocket::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<CSftpEvent, CTerminateEvent>(ev, this,
		&CSftpControlSocket::OnSftpEvent,
		&CSftpControlSocket::OnTerminate)) {
		return;
	}

	CControlSocket::operator()(ev);
}
