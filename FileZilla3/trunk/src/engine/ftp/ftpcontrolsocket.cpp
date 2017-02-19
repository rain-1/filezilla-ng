#include <filezilla.h>

#include "cwd.h"
#include "chmod.h"
#include "delete.h"
#include "directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "externalipresolver.h"
#include "filetransfer.h"
#include "ftpcontrolsocket.h"
#include "iothread.h"
#include "list.h"
#include "logon.h"
#include "mkd.h"
#include "pathcache.h"
#include "proxy.h"
#include "rawcommand.h"
#include "rawtransfer.h"
#include "rename.h"
#include "rmd.h"
#include "servercapabilities.h"
#include "tlssocket.h"
#include "transfersocket.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/util.hpp>

#include <algorithm>

CFtpControlSocket::CFtpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
	// Enable TCP_NODELAY, speeds things up a bit.
	m_pSocket->SetFlags(CSocket::flag_nodelay | CSocket::flag_keepalive);

	// Enable SO_KEEPALIVE, lots of clueless users have broken routers and
	// firewalls which terminate the control connection on long transfers.
	int v = engine_.GetOptions().GetOptionVal(OPTION_TCP_KEEPALIVE_INTERVAL);
	if (v >= 1 && v < 10000) {
		m_pSocket->SetKeepaliveInterval(fz::duration::from_minutes(v));
	}
}

CFtpControlSocket::~CFtpControlSocket()
{
	remove_handler();

	DoClose();
}

void CFtpControlSocket::OnReceive()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::OnReceive()");

	for (;;) {
		int error;
		int read = m_pBackend->Read(m_receiveBuffer + m_bufferLen, RECVBUFFERSIZE - m_bufferLen, error);

		if (read < 0) {
			if (error != EAGAIN) {
				LogMessage(MessageType::Error, _("Could not read from socket: %s"), CSocket::GetErrorDescription(error));
				if (GetCurrentCommandId() != Command::connect) {
					LogMessage(MessageType::Error, _("Disconnected from server"));
				}
				DoClose();
			}
			return;
		}

		if (!read) {
			auto messageType = (GetCurrentCommandId() == Command::none) ? MessageType::Status : MessageType::Error;
			LogMessage(messageType, _("Connection closed by server"));
			DoClose();
			return;
		}

		SetActive(CFileZillaEngine::recv);

		char* start = m_receiveBuffer;
		m_bufferLen += read;

		for (int i = start - m_receiveBuffer; i < m_bufferLen; ++i) {
			char& p = m_receiveBuffer[i];
			if (p == '\r' ||
				p == '\n' ||
				p == 0)
			{
				int len = i - (start - m_receiveBuffer);
				if (!len) {
					++start;
					continue;
				}

				p = 0;
				std::wstring line = ConvToLocal(start, i + 1 - (start - m_receiveBuffer));
				start = m_receiveBuffer + i + 1;

				ParseLine(line);

				// Abort if connection got closed
				if (!currentServer_) {
					return;
				}
			}
		}
		memmove(m_receiveBuffer, start, m_bufferLen - (start - m_receiveBuffer));
		m_bufferLen -= (start -m_receiveBuffer);
		if (m_bufferLen > MAXLINELEN) {
			m_bufferLen = MAXLINELEN;
		}
	}
}

void CFtpControlSocket::ParseLine(std::wstring line)
{
	m_rtt.Stop();
	LogMessageRaw(MessageType::Response, line);
	SetAlive();

	if (m_pCurOpData && m_pCurOpData->opId == Command::connect) {
		CFtpLogonOpData* pData = static_cast<CFtpLogonOpData *>(m_pCurOpData);
		if (pData->waitChallenge) {
			std::wstring& challenge = pData->challenge;
			if (!challenge.empty())
#ifdef FZ_WINDOWS
				challenge += L"\r\n";
#else
				challenge += L"\n";
#endif
			challenge += line;
		}
		else if (pData->opState == LOGON_FEAT) {
			pData->ParseFeat(line);
		}
		else if (pData->opState == LOGON_WELCOME) {
			if (!pData->gotFirstWelcomeLine) {
				if (fz::str_tolower_ascii(line).substr(0, 3) == L"ssh") {
					LogMessage(MessageType::Error, _("Cannot establish FTP connection to an SFTP server. Please select proper protocol."));
					DoClose(FZ_REPLY_CRITICALERROR);
					return;
				}
				pData->gotFirstWelcomeLine = true;
			}
		}
	}
	//Check for multi-line responses
	if (line.size() > 3) {
		if (!m_MultilineResponseCode.empty()) {
			if (line.substr(0, 4) == m_MultilineResponseCode) {
				// end of multi-line found
				m_MultilineResponseCode.clear();
				m_Response = line;
				ParseResponse();
				m_Response.clear();
				m_MultilineResponseLines.clear();
			}
			else {
				m_MultilineResponseLines.push_back(line);
			}
		}
		// start of new multi-line
		else if (line[3] == '-') {
			// DDD<SP> is the end of a multi-line response
			m_MultilineResponseCode = line.substr(0, 3) + L" ";
			m_MultilineResponseLines.push_back(line);
		}
		else {
			m_Response = line;
			ParseResponse();
			m_Response.clear();
		}
	}
}

