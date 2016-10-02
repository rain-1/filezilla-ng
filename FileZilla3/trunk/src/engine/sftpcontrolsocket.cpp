#include <filezilla.h>

#include "directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "pathcache.h"
#include "proxy.h"
#include "servercapabilities.h"
#include "sftpcontrolsocket.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>

#include <wx/log.h>

#include <cwchar>

#define FZSFTP_PROTOCOL_VERSION 6

struct sftp_message
{
	sftpEvent type;
	mutable std::wstring text[3];
};

struct sftp_event_type;
typedef fz::simple_event<sftp_event_type, sftp_message> CSftpEvent;

struct terminate_event_type;
typedef fz::simple_event<terminate_event_type, std::wstring> CTerminateEvent;

class CSftpFileTransferOpData : public CFileTransferOpData
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

class CSftpInputThread final : public fz::thread
{
public:
	CSftpInputThread(CSftpControlSocket* pOwner, fz::process& proc)
		: process_(proc)
		, m_pOwner(pOwner)
	{
	}

	virtual ~CSftpInputThread()
	{
		join();
	}

protected:

	std::wstring ReadLine(std::wstring &error)
	{
		int len = 0;
		const int buffersize = 4096;
		char buffer[buffersize];

		while (true) {
			char c;
			int read = process_.read(&c, 1);
			if (read != 1) {
				if (!read) {
					error = L"Unexpected EOF.";
				}
				else {
					error = L"Unknown error reading from process";
				}
				return std::wstring();
			}

			if (c == '\n') {
				break;
			}

			if (len == buffersize - 1) {
				// Cap string length
				continue;
			}

			buffer[len++] = c;
		}

		while (len && buffer[len - 1] == '\r') {
			--len;
		}

		buffer[len] = 0;

		const wxString line = m_pOwner->ConvToLocal(buffer, len + 1);
		if (len && line.empty()) {
			error = L"Failed to convert reply to local character set.";
		}

		return line.ToStdWstring();
	}

	virtual void entry()
	{
		std::wstring error;
		while (error.empty()) {
			char readType = 0;
			int read = process_.read(&readType, 1);
			if (read != 1) {
				break;
			}

			readType -= '0';

			if (readType < 0 || readType >= static_cast<char>(sftpEvent::count) ) {
				error = fz::sprintf(L"Unknown eventType %d", readType);
				break;
			}

			sftpEvent eventType = static_cast<sftpEvent>(readType);

			int lines{};
			switch (eventType)
			{
			case sftpEvent::count:
			case sftpEvent::Unknown:
				error = fz::sprintf(L"Unknown eventType %d", readType);
				break;
			case sftpEvent::Recv:
			case sftpEvent::Send:
			case sftpEvent::UsedQuotaRecv:
			case sftpEvent::UsedQuotaSend:
				break;
			case sftpEvent::Reply:
			case sftpEvent::Done:
			case sftpEvent::Error:
			case sftpEvent::Verbose:
			case sftpEvent::Status:
			case sftpEvent::Transfer:
			case sftpEvent::AskPassword:
			case sftpEvent::RequestPreamble:
			case sftpEvent::RequestInstruction:
			case sftpEvent::KexAlgorithm:
			case sftpEvent::KexHash:
			case sftpEvent::KexCurve:
			case sftpEvent::CipherClientToServer:
			case sftpEvent::CipherServerToClient:
			case sftpEvent::MacClientToServer:
			case sftpEvent::MacServerToClient:
			case sftpEvent::Hostkey:
				lines = 1;
				break;
			case sftpEvent::AskHostkeyBetteralg:
				lines = 2;
				break;
			case sftpEvent::Listentry:
			case sftpEvent::AskHostkey:
			case sftpEvent::AskHostkeyChanged:
				lines = 3;
				break;
			};

			auto msg = new CSftpEvent;
			auto & message = std::get<0>(msg->v_);
			message.type = eventType;
			for (int i = 0; i < lines && error.empty(); ++i) {
				message.text[i] = ReadLine(error);
			}

			if (!error.empty()) {
				delete msg;
				break;
			}

			m_pOwner->send_event(msg);
		}

		m_pOwner->send_event<CTerminateEvent>(error);
	}

	fz::process& process_;
	CSftpControlSocket* m_pOwner;
};

class CSftpDeleteOpData final : public COpData
{
public:
	CSftpDeleteOpData()
		: COpData(Command::del)
	{
	}

	virtual ~CSftpDeleteOpData() {}

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

enum connectStates
{
	connect_init,
	connect_proxy,
	connect_keys,
	connect_open
};

class CSftpConnectOpData final : public COpData
{
public:
	CSftpConnectOpData()
		: COpData(Command::connect)
		, keyfile_(keyfiles_.cend())
	{
	}

	virtual ~CSftpConnectOpData()
	{
	}

	wxString lastChallenge;
	CInteractiveLoginNotification::type lastChallengeType{CInteractiveLoginNotification::interactive};
	bool criticalFailure{};

