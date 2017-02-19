#include <filezilla.h>

#include "connect.h"
#include "cwd.h"
#include "directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "event.h"
#include "list.h"
#include "input_thread.h"
#include "pathcache.h"
#include "proxy.h"
#include "servercapabilities.h"
#include "sftpcontrolsocket.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>

#include <wx/string.h>

#include <algorithm>
#include <cwchar>

class CSftpFileTransferOpData final : public CFileTransferOpData
{
public:
	CSftpFileTransferOpData(bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path)
		: CFileTransferOpData(is_download, local_file, remote_file, remote_path)
	{
	}
};

enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_waitcwd,
	filetransfer_waitlist,
	filetransfer_mtime,
	filetransfer_transfer,
	filetransfer_chmtime
};

class CSftpDeleteOpData final : public COpData
{
public:
	CSftpDeleteOpData()
		: COpData(Command::del)
	{
	}

	CServerPath path;
	std::deque<std::wstring> files;

	// Set to fz::datetime::Now initially and after
	// sending an updated listing to the UI.
	fz::datetime m_time;

	bool m_needSendListing{};

	// Set to true if deletion of at least one file failed
	bool m_deleteFailed{};
};

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

	delete m_pCSConv;
	if (server.GetEncodingType() == ENCODING_CUSTOM) {
		LogMessage(MessageType::Debug_Info, L"Using custom encoding: %s", server.GetCustomEncoding());
		m_pCSConv = new wxCSConv(server.GetCustomEncoding());
		m_useUTF8 = false;
	}
	else {
		m_pCSConv = 0;
		m_useUTF8 = true;
	}

	currentServer_ = server;

	CSftpConnectOpData* pData = new CSftpConnectOpData(*this);
	Push(pData);

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
		if (!m_pCurOpData || m_pCurOpData->opId != Command::list) {
			LogMessage(MessageType::Debug_Warning, L"sftpEvent::Listentry outside list operation, ignoring.");
			break;
		}
		else {
			int res = static_cast<CSftpListOpData*>(m_pCurOpData)->ParseEntry(std::move(message.text[0]), message.text[1], std::move(message.text[2]));
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
				if (m_pCurOpData && m_pCurOpData->opId == Command::transfer) {
					CSftpFileTransferOpData *pData = static_cast<CSftpFileTransferOpData *>(m_pCurOpData);
					if (pData->download_) {
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
		if (!m_pCurOpData || m_pCurOpData->opId != Command::connect) {
			LogMessage(MessageType::Debug_Warning, L"sftpReqPassword outside connect operation, ignoring.");
			break;
		}
		else {
			CSftpConnectOpData *pData = static_cast<CSftpConnectOpData*>(m_pCurOpData);

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
				CInteractiveLoginNotification *pNotification = new CInteractiveLoginNotification(t, challenge, pData->lastChallenge == challengeIdentifier);
				pNotification->server = currentServer_;

				SendAsyncRequest(pNotification);
			}
			else {
				if (!pData->lastChallenge.empty() && pData->lastChallengeType != CInteractiveLoginNotification::keyfile) {
					// Check for same challenge. Will most likely fail as well, so abort early.
					if (pData->lastChallenge == challengeIdentifier) {
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
			pData->lastChallenge = challengeIdentifier;
			pData->lastChallengeType = t;
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
		wxFAIL_MSG(L"given notification codes not handled");
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

int CSftpControlSocket::AddToStream(std::wstring const& cmd, bool force_utf8)
{
	if (!process_) {
		return FZ_REPLY_INTERNALERROR;
	}

	std::string const str = ConvToServer(cmd, force_utf8);
	if (str.empty()) {
		LogMessage(MessageType::Error, _("Could not convert command to server encoding"));
		return FZ_REPLY_ERROR;
	}

	if (!process_->write(str)) {
		FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_WOULDBLOCK;
}

bool CSftpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (m_pCurOpData) {
		if (!m_pCurOpData->waitForAsyncRequest) {
			LogMessage(MessageType::Debug_Info, L"Not waiting for request reply, ignoring request reply %d", pNotification->GetRequestID());
			return false;
		}
		m_pCurOpData->waitForAsyncRequest = false;
	}

	RequestId const requestId = pNotification->GetRequestID();
	switch(requestId)
	{
	case reqId_fileexists:
		{
			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
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

			CHostKeyNotification *pHostKeyNotification = static_cast<CHostKeyNotification *>(pNotification);
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
				if (m_pCurOpData && m_pCurOpData->opId == Command::connect) {
					CSftpConnectOpData *pData = static_cast<CSftpConnectOpData *>(m_pCurOpData);
					pData->criticalFailure = true;
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
			CInteractiveLoginNotification *pInteractiveLoginNotification = static_cast<CInteractiveLoginNotification *>(pNotification);

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
	CServerPath newPath = m_CurrentPath;
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

	CSftpListOpData *pData = new CSftpListOpData(*this, path, subDir, flags);
	Push(pData);
}

void CSftpControlSocket::ChangeDir(CServerPath const& path, std::wstring const& subDir, bool link_discovery)
{
	auto pData = new CSftpChangeDirOpData(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->link_discovery_ = link_discovery;

	if (pData->pNextOpData && pData->pNextOpData->opId == Command::transfer &&
		!static_cast<CSftpFileTransferOpData *>(pData->pNextOpData)->download_)
	{
		pData->tryMkdOnFail_ = true;
		assert(subDir.empty());
	}

	Push(pData);
}

void CSftpControlSocket::ProcessReply(int result, std::wstring const& reply)
{
	result_ = result;
	response_ = reply;

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Skipping reply without active operation.");
		return;
	}

	int res = m_pCurOpData->ParseResponse();
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
		if (m_pCurOpData->opId == Command::connect) {
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

	if (m_pCurOpData && m_pCurOpData->opId == Command::connect) {
		CSftpConnectOpData *pData = static_cast<CSftpConnectOpData *>(m_pCurOpData);
		if (pData->opState == connect_init && (nErrorCode & FZ_REPLY_CANCELED) != FZ_REPLY_CANCELED) {
			LogMessage(MessageType::Error, _("fzsftp could not be started"));
		}
		if (pData->criticalFailure) {
			nErrorCode |= FZ_REPLY_CRITICALERROR;
		}
	}
	if (m_pCurOpData && m_pCurOpData->opId == Command::del && !(nErrorCode & FZ_REPLY_DISCONNECTED)) {
		CSftpDeleteOpData *pData = static_cast<CSftpDeleteOpData *>(m_pCurOpData);
		if (pData->m_needSendListing) {
			SendDirectoryListingNotification(pData->path, false, false);
		}
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

void CSftpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const& transferSettings)
{
	//FIXME

	/*
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::FileTransfer(...)");

	if (localFile.empty()) {
		if (!download) {
			ResetOperation(FZ_REPLY_CRITICALERROR | FZ_REPLY_NOTSUPPORTED);
		}
		else {
			ResetOperation(FZ_REPLY_SYNTAXERROR);
		}
		return FZ_REPLY_ERROR;
	}

	if (download) {
		std::wstring filename = remotePath.FormatFilename(remoteFile);
		LogMessage(MessageType::Status, _("Starting download of %s"), filename);
	}
	else {
		LogMessage(MessageType::Status, _("Starting upload of %s"), localFile);
	}
	if (m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"deleting nonzero pData");
		delete m_pCurOpData;
	}
	*/
	CSftpFileTransferOpData *pData = new CSftpFileTransferOpData(download, localFile, remoteFile, remotePath);
	Push(pData);

	pData->transferSettings_ = transferSettings;

	int64_t size;
	bool isLink;
	if (fz::local_filesys::get_file_info(fz::to_native(pData->localFile_), isLink, &size, 0, 0) == fz::local_filesys::file) {
		pData->localFileSize_ = size;
	}

	pData->opState = filetransfer_waitcwd;

	if (pData->remotePath_.GetType() == DEFAULT) {
		pData->remotePath_.SetType(currentServer_.GetType());
	}

	ChangeDir(pData->remotePath_);
}

int CSftpControlSocket::FileTransferSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::FileTransferSubcommandResult()");

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		return FZ_REPLY_INTERNALERROR;
	}

	CSftpFileTransferOpData *pData = static_cast<CSftpFileTransferOpData *>(m_pCurOpData);

	if (pData->opState == filetransfer_waitcwd) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, pData->tryAbsolutePath_ ? pData->remotePath_ : m_CurrentPath, pData->remoteFile_, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist)
					pData->opState = filetransfer_waitlist;
				else if (pData->download_ &&
					engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
				{
					pData->opState = filetransfer_mtime;
				}
				else {
					pData->opState = filetransfer_transfer;
				}
			}
			else {
				if (entry.is_unsure()) {
					pData->opState = filetransfer_waitlist;
				}
				else {
					if (matchedCase) {
						pData->remoteFileSize_ = entry.size;
						if (entry.has_date()) {
							pData->fileTime_ = entry.time;
						}

						if (pData->download_ && !entry.has_time() &&
							engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
						{
							pData->opState = filetransfer_mtime;
						}
						else {
							pData->opState = filetransfer_transfer;
						}
					}
					else {
						pData->opState = filetransfer_mtime;
					}
				}
			}
			if (pData->opState == filetransfer_waitlist) {
				// FIXME
				int res = 0;//List(CServerPath(), L"", LIST_FLAG_REFRESH);
				if (res != FZ_REPLY_OK) {
					return res;
				}
				ResetOperation(FZ_REPLY_INTERNALERROR);
				return FZ_REPLY_ERROR;
			}
			else if (pData->opState == filetransfer_transfer) {
				int res = CheckOverwriteFile();
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
		}
		else {
			pData->tryAbsolutePath_ = true;
			pData->opState = filetransfer_mtime;
		}
	}
	else if (pData->opState == filetransfer_waitlist) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, pData->tryAbsolutePath_ ? pData->remotePath_ : m_CurrentPath, pData->remoteFile_, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist)
					pData->opState = filetransfer_mtime;
				else if (pData->download_ &&
					engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
				{
					pData->opState = filetransfer_mtime;
				}
				else
					pData->opState = filetransfer_transfer;
			}
			else {
				if (matchedCase && !entry.is_unsure()) {
					pData->remoteFileSize_ = entry.size;
					if (!entry.has_date())
						pData->fileTime_ = entry.time;

					if (pData->download_ && !entry.has_time() &&
						engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
					{
						pData->opState = filetransfer_mtime;
					}
					else
						pData->opState = filetransfer_transfer;
				}
				else
					pData->opState = filetransfer_mtime;
			}
			if (pData->opState == filetransfer_transfer) {
				int res = CheckOverwriteFile();
				if (res != FZ_REPLY_OK)
					return res;
			}
		}
		else
			pData->opState = filetransfer_mtime;
	}
	else {
		LogMessage(MessageType::Debug_Warning, L"  Unknown opState (%d)", pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	return SendNextCommand();
}

int CSftpControlSocket::FileTransferSend()
{
	LogMessage(MessageType::Debug_Verbose, L"FileTransferSend()");

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpFileTransferOpData *pData = static_cast<CSftpFileTransferOpData *>(m_pCurOpData);

	if (pData->opState == filetransfer_transfer) {
		std::wstring cmd;
		if (pData->resume_) {
			cmd = L"re";
		}
		if (pData->download_) {
			if (!pData->resume_) {
				CreateLocalDir(pData->localFile_);
			}

			engine_.transfer_status_.Init(pData->remoteFileSize_, pData->resume_ ? pData->localFileSize_ : 0, false);
			cmd += L"get ";
			cmd += QuoteFilename(pData->remotePath_.FormatFilename(pData->remoteFile_, !pData->tryAbsolutePath_)) + L" ";

			std::wstring localFile = QuoteFilename(pData->localFile_);
			std::wstring logstr = cmd;
			logstr += localFile;
			LogMessageRaw(MessageType::Command, logstr);

			if (!AddToStream(cmd) || !AddToStream(localFile + L"\n", true)) {
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
		}
		else {
			engine_.transfer_status_.Init(pData->localFileSize_, pData->resume_ ? pData->remoteFileSize_ : 0, false);
			cmd += L"put ";

			std::wstring logstr = cmd;
			std::wstring localFile = QuoteFilename(pData->localFile_) + L" ";
			std::wstring remoteFile = QuoteFilename(pData->remotePath_.FormatFilename(pData->remoteFile_, !pData->tryAbsolutePath_));

			logstr += localFile;
			logstr += remoteFile;
			LogMessageRaw(MessageType::Command, logstr);

			if (!AddToStream(cmd) || !AddToStream(localFile, true) ||
				!AddToStream(remoteFile + L"\n"))
			{
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
		}
		engine_.transfer_status_.SetStartTime();

		pData->transferInitiated_ = true;
	}
	else if (pData->opState == filetransfer_mtime) {
		std::wstring quotedFilename = QuoteFilename(pData->remotePath_.FormatFilename(pData->remoteFile_, !pData->tryAbsolutePath_));
		if (!SendCommand(L"mtime " + WildcardEscape(quotedFilename),
			L"mtime " + quotedFilename))
			return FZ_REPLY_ERROR;
	}
	else if (pData->opState == filetransfer_chmtime) {
		assert(!pData->fileTime_.empty());
		if (pData->download_) {
			LogMessage(MessageType::Debug_Info, L"  filetransfer_chmtime during download");
			ResetOperation(FZ_REPLY_INTERNALERROR);
			return FZ_REPLY_ERROR;
		}

		std::wstring quotedFilename = QuoteFilename(pData->remotePath_.FormatFilename(pData->remoteFile_, !pData->tryAbsolutePath_));

		fz::datetime t = pData->fileTime_;
		t -= fz::duration::from_minutes(currentServer_.GetTimezoneOffset());

		// Y2K38
		time_t ticks = t.get_time_t();
		std::wstring seconds = fz::sprintf(L"%d", ticks);
		if (!SendCommand(L"chmtime " + seconds + L" " + WildcardEscape(quotedFilename),
			L"chmtime " + seconds + L" " + quotedFilename))
			return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpControlSocket::FileTransferParseResponse(int result, std::wstring const& reply)
{
	LogMessage(MessageType::Debug_Verbose, L"FileTransferParseResponse(%d)", result);

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpFileTransferOpData *pData = static_cast<CSftpFileTransferOpData *>(m_pCurOpData);

	if (pData->opState == filetransfer_transfer) {
		if (result != FZ_REPLY_OK) {
			ResetOperation(result);
			return FZ_REPLY_ERROR;
		}

		if (engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS)) {
			if (pData->download_) {
				if (!pData->fileTime_.empty()) {
					if (!fz::local_filesys::set_modification_time(fz::to_native(pData->localFile_), pData->fileTime_))
						LogMessage(MessageType::Debug_Warning, L"Could not set modification time");
				}
			}
			else {
				pData->fileTime_ = fz::local_filesys::get_modification_time(fz::to_native(pData->localFile_));
				if (!pData->fileTime_.empty()) {
					pData->opState = filetransfer_chmtime;
					return SendNextCommand();
				}
			}
		}
	}
	else if (pData->opState == filetransfer_mtime) {
		if (result == FZ_REPLY_OK && !reply.empty()) {
			time_t seconds = 0;
			bool parsed = true;
			for (auto c : reply) {
				if (c < '0' || c > '9') {
					parsed = false;
					break;
				}
				seconds *= 10;
				seconds += c - '0';
			}
			if (parsed) {
				fz::datetime fileTime = fz::datetime(seconds, fz::datetime::seconds);
				if (!fileTime.empty()) {
					pData->fileTime_ = fileTime;
					pData->fileTime_ += fz::duration::from_minutes(currentServer_.GetTimezoneOffset());
				}
			}
		}
		pData->opState = filetransfer_transfer;
		int res = CheckOverwriteFile();
		if (res != FZ_REPLY_OK) {
			return res;
		}

		return SendNextCommand();
	}
	else if (pData->opState == filetransfer_chmtime) {
		if (pData->download_) {
			LogMessage(MessageType::Debug_Info, L"  filetransfer_chmtime during download");
			return FZ_REPLY_INTERNALERROR;
		}
	}
	else {
		LogMessage(MessageType::Debug_Info, L"  Called at improper time: opState == %d", pData->opState);
		return FZ_REPLY_INTERNALERROR;
	}

	return FZ_REPLY_OK;
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

enum mkdStates
{
	mkd_init = 0,
	mkd_findparent,
	mkd_mkdsub,
	mkd_cwdsub,
	mkd_tryfull
};

int CSftpControlSocket::Mkdir(const CServerPath& path)
{
	/* Directory creation works like this: First find a parent directory into
	 * which we can CWD, then create the subdirs one by one. If either part
	 * fails, try MKD with the full path directly.
	 */

	if (!m_pCurOpData) {
		LogMessage(MessageType::Status, _("Creating directory '%s'..."), path.GetPath());
	}

	CMkdirOpData *pData = new CMkdirOpData;
	pData->path = path;

	if (!m_CurrentPath.empty()) {
		// Unless the server is broken, a directory already exists if current directory is a subdir of it.
		if (m_CurrentPath == path || m_CurrentPath.IsSubdirOf(path, false)) {
			delete pData;
			return FZ_REPLY_OK;
		}

		if (m_CurrentPath.IsParentOf(path, false)) {
			pData->commonParent = m_CurrentPath;
		}
		else {
			pData->commonParent = path.GetCommonParent(m_CurrentPath);
		}
	}

	if (!path.HasParent()) {
		pData->opState = mkd_tryfull;
	}
	else {
		pData->currentPath = path.GetParent();
		pData->segments.push_back(path.GetLastSegment());

		if (pData->currentPath == m_CurrentPath) {
			pData->opState = mkd_mkdsub;
		}
		else {
			pData->opState = mkd_findparent;
		}
	}

	Push(pData);

	return SendNextCommand();
}

int CSftpControlSocket::MkdirParseResponse(bool successful, std::wstring const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::MkdirParseResonse");

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CMkdirOpData *pData = static_cast<CMkdirOpData *>(m_pCurOpData);
	LogMessage(MessageType::Debug_Debug, L"  state = %d", pData->opState);

	bool error = false;
	switch (pData->opState)
	{
	case mkd_findparent:
		if (successful) {
			m_CurrentPath = pData->currentPath;
			pData->opState = mkd_mkdsub;
		}
		else if (pData->currentPath == pData->commonParent) {
			pData->opState = mkd_tryfull;
		}
		else if (pData->currentPath.HasParent()) {
			pData->segments.push_back(pData->currentPath.GetLastSegment());
			pData->currentPath = pData->currentPath.GetParent();
		}
		else {
			pData->opState = mkd_tryfull;
		}
		break;
	case mkd_mkdsub:
		if (successful) {
			if (pData->segments.empty()) {
				LogMessage(MessageType::Debug_Warning, L"  pData->segments is empty");
				ResetOperation(FZ_REPLY_INTERNALERROR);
				return FZ_REPLY_ERROR;
			}
			engine_.GetDirectoryCache().UpdateFile(currentServer_, pData->currentPath, pData->segments.back(), true, CDirectoryCache::dir);
			SendDirectoryListingNotification(pData->currentPath, false, false);

			pData->currentPath.AddSegment(pData->segments.back());
			pData->segments.pop_back();

			if (pData->segments.empty()) {
				ResetOperation(FZ_REPLY_OK);
				return FZ_REPLY_OK;
			}
			else {
				pData->opState = mkd_cwdsub;
			}
		}
		else {
			pData->opState = mkd_tryfull;
		}
		break;
	case mkd_cwdsub:
		if (successful) {
			m_CurrentPath = pData->currentPath;
			pData->opState = mkd_mkdsub;
		}
		else {
			pData->opState = mkd_tryfull;
		}
		break;
	case mkd_tryfull:
		if (!successful) {
			error = true;
		}
		else {
			ResetOperation(FZ_REPLY_OK);
			return FZ_REPLY_OK;
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (error) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	return MkdirSend();
}

int CSftpControlSocket::MkdirSend()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::MkdirSend");

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CMkdirOpData *pData = static_cast<CMkdirOpData *>(m_pCurOpData);
	LogMessage(MessageType::Debug_Debug, L"  state = %d", pData->opState);

	if (!pData->holdsLock_) {
		if (!TryLockCache(lock_mkdir, pData->path)) {
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	bool res;
	switch (pData->opState)
	{
	case mkd_findparent:
	case mkd_cwdsub:
		m_CurrentPath.clear();
		res = SendCommand(L"cd " + QuoteFilename(pData->currentPath.GetPath()));
		break;
	case mkd_mkdsub:
		res = SendCommand(L"mkdir " + QuoteFilename(pData->segments.back()));
		break;
	case mkd_tryfull:
		res = SendCommand(L"mkdir " + QuoteFilename(pData->path.GetPath()));
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!res) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

std::wstring CSftpControlSocket::QuoteFilename(std::wstring const& filename)
{
	return L"\"" + fz::replaced_substrings(filename, L"\"", L"\"\"") + L"\"";
}

int CSftpControlSocket::Delete(const CServerPath& path, std::deque<std::wstring>&& files)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::Delete");
	assert(!m_pCurOpData);
	CSftpDeleteOpData *pData = new CSftpDeleteOpData();
	Push(pData);
	pData->path = path;
	pData->files = files;

	// CFileZillaEnginePrivate should have checked this already
	assert(!files.empty());

	return SendNextCommand();
}

int CSftpControlSocket::DeleteParseResponse(bool successful, std::wstring const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::DeleteParseResponse");

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpDeleteOpData *pData = static_cast<CSftpDeleteOpData *>(m_pCurOpData);

	if (!successful) {
		pData->m_deleteFailed = true;
	}
	else {
		std::wstring const& file = pData->files.front();

		engine_.GetDirectoryCache().RemoveFile(currentServer_, pData->path, file);

		auto const now = fz::datetime::now();
		if (!pData->m_time.empty() && (now - pData->m_time).get_seconds() >= 1) {
			SendDirectoryListingNotification(pData->path, false, false);
			pData->m_time = now;
			pData->m_needSendListing = false;
		}
		else
			pData->m_needSendListing = true;
	}

	pData->files.pop_front();

	if (!pData->files.empty())
		return SendNextCommand();

	return ResetOperation(pData->m_deleteFailed ? FZ_REPLY_ERROR : FZ_REPLY_OK);
}

int CSftpControlSocket::DeleteSend()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::DeleteSend");

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}
	CSftpDeleteOpData *pData = static_cast<CSftpDeleteOpData *>(m_pCurOpData);

	std::wstring const& file = pData->files.front();
	if (file.empty()) {
		LogMessage(MessageType::Debug_Info, L"Empty filename");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	std::wstring filename = pData->path.FormatFilename(file);
	if (filename.empty()) {
		LogMessage(MessageType::Error, _("Filename cannot be constructed for directory %s and filename %s"), pData->path.GetPath(), file);
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	if (pData->m_time.empty()) {
		pData->m_time = fz::datetime::now();
	}

	engine_.GetDirectoryCache().InvalidateFile(currentServer_, pData->path, file);

	if (!SendCommand(L"rm " + WildcardEscape(QuoteFilename(filename)),
			  L"rm " + QuoteFilename(filename)))
	{
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

class CSftpRemoveDirOpData final : public COpData
{
public:
	CSftpRemoveDirOpData()
		: COpData(Command::removedir)
	{
	}

	CServerPath path;
	std::wstring subDir;
};

int CSftpControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::RemoveDir");

	assert(!m_pCurOpData);
	CSftpRemoveDirOpData *pData = new CSftpRemoveDirOpData();
	Push(pData);
	pData->path = path;
	pData->subDir = subDir;

	CServerPath fullPath = engine_.GetPathCache().Lookup(currentServer_, pData->path, pData->subDir);
	if (fullPath.empty()) {
		fullPath = pData->path;

		if (!fullPath.AddSegment(subDir)) {
			LogMessage(MessageType::Error, _("Path cannot be constructed for directory %s and subdir %s"), path.GetPath(), subDir);
			return FZ_REPLY_ERROR;
		}
	}

	engine_.GetDirectoryCache().InvalidateFile(currentServer_, path, subDir);

	engine_.GetPathCache().InvalidatePath(currentServer_, pData->path, pData->subDir);

	engine_.InvalidateCurrentWorkingDirs(fullPath);
	std::wstring quotedFilename = QuoteFilename(fullPath.GetPath());
	if (!SendCommand(L"rmdir " + WildcardEscape(quotedFilename),
			  L"rmdir " + quotedFilename))
		return FZ_REPLY_ERROR;

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpControlSocket::RemoveDirParseResponse(bool successful, std::wstring const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::RemoveDirParseResponse");

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!successful) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpRemoveDirOpData *pData = static_cast<CSftpRemoveDirOpData *>(m_pCurOpData);
	if (pData->path.empty())
	{
		LogMessage(MessageType::Debug_Info, L"Empty pData->path");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	engine_.GetDirectoryCache().RemoveDir(currentServer_, pData->path, pData->subDir, engine_.GetPathCache().Lookup(currentServer_, pData->path, pData->subDir));
	SendDirectoryListingNotification(pData->path, false, false);

	return ResetOperation(FZ_REPLY_OK);
}

class CSftpChmodOpData final : public COpData
{
public:
	CSftpChmodOpData(const CChmodCommand& command)
		: COpData(Command::chmod), m_cmd(command)
	{
	}

	CChmodCommand m_cmd;
	bool m_useAbsolute{};
};

enum chmodStates
{
	chmod_init = 0,
	chmod_chmod
};

int CSftpControlSocket::Chmod(const CChmodCommand& command)
{
	if (m_pCurOpData)
	{
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData not empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	LogMessage(MessageType::Status, _("Set permissions of '%s' to '%s'"), command.GetPath().FormatFilename(command.GetFile()), command.GetPermission());

	CSftpChmodOpData *pData = new CSftpChmodOpData(command);
	pData->opState = chmod_chmod;
	Push(pData);

	ChangeDir(command.GetPath());
	return FZ_REPLY_CONTINUE;
}

int CSftpControlSocket::ChmodParseResponse(bool successful, std::wstring const&)
{
	CSftpChmodOpData *pData = static_cast<CSftpChmodOpData*>(m_pCurOpData);
	if (!pData) {
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!successful) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	ResetOperation(FZ_REPLY_OK);
	return FZ_REPLY_OK;
}

int CSftpControlSocket::ChmodSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::ChmodSend()");

	CSftpChmodOpData *pData = static_cast<CSftpChmodOpData*>(m_pCurOpData);
	if (!pData)
	{
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (prevResult != FZ_REPLY_OK)
		pData->m_useAbsolute = true;

	return SendNextCommand();
}

int CSftpControlSocket::ChmodSend()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::ChmodSend()");

	CSftpChmodOpData *pData = static_cast<CSftpChmodOpData*>(m_pCurOpData);
	if (!pData) {
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	bool res;
	switch (pData->opState)
	{
	case chmod_chmod:
		{
			engine_.GetDirectoryCache().UpdateFile(currentServer_, pData->m_cmd.GetPath(), pData->m_cmd.GetFile(), false, CDirectoryCache::unknown);

			std::wstring quotedFilename = QuoteFilename(pData->m_cmd.GetPath().FormatFilename(pData->m_cmd.GetFile(), !pData->m_useAbsolute));

			res = SendCommand(L"chmod " + pData->m_cmd.GetPermission() + L" " + WildcardEscape(quotedFilename),
					   L"chmod " + pData->m_cmd.GetPermission() + L" " + quotedFilename);
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!res) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

class CSftpRenameOpData final : public COpData
{
public:
	CSftpRenameOpData(const CRenameCommand& command)
		: COpData(Command::rename), m_cmd(command)
	{
	}

	CRenameCommand m_cmd;
	bool m_useAbsolute{};
};

enum renameStates
{
	rename_init = 0,
	rename_rename
};

int CSftpControlSocket::Rename(const CRenameCommand& command)
{
	if (m_pCurOpData) {
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData not empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	LogMessage(MessageType::Status, _("Renaming '%s' to '%s'"), command.GetFromPath().FormatFilename(command.GetFromFile()), command.GetToPath().FormatFilename(command.GetToFile()));

	CSftpRenameOpData *pData = new CSftpRenameOpData(command);
	pData->opState = rename_rename;
	Push(pData);

	ChangeDir(command.GetFromPath());
	return FZ_REPLY_CONTINUE;
}

int CSftpControlSocket::RenameParseResponse(bool successful, std::wstring const&)
{
	CSftpRenameOpData *pData = static_cast<CSftpRenameOpData*>(m_pCurOpData);
	if (!pData) {
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!successful) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	const CServerPath& fromPath = pData->m_cmd.GetFromPath();
	const CServerPath& toPath = pData->m_cmd.GetToPath();

	engine_.GetDirectoryCache().Rename(currentServer_, fromPath, pData->m_cmd.GetFromFile(), toPath, pData->m_cmd.GetToFile());

	SendDirectoryListingNotification(fromPath, false, false);
	if (fromPath != toPath) {
		SendDirectoryListingNotification(toPath, false, false);
	}

	ResetOperation(FZ_REPLY_OK);
	return FZ_REPLY_OK;
}

int CSftpControlSocket::RenameSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::RenameSubcommandResult()");

	CSftpRenameOpData *pData = static_cast<CSftpRenameOpData*>(m_pCurOpData);
	if (!pData) {
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (prevResult != FZ_REPLY_OK) {
		pData->m_useAbsolute = true;
	}

	return SendNextCommand();
}

int CSftpControlSocket::RenameSend()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpControlSocket::RenameSend()");

	CSftpRenameOpData *pData = static_cast<CSftpRenameOpData*>(m_pCurOpData);
	if (!pData) {
		LogMessage(MessageType::Debug_Warning, L"m_pCurOpData empty");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	bool res;
	switch (pData->opState)
	{
	case rename_rename:
		{
			bool wasDir = false;
			engine_.GetDirectoryCache().InvalidateFile(currentServer_, pData->m_cmd.GetFromPath(), pData->m_cmd.GetFromFile(), &wasDir);
			engine_.GetDirectoryCache().InvalidateFile(currentServer_, pData->m_cmd.GetToPath(), pData->m_cmd.GetToFile());

			std::wstring fromQuoted = QuoteFilename(pData->m_cmd.GetFromPath().FormatFilename(pData->m_cmd.GetFromFile(), !pData->m_useAbsolute));
			std::wstring toQuoted = QuoteFilename(pData->m_cmd.GetToPath().FormatFilename(pData->m_cmd.GetToFile(), !pData->m_useAbsolute && pData->m_cmd.GetFromPath() == pData->m_cmd.GetToPath()));

			engine_.GetPathCache().InvalidatePath(currentServer_, pData->m_cmd.GetFromPath(), pData->m_cmd.GetFromFile());
			engine_.GetPathCache().InvalidatePath(currentServer_, pData->m_cmd.GetToPath(), pData->m_cmd.GetToFile());

			if (wasDir) {
				// Need to invalidate current working directories
				CServerPath path = engine_.GetPathCache().Lookup(currentServer_, pData->m_cmd.GetFromPath(), pData->m_cmd.GetFromFile());
				if (path.empty()) {
					path = pData->m_cmd.GetFromPath();
					path.AddSegment(pData->m_cmd.GetFromFile());
				}
				engine_.InvalidateCurrentWorkingDirs(path);
			}

			res = SendCommand(L"mv " + WildcardEscape(fromQuoted) + L" " + toQuoted,
					   L"mv " + fromQuoted + L" " + toQuoted);
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!res) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
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
		if (bytes > INT_MAX) {
			b = INT_MAX;
		}
		else {
			b = bytes;
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