void CFtpControlSocket::OnConnect()
{
	m_lastTypeBinary = -1;

	SetAlive();

	if (currentServer_.GetProtocol() == FTPS) {
		if (!m_pTlsSocket) {
			LogMessage(MessageType::Status, _("Connection established, initializing TLS..."));

			assert(!m_pTlsSocket);
			delete m_pBackend;
			m_pTlsSocket = new CTlsSocket(this, *m_pSocket, this);
			m_pBackend = m_pTlsSocket;

			if (!m_pTlsSocket->Init()) {
				LogMessage(MessageType::Error, _("Failed to initialize TLS."));
				DoClose();
				return;
			}

			int res = m_pTlsSocket->Handshake();
			if (res == FZ_REPLY_ERROR) {
				DoClose();
			}

			return;
		}
		else {
			LogMessage(MessageType::Status, _("TLS connection established, waiting for welcome message..."));
		}
	}
	else if ((currentServer_.GetProtocol() == FTPES || currentServer_.GetProtocol() == FTP) && m_pTlsSocket) {
		LogMessage(MessageType::Status, _("TLS connection established."));
		SendNextCommand();
		return;
	}
	else {
		LogMessage(MessageType::Status, _("Connection established, waiting for welcome message..."));
	}
	m_pendingReplies = 1;
	m_repliesToSkip = 0;
}

void CFtpControlSocket::ParseResponse()
{
	if (m_Response.empty()) {
		LogMessage(MessageType::Debug_Warning, L"No reply in ParseResponse");
		return;
	}

	if (m_Response[0] != '1') {
		if (m_pendingReplies > 0) {
			m_pendingReplies--;
		}
		else {
			LogMessage(MessageType::Debug_Warning, L"Unexpected reply, no reply was pending.");
			return;
		}
	}

	if (m_repliesToSkip) {
		LogMessage(MessageType::Debug_Info, L"Skipping reply after cancelled operation or keepalive command.");
		if (m_Response[0] != '1') {
			--m_repliesToSkip;
		}

		if (!m_repliesToSkip) {
			SetWait(false);
			if (!m_pCurOpData) {
				StartKeepaliveTimer();
			}
			else if (!m_pendingReplies) {
				SendNextCommand();
			}
		}

		return;
	}

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

int CFtpControlSocket::GetReplyCode() const
{
	if (m_Response.empty()) {
		return 0;
	}
	else if (m_Response[0] < '0' || m_Response[0] > '9') {
		return 0;
	}
	else {
		return m_Response[0] - '0';
	}
}

int CFtpControlSocket::SendCommand(std::wstring const& str, bool maskArgs, bool measureRTT)
{
	size_t pos;
	if (maskArgs && (pos = str.find(' ')) != std::wstring::npos) {
		std::wstring stars(str.size() - pos - 1, '*');
		LogMessageRaw(MessageType::Command, str.substr(0, pos + 1) + stars);
	}
	else {
		LogMessageRaw(MessageType::Command, str);
	}

	std::string buffer = ConvToServer(str);
	if (buffer.empty()) {
		LogMessage(MessageType::Error, _("Failed to convert command to 8 bit charset"));
		return FZ_REPLY_ERROR;
	}
	buffer += "\r\n";
	bool res = CRealControlSocket::Send(buffer.c_str(), buffer.size());
	if (res) {
		++m_pendingReplies;
	}

	if (measureRTT) {
		m_rtt.Start();
	}

	return res ? FZ_REPLY_WOULDBLOCK : FZ_REPLY_ERROR;
}

void CFtpControlSocket::List(CServerPath const& path, std::wstring const& subDir, int flags)
{
	if (m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"List called from other command");
	}

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

	CFtpListOpData *pData = new CFtpListOpData(*this, path, subDir, flags);
	Push(pData);
}