	std::vector<std::wstring> keyfiles_;
	std::vector<std::wstring>::const_iterator keyfile_;
};

int CSftpControlSocket::Connect(const CServer &server)
{
	LogMessage(MessageType::Status, _("Connecting to %s..."), server.Format(ServerFormat::with_optional_port));
	SetWait(true);

	m_sftpEncryptionDetails = CSftpEncryptionNotification();

	delete m_pCSConv;
	if (server.GetEncodingType() == ENCODING_CUSTOM) {
		LogMessage(MessageType::Debug_Info, _T("Using custom encoding: %s"), server.GetCustomEncoding());
		m_pCSConv = new wxCSConv(server.GetCustomEncoding());
		m_useUTF8 = false;
	}
	else {
		m_pCSConv = 0;
		m_useUTF8 = true;
	}

	delete m_pCurrentServer;
	m_pCurrentServer = new CServer(server);

	if (CServerCapabilities::GetCapability(*m_pCurrentServer, timezone_offset) == unknown) {
		CServerCapabilities::SetCapability(*m_pCurrentServer, timezone_offset, yes, 0);
	}

	CSftpConnectOpData* pData = new CSftpConnectOpData;
	m_pCurOpData = pData;

	pData->opState = connect_init;

	if (m_pCurrentServer->GetLogonType() == KEY) {
		pData->keyfiles_ = fz::strtok(m_pCurrentServer->GetKeyFile(), L"\r\n");
	}
	else {
		pData->keyfiles_ = fz::strtok(engine_.GetOptions().GetOption(OPTION_SFTP_KEYFILES), L"\r\n");
	}
	pData->keyfile_ = pData->keyfiles_.cbegin();

	m_pProcess = new fz::process();

	engine_.GetRateLimiter().AddObject(this);

	auto executable = fz::to_native(engine_.GetOptions().GetOption(OPTION_FZSFTP_EXECUTABLE));
	if (executable.empty())
		executable = fzT("fzsftp");
	LogMessage(MessageType::Debug_Verbose, _T("Going to execute %s"), executable);

	std::vector<fz::native_string> args = {fzT("-v")};
	if (engine_.GetOptions().GetOptionVal(OPTION_SFTP_COMPRESSION)) {
		args.push_back(fzT("-C"));
	}
	if (!m_pProcess->spawn(executable, args)) {
		LogMessage(MessageType::Debug_Warning, _T("Could not create process"));
		DoClose();
		return FZ_REPLY_ERROR;
	}

	m_pInputThread = new CSftpInputThread(this, *m_pProcess);
	if (!m_pInputThread->run()) {
		LogMessage(MessageType::Debug_Warning, _T("Thread creation failed"));
		delete m_pInputThread;
		m_pInputThread = 0;
		DoClose();
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpControlSocket::ConnectParseResponse(bool successful, const wxString& reply)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ConnectParseResponse(%s)"), reply);

	if (!successful) {
		DoClose(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		DoClose(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpConnectOpData *pData = static_cast<CSftpConnectOpData *>(m_pCurOpData);
	if (!pData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData of wrong type"));
		DoClose(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	switch (pData->opState)
	{
	case connect_init:
		if (reply != wxString::Format(_T("fzSftp started, protocol_version=%d"), FZSFTP_PROTOCOL_VERSION)) {
			LogMessage(MessageType::Error, _("fzsftp belongs to a different version of FileZilla"));
			DoClose(FZ_REPLY_INTERNALERROR);
			return FZ_REPLY_ERROR;
		}
		if (engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE) && !m_pCurrentServer->GetBypassProxy()) {
			pData->opState = connect_proxy;
		}
		else if (pData->keyfile_ != pData->keyfiles_.cend()) {
			pData->opState = connect_keys;
		}
		else {
			pData->opState = connect_open;
		}
		break;
	case connect_proxy:
		if (pData->keyfile_ != pData->keyfiles_.cend()) {
			pData->opState = connect_keys;
		}
		else {
			pData->opState = connect_open;
		}
		break;
	case connect_keys:
		if (pData->keyfile_ == pData->keyfiles_.cend()) {
			pData->opState = connect_open;
		}
		break;
	case connect_open:
		engine_.AddNotification(new CSftpEncryptionNotification(m_sftpEncryptionDetails));
		ResetOperation(FZ_REPLY_OK);
		return FZ_REPLY_OK;
	default:
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Unknown op state: %d"), pData->opState);
		DoClose(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	return SendNextCommand();
}

int CSftpControlSocket::ConnectSend()
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ConnectSend()"));
	if (!m_pCurOpData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		DoClose(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpConnectOpData *pData = static_cast<CSftpConnectOpData *>(m_pCurOpData);
	if (!pData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData of wrong type"));
		DoClose(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	bool res;
	switch (pData->opState)
	{
	case connect_proxy:
		{
			int type;
			switch (engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE))
			{
			case CProxySocket::HTTP:
				type = 1;
				break;
			case CProxySocket::SOCKS5:
				type = 2;
				break;
			case CProxySocket::SOCKS4:
				type = 3;
				break;
			default:
				LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Unsupported proxy type"));
				DoClose(FZ_REPLY_INTERNALERROR);
				return FZ_REPLY_ERROR;
			}

			std::wstring cmd = fz::sprintf(L"proxy %d \"%s\" %d", type,
											engine_.GetOptions().GetOption(OPTION_PROXY_HOST),
											engine_.GetOptions().GetOptionVal(OPTION_PROXY_PORT));
			std::wstring user = engine_.GetOptions().GetOption(OPTION_PROXY_USER);
			if (!user.empty()) {
				cmd += L" \"" + user + L"\"";
			}

			std::wstring show = cmd;
			std::wstring pass = engine_.GetOptions().GetOption(OPTION_PROXY_PASS);
			if (!pass.empty()) {
				cmd += L" \"" + pass + L"\"";
				show += L" \"" + std::wstring(pass.size(), '*') + L"\"";
			}
			res = SendCommand(cmd, show);
		}
		break;
	case connect_keys:
		res = SendCommand(L"keyfile \"" + *(pData->keyfile_++) + L"\"");
		break;
	case connect_open:
		res = SendCommand(fz::sprintf(L"open \"%s@%s\" %d", m_pCurrentServer->GetUser(), m_pCurrentServer->GetHost(), m_pCurrentServer->GetPort()));
		break;
	default:
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Unknown op state: %d"), pData->opState);
		DoClose(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (res)
		return FZ_REPLY_WOULDBLOCK;
	else
		return FZ_REPLY_ERROR;
}

void CSftpControlSocket::OnSftpEvent(sftp_message const& message)
{
	if (!m_pCurrentServer)
		return;

	if (!m_pInputThread)
		return;

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
		ListParseEntry(std::move(message.text[0]), message.text[1], std::move(message.text[2]));
		break;
	case sftpEvent::Transfer:
		{
			long value{};
			wxString s = message.text[0];
			if (!s.ToLong(&value)) {
				value = 0;
			}

			bool tmp;
			CTransferStatus status = engine_.transfer_status_.Get(tmp);
			if (!status.empty() && !status.madeProgress) {
				if (m_pCurOpData && m_pCurOpData->opId == Command::transfer) {
					CSftpFileTransferOpData *pData = static_cast<CSftpFileTransferOpData *>(m_pCurOpData);
					if (pData->download) {
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
			long port = 0;
			if (!wxString(message.text[1]).ToLong(&port)) {
				DoClose(FZ_REPLY_INTERNALERROR);
				break;
			}
			SendAsyncRequest(new CHostKeyNotification(message.text[0], port, message.text[2], message.type == sftpEvent::AskHostkeyChanged));
		}
		break;
	case sftpEvent::AskHostkeyBetteralg:
		LogMessage(MessageType::Error, _T("Got sftpReqHostkeyBetteralg when we shouldn't have. Aborting connection."));
		DoClose(FZ_REPLY_INTERNALERROR);
		break;
	case sftpEvent::AskPassword:
		if (!m_pCurOpData || m_pCurOpData->opId != Command::connect) {
			LogMessage(MessageType::Debug_Warning, _T("sftpReqPassword outside connect operation, ignoring."));
			break;
		}
		else {
			CSftpConnectOpData *pData = static_cast<CSftpConnectOpData*>(m_pCurOpData);

			wxString const challengeIdentifier = m_requestPreamble + _T("\n") + m_requestInstruction + _T("\n") + message.text[0];

			CInteractiveLoginNotification::type t = CInteractiveLoginNotification::interactive;
			if (m_pCurrentServer->GetLogonType() == INTERACTIVE || m_requestPreamble == _T("SSH key passphrase")) {
				if (m_requestPreamble == _T("SSH key passphrase")) {
					t = CInteractiveLoginNotification::keyfile;
				}

				wxString challenge;
				if (!m_requestPreamble.empty() && t != CInteractiveLoginNotification::keyfile) {
					challenge += m_requestPreamble + _T("\n");
				}
				if (!m_requestInstruction.empty()) {
					challenge += m_requestInstruction + _T("\n");
				}
				if (message.text[0] != L"Password:") {
					challenge += message.text[0];
				}
				CInteractiveLoginNotification *pNotification = new CInteractiveLoginNotification(t, challenge, pData->lastChallenge == challengeIdentifier);
				pNotification->server = *m_pCurrentServer;

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

				std::wstring const pass = m_pCurrentServer->GetPass().ToStdWstring();
				std::wstring show = _T("Pass: ");
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
		m_sftpEncryptionDetails.hostKey = message.text[0];
		break;
	default:
		wxFAIL_MSG(_T("given notification codes not handled"));
		break;
	}
}

void CSftpControlSocket::OnTerminate(std::wstring const& error)
{
	if (!error.empty()) {
		LogMessageRaw(MessageType::Error, error);
	}
	else {
		LogMessageRaw(MessageType::Debug_Info, _T("CSftpControlSocket::OnTerminate without error"));
	}
	if (m_pProcess) {
		DoClose();
	}
}

bool CSftpControlSocket::SendCommand(std::wstring const& cmd, std::wstring const& show)
{
	SetWait(true);

	LogMessageRaw(MessageType::Command, show.empty() ? cmd : show);

	// Check for newlines in command
	// a command like "ls\nrm foo/bar" is dangerous
	if (cmd.find('\n') != std::wstring::npos ||
		cmd.find('\r') != std::wstring::npos)
	{
		LogMessage(MessageType::Debug_Warning, _T("Command containing newline characters, aborting"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return AddToStream(cmd + _T("\n"));
}

bool CSftpControlSocket::AddToStream(const wxString& cmd, bool force_utf8)
{
	if (!m_pProcess) {
		return false;
	}

	wxCharBuffer const str = ConvToServer(cmd, force_utf8);
	if (!str) {
		LogMessage(MessageType::Error, _("Could not convert command to server encoding"));
		return false;
	}

	unsigned int len = strlen(str);
	return m_pProcess->write(str, len);
}

bool CSftpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (m_pCurOpData) {
		if (!m_pCurOpData->waitForAsyncRequest) {
			LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Not waiting for request reply, ignoring request reply %d"), pNotification->GetRequestID());
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
				!m_pCurrentServer)
			{
				LogMessage(MessageType::Debug_Info, _T("SetAsyncRequestReply called to wrong time"));
				return false;
			}

			CHostKeyNotification *pHostKeyNotification = static_cast<CHostKeyNotification *>(pNotification);
			std::wstring show;
			if (requestId == reqId_hostkey)
				show = _("Trust new Hostkey:").ToStdWstring();
			else
				show = _("Trust changed Hostkey:").ToStdWstring();
			show += ' ';
			if (!pHostKeyNotification->m_trust) {
				SendCommand(std::wstring(), show + _("No").ToStdWstring());
				if (m_pCurOpData && m_pCurOpData->opId == Command::connect) {
					CSftpConnectOpData *pData = static_cast<CSftpConnectOpData *>(m_pCurOpData);
					pData->criticalFailure = true;
				}
			}
			else if (pHostKeyNotification->m_alwaysTrust)
				SendCommand(L"y", show + _("Yes").ToStdWstring());
			else
				SendCommand(L"n", show + _("Once").ToStdWstring());
		}
		break;
	case reqId_interactiveLogin:
		{
			CInteractiveLoginNotification *pInteractiveLoginNotification = static_cast<CInteractiveLoginNotification *>(pNotification);

			if (!pInteractiveLoginNotification->passwordSet) {
				DoClose(FZ_REPLY_CANCELED);
				return false;
			}
			std::wstring const pass = pInteractiveLoginNotification->server.GetPass().ToStdWstring();
			m_pCurrentServer->SetUser(m_pCurrentServer->GetUser(), pass);
			std::wstring show = L"Pass: ";
			show.append(pass.size(), '*');
			SendCommand(pass, show);
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, _T("Unknown async request reply id: %d"), requestId);
		return false;
	}

	return true;
}

class CSftpListOpData final : public COpData
{
public:
	CSftpListOpData()
		: COpData(Command::list)
		, pParser()
		, refresh()
		, fallback_to_current()
		, mtime_index()
	{
	}

	virtual ~CSftpListOpData()
	{
		delete pParser;
	}

	CDirectoryListingParser* pParser;

	CServerPath path;
	wxString subDir;

	// Set to true to get a directory listing even if a cache
	// lookup can be made after finding out true remote directory
	bool refresh;
	bool fallback_to_current;

	CDirectoryListing directoryListing;
	int mtime_index;

	fz::monotonic_clock m_time_before_locking;
};

enum listStates
{
	list_init = 0,
	list_waitcwd,
	list_waitlock,
	list_list,
	list_mtime
};

int CSftpControlSocket::List(CServerPath path /*=CServerPath()*/, wxString subDir /*=_T("")*/, int flags /*=0*/)
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

	if (m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("List called from other command"));
	}

	if (!m_pCurrentServer) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurrenServer == 0"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpListOpData *pData = new CSftpListOpData;
	pData->pNextOpData = m_pCurOpData;
	m_pCurOpData = pData;

	pData->opState = list_waitcwd;

	if (path.GetType() == DEFAULT)
		path.SetType(m_pCurrentServer->GetType());
	pData->path = path;
	pData->subDir = subDir;
	pData->refresh = (flags & LIST_FLAG_REFRESH) != 0;
	pData->fallback_to_current = !path.empty() && (flags & LIST_FLAG_FALLBACK_CURRENT) != 0;

	int res = ChangeDir(path, subDir, (flags & LIST_FLAG_LINK) != 0);
	if (res != FZ_REPLY_OK)
		return res;

	return ParseSubcommandResult(FZ_REPLY_OK);
}

int CSftpControlSocket::ListParseResponse(bool successful, const wxString& reply)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ListParseResponse(%s)"), reply);

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpListOpData *pData = static_cast<CSftpListOpData *>(m_pCurOpData);
	if (!pData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData of wrong type"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (pData->opState == list_list) {
		if (!successful)
		{
			ResetOperation(FZ_REPLY_ERROR);
			return FZ_REPLY_ERROR;
		}

		if (!pData->pParser)
		{
			LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("pData->pParser is 0"));
			ResetOperation(FZ_REPLY_INTERNALERROR);
			return FZ_REPLY_ERROR;
		}

		pData->directoryListing = pData->pParser->Parse(m_CurrentPath);

		int res = ListCheckTimezoneDetection();
		if (res != FZ_REPLY_OK)
			return res;

		engine_.GetDirectoryCache().Store(pData->directoryListing, *m_pCurrentServer);

		engine_.SendDirectoryListingNotification(m_CurrentPath, !pData->pNextOpData, true, false);

		ResetOperation(FZ_REPLY_OK);
		return FZ_REPLY_OK;
	}
	else if (pData->opState == list_mtime) {
		if (successful && !reply.empty()) {
			time_t seconds = 0;
			bool parsed = true;
			for (unsigned int i = 0; i < reply.Len(); ++i) {
				wxChar c = reply[i];
				if (c < '0' || c > '9') {
					parsed = false;
					break;
				}
				seconds *= 10;
				seconds += c - '0';
			}
			if (parsed) {
				fz::datetime date(seconds, fz::datetime::seconds);
				if (!date.empty()) {
					wxASSERT(pData->directoryListing[pData->mtime_index].has_date());
					fz::datetime listTime = pData->directoryListing[pData->mtime_index].time;
					listTime -= fz::duration::from_minutes(m_pCurrentServer->GetTimezoneOffset());

					int serveroffset = static_cast<int>((date - listTime).get_seconds());
					if (!pData->directoryListing[pData->mtime_index].has_seconds()) {
						// Round offset to full minutes
						if (serveroffset < 0)
							serveroffset -= 59;
						serveroffset -= serveroffset % 60;
					}

					LogMessage(MessageType::Status, _("Timezone offset of server is %d seconds."), -serveroffset);

					fz::duration span = fz::duration::from_seconds(serveroffset);
					const int count = pData->directoryListing.GetCount();
					for (int i = 0; i < count; ++i) {
						CDirentry& entry = pData->directoryListing[i];
						entry.time += span;
					}

					// TODO: Correct cached listings

					CServerCapabilities::SetCapability(*m_pCurrentServer, timezone_offset, yes, serveroffset);
				}
			}
		}

		engine_.GetDirectoryCache().Store(pData->directoryListing, *m_pCurrentServer);

		engine_.SendDirectoryListingNotification(m_CurrentPath, !pData->pNextOpData, true, false);

		ResetOperation(FZ_REPLY_OK);
		return FZ_REPLY_OK;
	}

	LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("ListParseResponse called at inproper time: %d"), pData->opState);
	ResetOperation(FZ_REPLY_INTERNALERROR);
	return FZ_REPLY_ERROR;
}

int CSftpControlSocket::ListParseEntry(std::wstring && entry, std::wstring const& stime, std::wstring && name)
{
	if (!m_pCurOpData) {
		LogMessageRaw(MessageType::RawList, entry);
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (m_pCurOpData->opId != Command::list) {
		LogMessageRaw(MessageType::RawList, entry);
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Listentry received, but current operation is not Command::list"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpListOpData *pData = static_cast<CSftpListOpData *>(m_pCurOpData);
	if (!pData) {
		LogMessageRaw(MessageType::RawList, entry);
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData of wrong type"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (pData->opState != list_list) {
		LogMessageRaw(MessageType::RawList, entry);
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("ListParseResponse called at inproper time: %d"), pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!pData->pParser) {
		LogMessageRaw(MessageType::RawList, entry);
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("pData->pParser is 0"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_INTERNALERROR;
	}

	fz::datetime time;
	if (!stime.empty()) {
		int64_t t = std::wcstoll(stime.c_str(), 0, 10);
		if (t > 0) {
			time = fz::datetime(static_cast<time_t>(t), fz::datetime::seconds);
		}
	}
	pData->pParser->AddLine(std::move(entry), std::move(name), time);

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpControlSocket::ListSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ListSubcommandResult()"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpListOpData *pData = static_cast<CSftpListOpData *>(m_pCurOpData);
	LogMessage(MessageType::Debug_Debug, _T("  state = %d"), pData->opState);

	if (pData->opState != list_waitcwd) {
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (prevResult != FZ_REPLY_OK) {
		if (pData->fallback_to_current) {
			// List current directory instead
			pData->fallback_to_current = false;
			pData->path.clear();
			pData->subDir = _T("");
			int res = ChangeDir();
			if (res != FZ_REPLY_OK)
				return res;
		}
		else {
			ResetOperation(prevResult);
			return FZ_REPLY_ERROR;
		}
	}

	if (pData->path.empty())
		pData->path = m_CurrentPath;

	if (!pData->refresh) {
		wxASSERT(!pData->pNextOpData);

		// Do a cache lookup now that we know the correct directory

		int hasUnsureEntries;
		bool is_outdated = false;
		bool found = engine_.GetDirectoryCache().DoesExist(*m_pCurrentServer, m_CurrentPath, hasUnsureEntries, is_outdated);
		if (found) {
			// We're done if listins is recent and has no outdated entries
			if (!is_outdated && !hasUnsureEntries) {
				engine_.SendDirectoryListingNotification(m_CurrentPath, true, false, false);

				ResetOperation(FZ_REPLY_OK);

				return FZ_REPLY_OK;
			}
		}
	}

	if (!pData->holdsLock) {
		if (!TryLockCache(lock_list, m_CurrentPath)) {
			pData->opState = list_waitlock;
			pData->m_time_before_locking = fz::monotonic_clock::now();
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	pData->opState = list_list;

	return SendNextCommand();
}

int CSftpControlSocket::ListSend()
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ListSend()"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpListOpData *pData = static_cast<CSftpListOpData *>(m_pCurOpData);
	LogMessage(MessageType::Debug_Debug, _T("  state = %d"), pData->opState);

	if (pData->opState == list_waitlock) {
		if (!pData->holdsLock) {
			LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Not holding the lock as expected"));
			ResetOperation(FZ_REPLY_INTERNALERROR);
			return FZ_REPLY_ERROR;
		}

		// Check if we can use already existing listing
		CDirectoryListing listing;
		bool is_outdated = false;
		wxASSERT(pData->subDir.empty()); // Did do ChangeDir before trying to lock
		bool found = engine_.GetDirectoryCache().Lookup(listing, *m_pCurrentServer, pData->path, true, is_outdated);
		if (found && !is_outdated && !listing.get_unsure_flags() &&
			listing.m_firstListTime >= pData->m_time_before_locking)
		{
			engine_.SendDirectoryListingNotification(listing.path, !pData->pNextOpData, false, false);

			ResetOperation(FZ_REPLY_OK);
			return FZ_REPLY_OK;
		}

		pData->opState = list_waitcwd;

		return ListSubcommandResult(FZ_REPLY_OK);
	}
	else if (pData->opState == list_list) {
		pData->pParser = new CDirectoryListingParser(this, *m_pCurrentServer, listingEncoding::unknown);
		pData->pParser->SetTimezoneOffset(GetTimezoneOffset());
		if (!SendCommand(_T("ls")))
			return FZ_REPLY_ERROR;
		return FZ_REPLY_WOULDBLOCK;
	}
	else if (pData->opState == list_mtime) {
		LogMessage(MessageType::Status, _("Calculating timezone offset of server..."));
		std::wstring const& name = pData->directoryListing[pData->mtime_index].name;
		std::wstring quotedFilename = QuoteFilename(pData->directoryListing.path.FormatFilename(name, true).ToStdWstring());
		if (!SendCommand(L"mtime " + WildcardEscape(quotedFilename),
			L"mtime " + quotedFilename))
			return FZ_REPLY_ERROR;
		return FZ_REPLY_WOULDBLOCK;
	}

	LogMessage(MessageType::Debug_Warning, _T("Unknown opStatein CSftpControlSocket::ListSend"));
	ResetOperation(FZ_REPLY_INTERNALERROR);
	return FZ_REPLY_ERROR;
}

class CSftpChangeDirOpData : public CChangeDirOpData
{
};

enum cwdStates
{
	cwd_init = 0,
	cwd_pwd,
	cwd_cwd,
	cwd_cwd_subdir
};

int CSftpControlSocket::ChangeDir(CServerPath path /*=CServerPath()*/, wxString subDir /*=_T("")*/, bool link_discovery /*=false*/)
{
	cwdStates state = cwd_init;

	if (path.GetType() == DEFAULT)
		path.SetType(m_pCurrentServer->GetType());

	CServerPath target;
	if (path.empty()) {
		if (m_CurrentPath.empty())
			state = cwd_pwd;
		else
			return FZ_REPLY_OK;
	}
	else {
		if (!subDir.empty()) {
			// Check if the target is in cache already
			target = engine_.GetPathCache().Lookup(*m_pCurrentServer, path, subDir);
			if (!target.empty()) {
				if (m_CurrentPath == target)
					return FZ_REPLY_OK;

				path = target;
				subDir = _T("");
				state = cwd_cwd;
			}
			else {
				// Target unknown, check for the parent's target
				target = engine_.GetPathCache().Lookup(*m_pCurrentServer, path, _T(""));
				if (m_CurrentPath == path || (!target.empty() && target == m_CurrentPath)) {
					target.clear();
					state = cwd_cwd_subdir;
				}
				else
					state = cwd_cwd;
			}
		}
		else {
			target = engine_.GetPathCache().Lookup(*m_pCurrentServer, path, _T(""));
			if (m_CurrentPath == path || (!target.empty() && target == m_CurrentPath))
				return FZ_REPLY_OK;
			state = cwd_cwd;
		}
	}

	CSftpChangeDirOpData *pData = new CSftpChangeDirOpData;
	pData->pNextOpData = m_pCurOpData;
	pData->opState = state;
	pData->path = path;
	pData->subDir = subDir;
	pData->target = target;
	pData->link_discovery = link_discovery;

	if (pData->pNextOpData && pData->pNextOpData->opId == Command::transfer &&
		!static_cast<CSftpFileTransferOpData *>(pData->pNextOpData)->download)
	{
		pData->tryMkdOnFail = true;
		wxASSERT(subDir.empty());
	}

	m_pCurOpData = pData;

	return SendNextCommand();
}

int CSftpControlSocket::ChangeDirParseResponse(bool successful, const wxString& reply)
{
	if (!m_pCurOpData)
	{
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}
	CSftpChangeDirOpData *pData = static_cast<CSftpChangeDirOpData *>(m_pCurOpData);

	bool error = false;
	switch (pData->opState)
	{
	case cwd_pwd:
		if (!successful || reply.empty())
			error = true;
		if (ParsePwdReply(reply))
		{
			ResetOperation(FZ_REPLY_OK);
			return FZ_REPLY_OK;
		}
		else
			error = true;
		break;
	case cwd_cwd:
		if (!successful)
		{
			// Create remote directory if part of a file upload
			if (pData->tryMkdOnFail)
			{
				pData->tryMkdOnFail = false;
				int res = Mkdir(pData->path);
				if (res != FZ_REPLY_OK)
					return res;
			}
			else
				error = true;
		}
		else if (reply.empty())
			error = true;
		else if (ParsePwdReply(reply)) {
			engine_.GetPathCache().Store(*m_pCurrentServer, m_CurrentPath, pData->path);

			if (pData->subDir.empty()) {
				ResetOperation(FZ_REPLY_OK);
				return FZ_REPLY_OK;
			}

			pData->target.clear();
			pData->opState = cwd_cwd_subdir;
		}
		else
			error = true;
		break;
	case cwd_cwd_subdir:
		if (!successful || reply.empty())
		{
			if (pData->link_discovery)
			{
				LogMessage(MessageType::Debug_Info, _T("Symlink does not link to a directory, probably a file"));
				ResetOperation(FZ_REPLY_LINKNOTDIR);
				return FZ_REPLY_ERROR;
			}
			else
				error = true;
		}
		else if (ParsePwdReply(reply)) {
			engine_.GetPathCache().Store(*m_pCurrentServer, m_CurrentPath, pData->path, pData->subDir);

			ResetOperation(FZ_REPLY_OK);
			return FZ_REPLY_OK;
		}
		else
			error = true;
		break;
	default:
		error = true;
		break;
	}

	if (error)
	{
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	return SendNextCommand();
}

int CSftpControlSocket::ChangeDirSubcommandResult(int WXUNUSED(prevResult))
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ChangeDirSubcommandResult()"));

	return SendNextCommand();
}

int CSftpControlSocket::ChangeDirSend()
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ChangeDirSend()"));

	if (!m_pCurOpData) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}
	CSftpChangeDirOpData *pData = static_cast<CSftpChangeDirOpData *>(m_pCurOpData);

	std::wstring cmd;
	switch (pData->opState)
	{
	case cwd_pwd:
		cmd = L"pwd";
		break;
	case cwd_cwd:
		if (pData->tryMkdOnFail && !pData->holdsLock) {
			if (IsLocked(lock_mkdir, pData->path)) {
				// Some other engine is already creating this directory or
				// performing an action that will lead to its creation
				pData->tryMkdOnFail = false;
			}
			if (!TryLockCache(lock_mkdir, pData->path)) {
				return FZ_REPLY_WOULDBLOCK;
			}
		}
		cmd = L"cd " + QuoteFilename(pData->path.GetPath().ToStdWstring());
		m_CurrentPath.clear();
		break;
	case cwd_cwd_subdir:
		if (pData->subDir.empty()) {
			ResetOperation(FZ_REPLY_INTERNALERROR);
			return FZ_REPLY_ERROR;
		}
		else {
			cmd = L"cd " + QuoteFilename(pData->subDir.ToStdWstring());
		}
		m_CurrentPath.clear();
		break;
	}

	if (!cmd.empty()) {
		if (!SendCommand(cmd)) {
			return FZ_REPLY_ERROR;
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

void CSftpControlSocket::ProcessReply(int result, std::wstring const& reply)
{
	Command commandId = GetCurrentCommandId();
	switch (commandId)
	{
	case Command::connect:
		ConnectParseResponse(result == FZ_REPLY_OK, reply);
		break;
	case Command::list:
		ListParseResponse(result == FZ_REPLY_OK, reply);
		break;
	case Command::transfer:
		FileTransferParseResponse(result, reply);
		break;
	case Command::cwd:
		ChangeDirParseResponse(result == FZ_REPLY_OK, reply);
		break;
	case Command::mkdir:
		MkdirParseResponse(result == FZ_REPLY_OK, reply);
		break;
	case Command::del:
		DeleteParseResponse(result == FZ_REPLY_OK, reply);
		break;
	case Command::removedir:
		RemoveDirParseResponse(result == FZ_REPLY_OK, reply);
		break;
	case Command::chmod:
		ChmodParseResponse(result == FZ_REPLY_OK, reply);
		break;
	case Command::rename:
		RenameParseResponse(result == FZ_REPLY_OK, reply);
		break;
	default:
		LogMessage(MessageType::Debug_Warning, _T("No action for parsing replies to command %d"), (int)commandId);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		break;
	}
}

int CSftpControlSocket::ResetOperation(int nErrorCode)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ResetOperation(%d)"), nErrorCode);

	if (m_pCurOpData && m_pCurOpData->opId == Command::connect)
	{
		CSftpConnectOpData *pData = static_cast<CSftpConnectOpData *>(m_pCurOpData);
		if (pData->opState == connect_init && (nErrorCode & FZ_REPLY_CANCELED) != FZ_REPLY_CANCELED)
			LogMessage(MessageType::Error, _("fzsftp could not be started"));
		if (pData->criticalFailure)
			nErrorCode |= FZ_REPLY_CRITICALERROR;
	}
	if (m_pCurOpData && m_pCurOpData->opId == Command::del && !(nErrorCode & FZ_REPLY_DISCONNECTED))
	{
		CSftpDeleteOpData *pData = static_cast<CSftpDeleteOpData *>(m_pCurOpData);
		if (pData->m_needSendListing)
			engine_.SendDirectoryListingNotification(pData->path, false, true, false);
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

int CSftpControlSocket::SendNextCommand()
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::SendNextCommand()"));
	if (!m_pCurOpData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("SendNextCommand called without active operation"));
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	if (m_pCurOpData->waitForAsyncRequest)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Waiting for async request, ignoring SendNextCommand"));
		return FZ_REPLY_WOULDBLOCK;
	}

	switch (m_pCurOpData->opId)
	{
	case Command::connect:
		return ConnectSend();
	case Command::list:
		return ListSend();
	case Command::transfer:
		return FileTransferSend();
	case Command::cwd:
		return ChangeDirSend();
	case Command::mkdir:
		return MkdirSend();
	case Command::rename:
		return RenameSend();
	case Command::chmod:
		return ChmodSend();
	case Command::del:
		return DeleteSend();
	default:
		LogMessage(MessageType::Debug_Warning, __TFILE__, __LINE__, _T("Unknown opID (%d) in SendNextCommand"), m_pCurOpData->opId);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		break;
	}

	return FZ_REPLY_ERROR;
}

int CSftpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const& transferSettings)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::FileTransfer(...)"));

	if (localFile.empty()) {
		if (!download)
			ResetOperation(FZ_REPLY_CRITICALERROR | FZ_REPLY_NOTSUPPORTED);
		else
			ResetOperation(FZ_REPLY_SYNTAXERROR);
		return FZ_REPLY_ERROR;
	}

	if (download) {
		wxString filename = remotePath.FormatFilename(remoteFile);
		LogMessage(MessageType::Status, _("Starting download of %s"), filename);
	}
	else {
		LogMessage(MessageType::Status, _("Starting upload of %s"), localFile);
	}
	if (m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("deleting nonzero pData"));
		delete m_pCurOpData;
	}

	CSftpFileTransferOpData *pData = new CSftpFileTransferOpData(download, localFile, remoteFile, remotePath);
	m_pCurOpData = pData;

	pData->transferSettings = transferSettings;

	int64_t size;
	bool isLink;
	if (fz::local_filesys::get_file_info(fz::to_native(pData->localFile), isLink, &size, 0, 0) == fz::local_filesys::file) {
		pData->localFileSize = size;
	}

	pData->opState = filetransfer_waitcwd;

	if (pData->remotePath.GetType() == DEFAULT)
		pData->remotePath.SetType(m_pCurrentServer->GetType());

	int res = ChangeDir(pData->remotePath);
	if (res != FZ_REPLY_OK)
		return res;

	return ParseSubcommandResult(FZ_REPLY_OK);
}

int CSftpControlSocket::FileTransferSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::FileTransferSubcommandResult()"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpFileTransferOpData *pData = static_cast<CSftpFileTransferOpData *>(m_pCurOpData);

	if (pData->opState == filetransfer_waitcwd) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, *m_pCurrentServer, pData->tryAbsolutePath ? pData->remotePath : m_CurrentPath, pData->remoteFile, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist)
					pData->opState = filetransfer_waitlist;
				else if (pData->download &&
					engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
				{
					pData->opState = filetransfer_mtime;
				}
				else
					pData->opState = filetransfer_transfer;
			}
			else {
				if (entry.is_unsure())
					pData->opState = filetransfer_waitlist;
				else {
					if (matchedCase) {
						pData->remoteFileSize = entry.size;
						if (entry.has_date())
							pData->fileTime = entry.time;

						if (pData->download && !entry.has_time() &&
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
			}
			if (pData->opState == filetransfer_waitlist) {
				int res = List(CServerPath(), _T(""), LIST_FLAG_REFRESH);
				if (res != FZ_REPLY_OK)
					return res;
				ResetOperation(FZ_REPLY_INTERNALERROR);
				return FZ_REPLY_ERROR;
			}
			else if (pData->opState == filetransfer_transfer) {
				int res = CheckOverwriteFile();
				if (res != FZ_REPLY_OK)
					return res;
			}
		}
		else {
			pData->tryAbsolutePath = true;
			pData->opState = filetransfer_mtime;
		}
	}
	else if (pData->opState == filetransfer_waitlist) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, *m_pCurrentServer, pData->tryAbsolutePath ? pData->remotePath : m_CurrentPath, pData->remoteFile, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist)
					pData->opState = filetransfer_mtime;
				else if (pData->download &&
					engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
				{
					pData->opState = filetransfer_mtime;
				}
				else
					pData->opState = filetransfer_transfer;
			}
			else {
				if (matchedCase && !entry.is_unsure()) {
					pData->remoteFileSize = entry.size;
					if (!entry.has_date())
						pData->fileTime = entry.time;

					if (pData->download && !entry.has_time() &&
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
		LogMessage(MessageType::Debug_Warning, _T("  Unknown opState (%d)"), pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	return SendNextCommand();
}

int CSftpControlSocket::FileTransferSend()
{
	LogMessage(MessageType::Debug_Verbose, _T("FileTransferSend()"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpFileTransferOpData *pData = static_cast<CSftpFileTransferOpData *>(m_pCurOpData);

	if (pData->opState == filetransfer_transfer) {
		std::wstring cmd;
		if (pData->resume) {
			cmd = _T("re");
		}
		if (pData->download) {
			if (!pData->resume) {
				CreateLocalDir(pData->localFile);
			}

			engine_.transfer_status_.Init(pData->remoteFileSize, pData->resume ? pData->localFileSize : 0, false);
			cmd += L"get ";
			cmd += QuoteFilename(pData->remotePath.FormatFilename(pData->remoteFile, !pData->tryAbsolutePath).ToStdWstring()) + L" ";

			std::wstring localFile = QuoteFilename(pData->localFile);
			std::wstring logstr = cmd;
			logstr += localFile;
			LogMessageRaw(MessageType::Command, logstr);

			if (!AddToStream(cmd) || !AddToStream(localFile + L"\n", true)) {
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
		}
		else {
			engine_.transfer_status_.Init(pData->localFileSize, pData->resume ? pData->remoteFileSize : 0, false);
			cmd += L"put ";

			std::wstring logstr = cmd;
			std::wstring localFile = QuoteFilename(pData->localFile) + L" ";
			std::wstring remoteFile = QuoteFilename(pData->remotePath.FormatFilename(pData->remoteFile, !pData->tryAbsolutePath).ToStdWstring());

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

		pData->transferInitiated = true;
	}
	else if (pData->opState == filetransfer_mtime) {
		std::wstring quotedFilename = QuoteFilename(pData->remotePath.FormatFilename(pData->remoteFile, !pData->tryAbsolutePath).ToStdWstring());
		if (!SendCommand(_T("mtime ") + WildcardEscape(quotedFilename),
			L"mtime " + quotedFilename))
			return FZ_REPLY_ERROR;
	}
	else if (pData->opState == filetransfer_chmtime) {
		wxASSERT(!pData->fileTime.empty());
		if (pData->download) {
			LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("  filetransfer_chmtime during download"));
			ResetOperation(FZ_REPLY_INTERNALERROR);
			return FZ_REPLY_ERROR;
		}

		std::wstring quotedFilename = QuoteFilename(pData->remotePath.FormatFilename(pData->remoteFile, !pData->tryAbsolutePath).ToStdWstring());

		fz::datetime t = pData->fileTime;
		t -= fz::duration::from_minutes(m_pCurrentServer->GetTimezoneOffset());
		
		// Y2K38
		time_t ticks = t.get_time_t();
		std::wstring seconds = fz::sprintf(L"%d", ticks);
		if (!SendCommand(L"chmtime " + seconds + _T(" ") + WildcardEscape(quotedFilename),
			L"chmtime " + seconds + L" " + quotedFilename))
			return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpControlSocket::FileTransferParseResponse(int result, const wxString& reply)
{
	LogMessage(MessageType::Debug_Verbose, _T("FileTransferParseResponse(%d)"), result);

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
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
			if (pData->download) {
				if (!pData->fileTime.empty()) {
					if (!fz::local_filesys::set_modification_time(fz::to_native(pData->localFile), pData->fileTime))
						LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Could not set modification time"));
				}
			}
			else {
				pData->fileTime = fz::local_filesys::get_modification_time(fz::to_native(pData->localFile));
				if (!pData->fileTime.empty()) {
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
			for (unsigned int i = 0; i < reply.Len(); ++i) {
				wxChar c = reply[i];
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
					pData->fileTime = fileTime;
					pData->fileTime += fz::duration::from_minutes(m_pCurrentServer->GetTimezoneOffset());
				}
			}
		}
		pData->opState = filetransfer_transfer;
		int res = CheckOverwriteFile();
		if (res != FZ_REPLY_OK)
			return res;

		return SendNextCommand();
	}
	else if (pData->opState == filetransfer_chmtime) {
		if (pData->download) {
			LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("  filetransfer_chmtime during download"));
			ResetOperation(FZ_REPLY_INTERNALERROR);
			return FZ_REPLY_ERROR;
		}
	}
	else {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("  Called at improper time: opState == %d"), pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	ResetOperation(FZ_REPLY_OK);
	return FZ_REPLY_OK;
}

int CSftpControlSocket::DoClose(int nErrorCode /*=FZ_REPLY_DISCONNECTED*/)
{
	engine_.GetRateLimiter().RemoveObject(this);

	if (m_pProcess) {
		m_pProcess->kill();
	}

	if (m_pInputThread) {
		m_pInputThread->join();
		delete m_pInputThread;
		m_pInputThread = 0;

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
	if (m_pProcess) {
		delete m_pProcess;
		m_pProcess = 0;
	}
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

	if (!m_pCurOpData)
		LogMessage(MessageType::Status, _("Creating directory '%s'..."), path.GetPath());

	CMkdirOpData *pData = new CMkdirOpData;
	pData->path = path;

	if (!m_CurrentPath.empty()) {
		// Unless the server is broken, a directory already exists if current directory is a subdir of it.
		if (m_CurrentPath == path || m_CurrentPath.IsSubdirOf(path, false)) {
			delete pData;
			return FZ_REPLY_OK;
		}

		if (m_CurrentPath.IsParentOf(path, false))
			pData->commonParent = m_CurrentPath;
		else
			pData->commonParent = path.GetCommonParent(m_CurrentPath);
	}

	if (!path.HasParent())
		pData->opState = mkd_tryfull;
	else {
		pData->currentPath = path.GetParent();
		pData->segments.push_back(path.GetLastSegment());

		if (pData->currentPath == m_CurrentPath)
			pData->opState = mkd_mkdsub;
		else
			pData->opState = mkd_findparent;
	}

	pData->pNextOpData = m_pCurOpData;
	m_pCurOpData = pData;

	return SendNextCommand();
}

int CSftpControlSocket::MkdirParseResponse(bool successful, const wxString&)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::MkdirParseResonse"));

	if (!m_pCurOpData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CMkdirOpData *pData = static_cast<CMkdirOpData *>(m_pCurOpData);
	LogMessage(MessageType::Debug_Debug, _T("  state = %d"), pData->opState);

	bool error = false;
	switch (pData->opState)
	{
	case mkd_findparent:
		if (successful) {
			m_CurrentPath = pData->currentPath;
			pData->opState = mkd_mkdsub;
		}
		else if (pData->currentPath == pData->commonParent)
			pData->opState = mkd_tryfull;
		else if (pData->currentPath.HasParent()) {
			pData->segments.push_back(pData->currentPath.GetLastSegment());
			pData->currentPath = pData->currentPath.GetParent();
		}
		else
			pData->opState = mkd_tryfull;
		break;
	case mkd_mkdsub:
		if (successful) {
			if (pData->segments.empty()) {
				LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("  pData->segments is empty"));
				ResetOperation(FZ_REPLY_INTERNALERROR);
				return FZ_REPLY_ERROR;
			}
			engine_.GetDirectoryCache().UpdateFile(*m_pCurrentServer, pData->currentPath, pData->segments.back(), true, CDirectoryCache::dir);
			engine_.SendDirectoryListingNotification(pData->currentPath, false, true, false);

			pData->currentPath.AddSegment(pData->segments.back());
			pData->segments.pop_back();

			if (pData->segments.empty()) {
				ResetOperation(FZ_REPLY_OK);
				return FZ_REPLY_OK;
			}
			else
				pData->opState = mkd_cwdsub;
		}
		else
			pData->opState = mkd_tryfull;
		break;
	case mkd_cwdsub:
		if (successful) {
			m_CurrentPath = pData->currentPath;
			pData->opState = mkd_mkdsub;
		}
		else
			pData->opState = mkd_tryfull;
		break;
	case mkd_tryfull:
		if (!successful)
			error = true;
		else {
			ResetOperation(FZ_REPLY_OK);
			return FZ_REPLY_OK;
		}
		break;
	default:
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("unknown op state: %d"), pData->opState);
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
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::MkdirSend"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CMkdirOpData *pData = static_cast<CMkdirOpData *>(m_pCurOpData);
	LogMessage(MessageType::Debug_Debug, _T("  state = %d"), pData->opState);

	if (!pData->holdsLock) {
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
		res = SendCommand(L"cd " + QuoteFilename(pData->currentPath.GetPath().ToStdWstring()));
		break;
	case mkd_mkdsub:
		res = SendCommand(L"mkdir " + QuoteFilename(pData->segments.back().ToStdWstring()));
		break;
	case mkd_tryfull:
		res = SendCommand(L"mkdir " + QuoteFilename(pData->path.GetPath().ToStdWstring()));
		break;
	default:
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("unknown op state: %d"), pData->opState);
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
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::Delete"));
	wxASSERT(!m_pCurOpData);
	CSftpDeleteOpData *pData = new CSftpDeleteOpData();
	m_pCurOpData = pData;
	pData->path = path;
	pData->files = files;

	// CFileZillaEnginePrivate should have checked this already
	wxASSERT(!files.empty());

	return SendNextCommand();
}

int CSftpControlSocket::DeleteParseResponse(bool successful, const wxString&)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::DeleteParseResponse"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	CSftpDeleteOpData *pData = static_cast<CSftpDeleteOpData *>(m_pCurOpData);

	if (!successful) {
		pData->m_deleteFailed = true;
	}
	else {
		const wxString& file = pData->files.front();

		engine_.GetDirectoryCache().RemoveFile(*m_pCurrentServer, pData->path, file);

		auto const now = fz::datetime::now();
		if (!pData->m_time.empty() && (now - pData->m_time).get_seconds() >= 1) {
			engine_.SendDirectoryListingNotification(pData->path, false, true, false);
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
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::DeleteSend"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}
	CSftpDeleteOpData *pData = static_cast<CSftpDeleteOpData *>(m_pCurOpData);

	const wxString& file = pData->files.front();
	if (file.empty()) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty filename"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	std::wstring filename = pData->path.FormatFilename(file).ToStdWstring();
	if (filename.empty()) {
		LogMessage(MessageType::Error, _("Filename cannot be constructed for directory %s and filename %s"), pData->path.GetPath(), file);
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	if (pData->m_time.empty()) {
		pData->m_time = fz::datetime::now();
	}

	engine_.GetDirectoryCache().InvalidateFile(*m_pCurrentServer, pData->path, file);

	if (!SendCommand(L"rm " + WildcardEscape(QuoteFilename(filename)),
			  L"rm " + QuoteFilename(filename)))
		return FZ_REPLY_ERROR;

	return FZ_REPLY_WOULDBLOCK;
}

class CSftpRemoveDirOpData : public COpData
{
public:
	CSftpRemoveDirOpData()
		: COpData(Command::removedir)
	{
	}

	virtual ~CSftpRemoveDirOpData() {}

	CServerPath path;
	wxString subDir;
};

int CSftpControlSocket::RemoveDir(const CServerPath& path /*=CServerPath()*/, const wxString& subDir /*=_T("")*/)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::RemoveDir"));

	wxASSERT(!m_pCurOpData);
	CSftpRemoveDirOpData *pData = new CSftpRemoveDirOpData();
	m_pCurOpData = pData;
	pData->path = path;
	pData->subDir = subDir;

	CServerPath fullPath = engine_.GetPathCache().Lookup(*m_pCurrentServer, pData->path, pData->subDir);
	if (fullPath.empty()) {
		fullPath = pData->path;

		if (!fullPath.AddSegment(subDir)) {
			LogMessage(MessageType::Error, _("Path cannot be constructed for directory %s and subdir %s"), path.GetPath(), subDir);
			return FZ_REPLY_ERROR;
		}
	}

	engine_.GetDirectoryCache().InvalidateFile(*m_pCurrentServer, path, subDir);

	engine_.GetPathCache().InvalidatePath(*m_pCurrentServer, pData->path, pData->subDir);

	engine_.InvalidateCurrentWorkingDirs(fullPath);
	std::wstring quotedFilename = QuoteFilename(fullPath.GetPath().ToStdWstring());
	if (!SendCommand(L"rmdir " + WildcardEscape(quotedFilename),
			  L"rmdir " + quotedFilename))
		return FZ_REPLY_ERROR;

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpControlSocket::RemoveDirParseResponse(bool successful, const wxString&)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::RemoveDirParseResponse"));

	if (!m_pCurOpData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty m_pCurOpData"));
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
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Info, _T("Empty pData->path"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	engine_.GetDirectoryCache().RemoveDir(*m_pCurrentServer, pData->path, pData->subDir, engine_.GetPathCache().Lookup(*m_pCurrentServer, pData->path, pData->subDir));
	engine_.SendDirectoryListingNotification(pData->path, false, true, false);

	return ResetOperation(FZ_REPLY_OK);
}

class CSftpChmodOpData : public COpData
{
public:
	CSftpChmodOpData(const CChmodCommand& command)
		: COpData(Command::chmod), m_cmd(command)
	{
		m_useAbsolute = false;
	}

	virtual ~CSftpChmodOpData() {}

	CChmodCommand m_cmd;
	bool m_useAbsolute;
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
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData not empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	LogMessage(MessageType::Status, _("Set permissions of '%s' to '%s'"), command.GetPath().FormatFilename(command.GetFile()), command.GetPermission());

	CSftpChmodOpData *pData = new CSftpChmodOpData(command);
	pData->opState = chmod_chmod;
	m_pCurOpData = pData;

	int res = ChangeDir(command.GetPath());
	if (res != FZ_REPLY_OK)
		return res;

	return SendNextCommand();
}

int CSftpControlSocket::ChmodParseResponse(bool successful, const wxString&)
{
	CSftpChmodOpData *pData = static_cast<CSftpChmodOpData*>(m_pCurOpData);
	if (!pData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!successful)
	{
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	ResetOperation(FZ_REPLY_OK);
	return FZ_REPLY_OK;
}

int CSftpControlSocket::ChmodSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ChmodSend()"));

	CSftpChmodOpData *pData = static_cast<CSftpChmodOpData*>(m_pCurOpData);
	if (!pData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (prevResult != FZ_REPLY_OK)
		pData->m_useAbsolute = true;

	return SendNextCommand();
}

int CSftpControlSocket::ChmodSend()
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ChmodSend()"));

	CSftpChmodOpData *pData = static_cast<CSftpChmodOpData*>(m_pCurOpData);
	if (!pData) {
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	bool res;
	switch (pData->opState)
	{
	case chmod_chmod:
		{
			engine_.GetDirectoryCache().UpdateFile(*m_pCurrentServer, pData->m_cmd.GetPath(), pData->m_cmd.GetFile(), false, CDirectoryCache::unknown);

			std::wstring quotedFilename = QuoteFilename(pData->m_cmd.GetPath().FormatFilename(pData->m_cmd.GetFile(), !pData->m_useAbsolute).ToStdWstring());

			res = SendCommand(L"chmod " + pData->m_cmd.GetPermission().ToStdWstring() + L" " + WildcardEscape(quotedFilename),
					   L"chmod " + pData->m_cmd.GetPermission().ToStdWstring() + _T(" ") + quotedFilename);
		}
		break;
	default:
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("unknown op state: %d"), pData->opState);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!res) {
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

class CSftpRenameOpData : public COpData
{
public:
	CSftpRenameOpData(const CRenameCommand& command)
		: COpData(Command::rename), m_cmd(command)
	{
		m_useAbsolute = false;
	}

	virtual ~CSftpRenameOpData() {}

	CRenameCommand m_cmd;
	bool m_useAbsolute;
};

enum renameStates
{
	rename_init = 0,
	rename_rename
};

int CSftpControlSocket::Rename(const CRenameCommand& command)
{
	if (m_pCurOpData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData not empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	LogMessage(MessageType::Status, _("Renaming '%s' to '%s'"), command.GetFromPath().FormatFilename(command.GetFromFile()), command.GetToPath().FormatFilename(command.GetToFile()));

	CSftpRenameOpData *pData = new CSftpRenameOpData(command);
	pData->opState = rename_rename;
	m_pCurOpData = pData;

	int res = ChangeDir(command.GetFromPath());
	if (res != FZ_REPLY_OK)
		return res;

	return SendNextCommand();
}

int CSftpControlSocket::RenameParseResponse(bool successful, const wxString&)
{
	CSftpRenameOpData *pData = static_cast<CSftpRenameOpData*>(m_pCurOpData);
	if (!pData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (!successful)
	{
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	const CServerPath& fromPath = pData->m_cmd.GetFromPath();
	const CServerPath& toPath = pData->m_cmd.GetToPath();

	engine_.GetDirectoryCache().Rename(*m_pCurrentServer, fromPath, pData->m_cmd.GetFromFile(), toPath, pData->m_cmd.GetToFile());

	engine_.SendDirectoryListingNotification(fromPath, false, true, false);
	if (fromPath != toPath)
		engine_.SendDirectoryListingNotification(toPath, false, true, false);

	ResetOperation(FZ_REPLY_OK);
	return FZ_REPLY_OK;
}

int CSftpControlSocket::RenameSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::RenameSubcommandResult()"));

	CSftpRenameOpData *pData = static_cast<CSftpRenameOpData*>(m_pCurOpData);
	if (!pData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (prevResult != FZ_REPLY_OK)
		pData->m_useAbsolute = true;

	return SendNextCommand();
}

int CSftpControlSocket::RenameSend()
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::RenameSend()"));

	CSftpRenameOpData *pData = static_cast<CSftpRenameOpData*>(m_pCurOpData);
	if (!pData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("m_pCurOpData empty"));
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	bool res;
	switch (pData->opState)
	{
	case rename_rename:
		{
			bool wasDir = false;
			engine_.GetDirectoryCache().InvalidateFile(*m_pCurrentServer, pData->m_cmd.GetFromPath(), pData->m_cmd.GetFromFile(), &wasDir);
			engine_.GetDirectoryCache().InvalidateFile(*m_pCurrentServer, pData->m_cmd.GetToPath(), pData->m_cmd.GetToFile());

			std::wstring fromQuoted = QuoteFilename(pData->m_cmd.GetFromPath().FormatFilename(pData->m_cmd.GetFromFile(), !pData->m_useAbsolute).ToStdWstring());
			std::wstring toQuoted = QuoteFilename(pData->m_cmd.GetToPath().FormatFilename(pData->m_cmd.GetToFile(), !pData->m_useAbsolute && pData->m_cmd.GetFromPath() == pData->m_cmd.GetToPath()).ToStdWstring());

			engine_.GetPathCache().InvalidatePath(*m_pCurrentServer, pData->m_cmd.GetFromPath(), pData->m_cmd.GetFromFile());
			engine_.GetPathCache().InvalidatePath(*m_pCurrentServer, pData->m_cmd.GetToPath(), pData->m_cmd.GetToFile());

			if (wasDir) {
				// Need to invalidate current working directories
				CServerPath path = engine_.GetPathCache().Lookup(*m_pCurrentServer, pData->m_cmd.GetFromPath(), pData->m_cmd.GetFromFile());
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
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("unknown op state: %d"), pData->opState);
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
		if (bytes > INT_MAX)
			b = INT_MAX;
		else
			b = bytes;
		AddToStream(wxString::Format(_T("-%d%d,%d\n"), (int)direction, b, engine_.GetOptions().GetOptionVal(OPTION_SPEEDLIMIT_INBOUND + static_cast<int>(direction))));
		UpdateUsage(direction, b);
	}
	else if (bytes == 0)
		Wait(direction);
	else if (bytes < 0)
		AddToStream(wxString::Format(_T("-%d-\n"), (int)direction));
}


int CSftpControlSocket::ParseSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, _T("CSftpControlSocket::ParseSubcommandResult(%d)"), prevResult);
	if (!m_pCurOpData)
	{
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("ParseSubcommandResult called without active operation"));
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	switch (m_pCurOpData->opId)
	{
	case Command::cwd:
		return ChangeDirSubcommandResult(prevResult);
	case Command::list:
		return ListSubcommandResult(prevResult);
	case Command::transfer:
		return FileTransferSubcommandResult(prevResult);
	case Command::rename:
		return RenameSubcommandResult(prevResult);
	case Command::chmod:
		return ChmodSubcommandResult(prevResult);
	default:
		LogMessage(__TFILE__, __LINE__, this, MessageType::Debug_Warning, _T("Unknown opID (%d) in ParseSubcommandResult"), m_pCurOpData->opId);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		break;
	}

	return FZ_REPLY_ERROR;
}

int CSftpControlSocket::ListCheckTimezoneDetection()
{
	wxASSERT(m_pCurOpData);

	CSftpListOpData *pData = static_cast<CSftpListOpData *>(m_pCurOpData);

	if (CServerCapabilities::GetCapability(*m_pCurrentServer, timezone_offset) == unknown) {
		const int count = pData->directoryListing.GetCount();
		for (int i = 0; i < count; ++i) {
			if (!pData->directoryListing[i].has_time())
				continue;

			if (pData->directoryListing[i].is_link())
				continue;

			pData->opState = list_mtime;
			pData->mtime_index = i;
			return SendNextCommand();
		}
	}

	return FZ_REPLY_OK;
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