int CFtpControlSocket::ResetOperation(int nErrorCode)
{
 	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::ResetOperation(%d)", nErrorCode);

	m_pTransferSocket.reset();
	m_pIPResolver.reset();

	m_repliesToSkip = m_pendingReplies;

	if (m_pCurOpData && m_pCurOpData->opId == Command::transfer) {
		CFtpFileTransferOpData *pData = static_cast<CFtpFileTransferOpData *>(m_pCurOpData);
		if (pData->tranferCommandSent) {
			if (pData->transferEndReason == TransferEndReason::transfer_failure_critical) {
				nErrorCode |= FZ_REPLY_CRITICALERROR | FZ_REPLY_WRITEFAILED;
			}
			if (pData->transferEndReason != TransferEndReason::transfer_command_failure_immediate || GetReplyCode() != 5) {
				pData->transferInitiated_ = true;
			}
			else {
				if (nErrorCode == FZ_REPLY_ERROR) {
					nErrorCode |= FZ_REPLY_CRITICALERROR;
				}
			}
		}
		if (nErrorCode != FZ_REPLY_OK && pData->download_ && !pData->fileDidExist_) {
			pData->ioThread_.reset();
			int64_t size;
			bool isLink;
			if (fz::local_filesys::get_file_info(fz::to_native(pData->localFile_), isLink, &size, 0, 0) == fz::local_filesys::file && size == 0) {
				// Download failed and a new local file was created before, but
				// nothing has been written to it. Remove it again, so we don't
				// leave a bunch of empty files all over the place.
				LogMessage(MessageType::Debug_Verbose, L"Deleting empty file");
				fz::remove_file(fz::to_native(pData->localFile_));
			}
		}
	}
	if (m_pCurOpData && m_pCurOpData->opId == Command::del && !(nErrorCode & FZ_REPLY_DISCONNECTED)) {
		CFtpDeleteOpData *pData = static_cast<CFtpDeleteOpData *>(m_pCurOpData);
		if (pData->needSendListing_) {
			SendDirectoryListingNotification(pData->path_, false, false);
		}
	}

	if (m_pCurOpData && m_pCurOpData->opId == PrivCommand::rawtransfer &&
		nErrorCode != FZ_REPLY_OK)
	{
		CFtpRawTransferOpData *pData = static_cast<CFtpRawTransferOpData *>(m_pCurOpData);
		if (pData->pOldData->transferEndReason == TransferEndReason::successful) {
			if ((nErrorCode & FZ_REPLY_TIMEOUT) == FZ_REPLY_TIMEOUT) {
				pData->pOldData->transferEndReason = TransferEndReason::timeout;
			}
			else if (!pData->pOldData->tranferCommandSent) {
				pData->pOldData->transferEndReason = TransferEndReason::pre_transfer_command_failure;
			}
			else {
				pData->pOldData->transferEndReason = TransferEndReason::failure;
			}
		}
	}

	m_lastCommandCompletionTime = fz::monotonic_clock::now();
	if (m_pCurOpData && !(nErrorCode & FZ_REPLY_DISCONNECTED)) {
		StartKeepaliveTimer();
	}
	else {
		stop_timer(m_idleTimer);
		m_idleTimer = 0;
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

bool CFtpControlSocket::CanSendNextCommand() const
{
	if (m_repliesToSkip) {
		LogMessage(MessageType::Status, L"Waiting for replies to skip before sending next command...");
		return false;
	}

	return true;
}

void CFtpControlSocket::ChangeDir(CServerPath const& path, std::wstring const& subDir, bool link_discovery)
{
	CFtpChangeDirOpData *pData = new CFtpChangeDirOpData(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->link_discovery_ = link_discovery;

	if (pData->pNextOpData && pData->pNextOpData->opId == Command::transfer &&
		!static_cast<CFtpFileTransferOpData *>(pData->pNextOpData)->download_)
	{
		pData->tryMkdOnFail_ = true;
		assert(subDir.empty());
	}

	Push(pData);
}

void CFtpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const& transferSettings)
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::FileTransfer()");

	CFtpFileTransferOpData *pData = new CFtpFileTransferOpData(*this, download, localFile, remoteFile, remotePath);
	pData->transferSettings_ = transferSettings;
	pData->binary = transferSettings.binary;
	Push(pData);
}

void CFtpControlSocket::TransferEnd()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::TransferEnd()");

	// If m_pTransferSocket is zero, the message was sent by the previous command.
	// We can safely ignore it.
	// It does not cause problems, since before creating the next transfer socket, other
	// messages which were added to the queue later than this one will be processed first.
	if (!m_pCurOpData || !m_pTransferSocket || GetCurrentCommandId() != PrivCommand::rawtransfer) {
		LogMessage(MessageType::Debug_Verbose, L"Call to TransferEnd at unusual time, ignoring");
		return;
	}

	TransferEndReason reason = m_pTransferSocket->GetTransferEndreason();
	if (reason == TransferEndReason::none) {
		LogMessage(MessageType::Debug_Info, L"Call to TransferEnd at unusual time");
		return;
	}

	if (reason == TransferEndReason::successful) {
		SetAlive();
	}

	CFtpRawTransferOpData *pData = static_cast<CFtpRawTransferOpData *>(m_pCurOpData);
	if (pData->pOldData->transferEndReason == TransferEndReason::successful) {
		pData->pOldData->transferEndReason = reason;
	}

	switch (m_pCurOpData->opState)
	{
	case rawtransfer_transfer:
		pData->opState = rawtransfer_waittransferpre;
		break;
	case rawtransfer_waitfinish:
		pData->opState = rawtransfer_waittransfer;
		break;
	case rawtransfer_waitsocket:
		ResetOperation((reason == TransferEndReason::successful) ? FZ_REPLY_OK : FZ_REPLY_ERROR);
		break;
	default:
		LogMessage(MessageType::Debug_Info, L"TransferEnd at unusual op state %d, ignoring", m_pCurOpData->opState);
		break;
	}
}

bool CFtpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (m_pCurOpData) {
		if (!m_pCurOpData->waitForAsyncRequest) {
			LogMessage(MessageType::Debug_Info, L"Not waiting for request reply, ignoring request reply %d", pNotification->GetRequestID());
			return false;
		}
		m_pCurOpData->waitForAsyncRequest = false;
	}

	const RequestId requestId = pNotification->GetRequestID();
	switch (requestId)
	{
	case reqId_fileexists:
		{
			if (!m_pCurOpData || m_pCurOpData->opId != Command::transfer) {
				LogMessage(MessageType::Debug_Info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
		break;
	case reqId_interactiveLogin:
		{
			if (!m_pCurOpData || m_pCurOpData->opId != Command::connect) {
				LogMessage(MessageType::Debug_Info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			CFtpLogonOpData* pData = static_cast<CFtpLogonOpData*>(m_pCurOpData);

			CInteractiveLoginNotification *pInteractiveLoginNotification = static_cast<CInteractiveLoginNotification *>(pNotification);
			if (!pInteractiveLoginNotification->passwordSet) {
				ResetOperation(FZ_REPLY_CANCELED);
				return false;
			}
			currentServer_.SetUser(currentServer_.GetUser(), pInteractiveLoginNotification->server.GetPass());
			pData->gotPassword = true;
			SendNextCommand();
		}
		break;
	case reqId_certificate:
		{
			if (!m_pTlsSocket || m_pTlsSocket->GetState() != CTlsSocket::TlsState::verifycert) {
				LogMessage(MessageType::Debug_Info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			CCertificateNotification* pCertificateNotification = static_cast<CCertificateNotification *>(pNotification);
			m_pTlsSocket->TrustCurrentCert(pCertificateNotification->m_trusted);

			if (!pCertificateNotification->m_trusted) {
				DoClose(FZ_REPLY_CRITICALERROR);
				return false;
			}

			if (m_pCurOpData && m_pCurOpData->opId == Command::connect &&
				m_pCurOpData->opState == LOGON_AUTH_WAIT)
			{
				m_pCurOpData->opState = LOGON_LOGON;
			}
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown request %d", pNotification->GetRequestID());
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}

void CFtpControlSocket::RawCommand(std::wstring const& command)
{
	assert(!command.empty());

	Push(new CFtpRawCommandOpData(*this, command));
}

void CFtpControlSocket::Delete(CServerPath const& path, std::deque<std::wstring>&& files)
{
	assert(!m_pCurOpData);
	CFtpDeleteOpData *pData = new CFtpDeleteOpData(*this);
	Push(pData);
	pData->path_ = path;
	pData->files_ = files;
	pData->omitPath_ = true;

	ChangeDir(pData->path_);
}

void CFtpControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	assert(!m_pCurOpData);
	CFtpRemoveDirOpData *pData = new CFtpRemoveDirOpData(*this);
	Push(pData);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->omitPath_ = true;
	pData->fullPath_ = path;

	ChangeDir(pData->path_);
}

void CFtpControlSocket::Mkdir(CServerPath const& path)
{
	/* Directory creation works like this: First find a parent directory into
	 * which we can CWD, then create the subdirs one by one. If either part
	 * fails, try MKD with the full path directly.
	 */

	if (!m_pCurOpData && !path.empty()) {
		LogMessage(MessageType::Status, _("Creating directory '%s'..."), path.GetPath());
	}

	CMkdirOpData *pData = new CMkdirOpData;
	pData->path_ = path;

	Push(pData);
}

int CFtpControlSocket::Rename(CRenameCommand const& command)
{
	LogMessage(MessageType::Status, _("Renaming '%s' to '%s'"), command.GetFromPath().FormatFilename(command.GetFromFile()), command.GetToPath().FormatFilename(command.GetToFile()));

	Push(new CFtpRenameOpData(*this, command));

	ChangeDir(command.GetFromPath());
	return FZ_REPLY_CONTINUE;
}

int CFtpControlSocket::Chmod(CChmodCommand const& command)
{
	LogMessage(MessageType::Status, _("Setting permissions of '%s' to '%s'"), command.GetPath().FormatFilename(command.GetFile()), command.GetPermission());

	Push(new CFtpChmodOpData(*this, command));

	ChangeDir(command.GetPath());
	return FZ_REPLY_CONTINUE;
}

int CFtpControlSocket::GetExternalIPAddress(std::string& address)
{
	// Local IP should work. Only a complete moron would use IPv6
	// and NAT at the same time.
	if (m_pSocket->GetAddressFamily() != CSocket::ipv6) {
		int mode = engine_.GetOptions().GetOptionVal(OPTION_EXTERNALIPMODE);

		if (mode) {
			if (engine_.GetOptions().GetOptionVal(OPTION_NOEXTERNALONLOCAL) &&
				!fz::is_routable_address(m_pSocket->GetPeerIP()))
				// Skip next block, use local address
				goto getLocalIP;
		}

		if (mode == 1) {
			std::wstring ip = engine_.GetOptions().GetOption(OPTION_EXTERNALIP);
			if (!ip.empty()) {
				address = fz::to_string(ip);
				return FZ_REPLY_OK;
			}

			LogMessage(MessageType::Debug_Warning, _("No external IP address set, trying default."));
		}
		else if (mode == 2) {
			if (!m_pIPResolver) {
				std::string localAddress = m_pSocket->GetLocalIP(true);

				if (!localAddress.empty() && localAddress == fz::to_string(engine_.GetOptions().GetOption(OPTION_LASTRESOLVEDIP))) {
					LogMessage(MessageType::Debug_Verbose, L"Using cached external IP address");

					address = localAddress;
					return FZ_REPLY_OK;
				}

				std::wstring resolverAddress = engine_.GetOptions().GetOption(OPTION_EXTERNALIPRESOLVER);

				LogMessage(MessageType::Debug_Info, _("Retrieving external IP address from %s"), resolverAddress);

				m_pIPResolver = std::make_unique<CExternalIPResolver>(engine_.GetThreadPool(), *this);
				m_pIPResolver->GetExternalIP(resolverAddress, CSocket::ipv4);
				if (!m_pIPResolver->Done()) {
					LogMessage(MessageType::Debug_Verbose, L"Waiting for resolver thread");
					return FZ_REPLY_WOULDBLOCK;
				}
			}
			if (!m_pIPResolver->Successful()) {
				m_pIPResolver.reset();

				LogMessage(MessageType::Debug_Warning, _("Failed to retrieve external ip address, using local address"));
			}
			else {
				LogMessage(MessageType::Debug_Info, L"Got external IP address");
				address = m_pIPResolver->GetIP();

				engine_.GetOptions().SetOption(OPTION_LASTRESOLVEDIP, fz::to_wstring(address));

				m_pIPResolver.reset();

				return FZ_REPLY_OK;
			}
		}
	}

getLocalIP:
	address = m_pSocket->GetLocalIP(true);
	if (address.empty()) {
		LogMessage(MessageType::Error, _("Failed to retrieve local ip address."), 1);
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_OK;
}

void CFtpControlSocket::OnExternalIPAddress()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::OnExternalIPAddress()");
	if (!m_pIPResolver) {
		LogMessage(MessageType::Debug_Info, L"Ignoring event");
		return;
	}

	SendNextCommand();
}

void CFtpControlSocket::Transfer(std::wstring const& cmd, CFtpTransferOpData* oldData)
{
	assert(oldData);
	oldData->tranferCommandSent = false;

	CFtpRawTransferOpData *pData = new CFtpRawTransferOpData(*this);
	Push(pData);

	pData->cmd_ = cmd;
	pData->pOldData = oldData;
	pData->pOldData->transferEndReason = TransferEndReason::successful;

	if (m_pProxyBackend) {
		// Only passive suported
		// Theoretically could use reverse proxy ability in SOCKS5, but
		// it is too fragile to set up with all those broken routers and
		// firewalls sabotaging connections. Regular active mode is hard
		// enough already
		pData->bPasv = true;
		pData->bTriedActive = true;
	}
	else {
		switch (currentServer_.GetPasvMode())
		{
		case MODE_PASSIVE:
			pData->bPasv = true;
			break;
		case MODE_ACTIVE:
			pData->bPasv = false;
			break;
		default:
			pData->bPasv = engine_.GetOptions().GetOptionVal(OPTION_USEPASV) != 0;
			break;
		}
	}

	if ((pData->pOldData->binary && m_lastTypeBinary == 1) ||
		(!pData->pOldData->binary && m_lastTypeBinary == 0))
	{
		pData->opState = rawtransfer_port_pasv;
	}
	else {
		pData->opState = rawtransfer_type;
	}
}

void CFtpControlSocket::Connect(CServer const& server)
{
	if (m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"CFtpControlSocket::Connect(): deleting nonzero pData");
		delete m_pCurOpData;
	}

	currentServer_ = server;

	CFtpLogonOpData* pData = new CFtpLogonOpData(*this, server);
	Push(pData);
}

void CFtpControlSocket::OnTimer(fz::timer_id id)
{
	if (id != m_idleTimer) {
		CControlSocket::OnTimer(id);
		return;
	}

	if (m_pCurOpData) {
		return;
	}

	if (m_pendingReplies || m_repliesToSkip) {
		return;
	}

	LogMessage(MessageType::Status, _("Sending keep-alive command"));

	std::wstring cmd;
	int i = fz::random_number(0, 2);
	if (!i) {
		cmd = L"NOOP";
	}
	else if (i == 1) {
		if (m_lastTypeBinary) {
			cmd = L"TYPE I";
		}
		else {
			cmd = L"TYPE A";
		}
	}
	else {
		cmd = L"PWD";
	}

	int res = SendCommand(cmd);
	if (res == FZ_REPLY_WOULDBLOCK) {
		++m_repliesToSkip;
	}
	else {
		DoClose(res);
	}
}

void CFtpControlSocket::StartKeepaliveTimer()
{
	if (!engine_.GetOptions().GetOptionVal(OPTION_FTP_SENDKEEPALIVE)) {
		return;
	}

	if (m_repliesToSkip || m_pendingReplies) {
		return;
	}

	if (!m_lastCommandCompletionTime) {
		return;
	}

	fz::duration const span = fz::monotonic_clock::now() - m_lastCommandCompletionTime;
	if (span.get_minutes() >= 30) {
		return;
	}

	stop_timer(m_idleTimer);
	m_idleTimer = add_timer(fz::duration::from_seconds(30), true);
}

void CFtpControlSocket::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<fz::timer_event>(ev, this, &CFtpControlSocket::OnTimer)) {
		return;
	}

	if (fz::dispatch<CExternalIPResolveEvent>(ev, this, &CFtpControlSocket::OnExternalIPAddress)) {
		return;
	}

	CRealControlSocket::operator()(ev);
}
