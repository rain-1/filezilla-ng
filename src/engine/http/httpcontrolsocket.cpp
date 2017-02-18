#include <filezilla.h>

#include "connect.h"
#include "ControlSocket.h"
#include "engineprivate.h"
#include "filetransfer.h"
#include "httpcontrolsocket.h"
#include "internalconnect.h"
#include "request.h"
#include "tlssocket.h"
#include "uri.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>

/*
#define FZ_REPLY_REDIRECTED FZ_REPLY_ALREADYCONNECTED

// Connect is special for HTTP: It is done on a per-command basis, so we need
// to establish a connection before each command.
class CHttpInternalConnectOpData final : public CConnectOpData, public CHttpOpData
{
public:
	CHttpInternalConnectOpData(CHttpControlSocket& controlSocket)
		: CConnectOpData(CServer())
		, CHttpOpData(controlSocket)
	{}

	virtual int Send() { return FZ_REPLY_NOTSUPPORTED; }

	virtual int ParseResponse() { return FZ_REPLY_INTERNALERROR; }

	bool tls_{};
};

class CHttpRequestOpData : public COpData, public CHttpOpData
{
public:
	CHttpRequestOpData(CHttpControlSocket& controlSocket)
		: COpData(Command::rawtransfer)
		, CHttpOpData(controlSocket)
	{}

	bool m_gotHeader{};
	int m_responseCode{-1};
	std::wstring m_responseString;
	fz::uri m_newLocation;
	int m_redirectionCount{};

	int64_t m_totalSize{-1};
	int64_t m_receivedData{};

	enum transferEncodings
	{
		identity,
		chunked,
		unknown
	};
	transferEncodings m_transferEncoding{unknown};

	struct t_chunkData
	{
		bool getTrailer{};
		bool terminateChunk{};
		int64_t size{};
	} m_chunkData;
};

class CHttpFileTransferOpData final : public CFileTransferOpData, public CHttpOpData
{
public:
	CHttpFileTransferOpData(CHttpControlSocket & controlSocket, bool is_download, std::wstring const& local_file, std::wstring const& remote_file, const CServerPath& remote_path)
		: CFileTransferOpData(is_download, local_file, remote_file, remote_path)
		, CHttpOpData(controlSocket)
	{
	}

	fz::file file;
};
*/

CHttpControlSocket::CHttpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
}

CHttpControlSocket::~CHttpControlSocket()
{
	remove_handler();
	DoClose();
}

int CHttpControlSocket::SendNextCommand()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::SendNextCommand()");
	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Warning, L"SendNextCommand called without active operation");
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	while (m_pCurOpData) {
		if (m_pCurOpData->waitForAsyncRequest) {
			LogMessage(MessageType::Debug_Info, L"Waiting for async request, ignoring SendNextCommand...");
			return FZ_REPLY_WOULDBLOCK;
		}

		int res = m_pCurOpData->Send();
		if (res != FZ_REPLY_CONTINUE) {
			if (res == FZ_REPLY_OK) {
				return ResetOperation(res);
			}
			else if ((res & FZ_REPLY_DISCONNECTED) == FZ_REPLY_DISCONNECTED) {
				return DoClose(res);
			}
			else if (res & FZ_REPLY_ERROR) {
				return ResetOperation(res);
			}
			else if (res == FZ_REPLY_WOULDBLOCK) {
				return FZ_REPLY_WOULDBLOCK;
			}
			else if (res != FZ_REPLY_CONTINUE) {
				LogMessage(MessageType::Debug_Warning, L"Unknown result %d returned by m_pCurOpData->Send()");
				return ResetOperation(FZ_REPLY_INTERNALERROR);
			}
		}
	}

	return FZ_REPLY_OK;
}

/*
int CHttpControlSocket::ContinueConnect()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::ContinueConnect() &engine_=%p", &engine_);
	if (GetCurrentCommandId() != Command::connect ||
		!currentServer_)
	{
		LogMessage(MessageType::Debug_Warning, L"Invalid context for call to ContinueConnect(), cmd=%d, currentServer_ is %s", GetCurrentCommandId(), currentServer_ ? L"non-empty" : L"empty");
		return DoClose(FZ_REPLY_INTERNALERROR);
	}

	ResetOperation(FZ_REPLY_OK);
	return FZ_REPLY_OK;
}
*/

bool CHttpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (m_pCurOpData) {
		if (!m_pCurOpData->waitForAsyncRequest) {
			LogMessage(MessageType::Debug_Info, L"Not waiting for request reply, ignoring request reply %d", pNotification->GetRequestID());
			return false;
		}
		m_pCurOpData->waitForAsyncRequest = false;
	}

	switch (pNotification->GetRequestID())
	{
	case reqId_fileexists:
		{
			if (!m_pCurOpData || m_pCurOpData->opId != Command::transfer) {
				LogMessage(MessageType::Debug_Info, L"No or invalid operation in progress, ignoring request reply %f", pNotification->GetRequestID());
				return false;
			}

			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
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
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown request %d", pNotification->GetRequestID());
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}

/*
void CHttpControlSocket::OnReceive()
{
	DoReceive();
}

int CHttpControlSocket::DoReceive()
{
	do {
		const CSocket::SocketState state = m_pSocket->GetState();
		if (state != CSocket::connected && state != CSocket::closing) {
			return 0;
		}

		if (!m_pRecvBuffer) {
			m_pRecvBuffer = new char[m_recvBufferLen];
			m_recvBufferPos = 0;
		}

		unsigned int len = m_recvBufferLen - m_recvBufferPos;
		int error;
		int read = m_pBackend->Read(m_pRecvBuffer + m_recvBufferPos, len, error);
		if (read == -1) {
			if (error != EAGAIN) {
				ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
			}
			return 0;
		}

		SetActive(CFileZillaEngine::recv);

		if (!m_pCurOpData || m_pCurOpData->opId == Command::connect) {
			// Just ignore all further data
			m_recvBufferPos = 0;
			return 0;
		}

		auto httpRequestOpData = reinterpret_cast<CHttpRequestOpData*>(m_pCurOpData);

		m_recvBufferPos += read;

		if (!httpRequestOpData->m_gotHeader) {
			if (!read) {
				ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
				return 0;
			}

			int res = ParseHeader(httpOpData);
			if ((res & FZ_REPLY_REDIRECTED) == FZ_REPLY_REDIRECTED) {
				return FZ_REPLY_REDIRECTED;
			}
			if (res != FZ_REPLY_WOULDBLOCK) {
				return 0;
			}
		}
		else if (httpRequestOpData->m_transferEncoding == CHttpOpData::chunked) {
			if (!read) {
				ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
				return 0;
			}
			OnChunkedData(httpOpData);
		}
		else {
			if (!read) {
				assert(!m_recvBufferPos);
				ProcessData(0, 0);
				return 0;
			}
			else {
				httpRequestOpData->m_receivedData += m_recvBufferPos;
				ProcessData(m_pRecvBuffer, m_recvBufferPos);
				m_recvBufferPos = 0;
			}
		}
	}
	while (m_pSocket);

	return 0;
}
*/
void CHttpControlSocket::OnConnect()
{
	assert(GetCurrentCommandId() == PrivCommand::http_connect);

	CHttpInternalConnectOpData *pData = static_cast<CHttpInternalConnectOpData *>(m_pCurOpData);

	if (pData->tls_) {
		if (!m_pTlsSocket) {
			LogMessage(MessageType::Status, _("Connection established, initializing TLS..."));

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
		}
		else {
			LogMessage(MessageType::Status, _("TLS connection established, sending HTTP request"));
			ResetOperation(FZ_REPLY_OK);
		}

		return;
	}
	else {
		LogMessage(MessageType::Status, _("Connection established, sending HTTP request"));
		ResetOperation(FZ_REPLY_OK);
	}
}

int CHttpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::FileTransfer()");

	if (download) {
		LogMessage(MessageType::Status, _("Downloading %s"), remotePath.FormatFilename(remoteFile));
	}

	CHttpFileTransferOpData *pData = new CHttpFileTransferOpData(*this, download, localFile, remoteFile, remotePath);
	Push(pData);
	return FZ_REPLY_CONTINUE;
}

void CHttpControlSocket::Request(HttpRequest & request, HttpResponse & response)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::Request()");
	Push(new CHttpRequestOpData(*this, request, response));
}

/*
int CHttpControlSocket::FileTransferSubcommandResult(int prevResult)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::FileTransferSubcommandResult(%d)", prevResult);

	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Info, L"Empty m_pCurOpData");
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return FZ_REPLY_ERROR;
	}

	if (prevResult != FZ_REPLY_OK) {
		ResetOperation(prevResult);
		return FZ_REPLY_ERROR;
	}

	return FileTransferSend();
}

*/
void CHttpControlSocket::InternalConnect(std::wstring const& host, unsigned short port, bool tls)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::InternalConnect()");

	if (fz::get_address_type(host) == fz::address_type::unknown) {
		LogMessage(MessageType::Status, _("Resolving address of %s"), host);
	}

	Push(new CHttpInternalConnectOpData(*this, ConvertDomainName(host), port, tls));
}
/*
int CHttpControlSocket::ParseHeader(CHttpOpData* pData)
{
	// Parse the HTTP header.
	// We do just the neccessary parsing and silently ignore most header fields
	// Redirects are supported though if the server sends the Location field.

	for (;;) {
		// Find line ending
		unsigned int i = 0;
		for (i = 0; (i + 1) < m_recvBufferPos; i++) {
			if (m_pRecvBuffer[i] == '\r') {
				if (m_pRecvBuffer[i + 1] != '\n') {
					LogMessage(MessageType::Error, _("Malformed reply, server not sending proper line endings"));
					ResetOperation(FZ_REPLY_ERROR);
					return FZ_REPLY_ERROR;
				}
				break;
			}
		}
		if ((i + 1) >= m_recvBufferPos) {
			if (m_recvBufferPos == m_recvBufferLen) {
				// We don't support header lines larger than 4096
				LogMessage(MessageType::Error, _("Too long header line"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
			return FZ_REPLY_WOULDBLOCK;
		}

		m_pRecvBuffer[i] = 0;
		std::wstring const line = wxString(m_pRecvBuffer, wxConvLocal).ToStdWstring();
		if (!line.empty()) {
			LogMessageRaw(MessageType::Response, line);
		}

		if (pData->m_responseCode == -1) {
			pData->m_responseString = line;
			if (m_recvBufferPos < 16 || memcmp(m_pRecvBuffer, "HTTP/1.", 7)) {
				// Invalid HTTP Status-Line
				LogMessage(MessageType::Error, _("Invalid HTTP Response"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}

			if (m_pRecvBuffer[9] < '1' || m_pRecvBuffer[9] > '5' ||
				m_pRecvBuffer[10] < '0' || m_pRecvBuffer[10] > '9' ||
				m_pRecvBuffer[11] < '0' || m_pRecvBuffer[11] > '9')
			{
				// Invalid response code
				LogMessage(MessageType::Error, _("Invalid response code"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}

			pData->m_responseCode = (m_pRecvBuffer[9] - '0') * 100 + (m_pRecvBuffer[10] - '0') * 10 + m_pRecvBuffer[11] - '0';

			if (pData->m_responseCode == 416) {
				CHttpFileTransferOpData* pTransfer = static_cast<CHttpFileTransferOpData*>(pData);
				if (pTransfer->resume) {
					// Sad, the server does not like our attempt to resume.
					// Get full file instead.
					pTransfer->resume = false;
					int res = OpenFile(pTransfer);
					if (res != FZ_REPLY_OK) {
						return res;
					}
					pData->m_newLocation = m_current_uri;
					pData->m_responseCode = 300;
				}
			}

			if (pData->m_responseCode >= 400) {
				// Failed request
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}

			if (pData->m_responseCode == 305) {
				// Unsupported redirect
				LogMessage(MessageType::Error, _("Unsupported redirect"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
		}
		else {
			if (!i) {
				// End of header, data from now on

				// Redirect if neccessary
				if (pData->m_responseCode >= 300) {
					if (pData->m_redirectionCount++ == 6) {
						LogMessage(MessageType::Error, _("Too many redirects"));
						ResetOperation(FZ_REPLY_ERROR);
						return FZ_REPLY_ERROR;
					}

					ResetSocket();
					ResetHttpData(pData);

					if (pData->m_newLocation.scheme_.empty() || pData->m_newLocation.host_.empty() || !pData->m_newLocation.is_absolute()) {
						LogMessage(MessageType::Error, _("Redirection to invalid or unsupported URI: %s"), m_current_uri.to_string());
						ResetOperation(FZ_REPLY_ERROR);
						return FZ_REPLY_ERROR;
					}

					ServerProtocol protocol = CServer::GetProtocolFromPrefix(fz::to_wstring_from_utf8(pData->m_newLocation.scheme_));
					if (protocol != HTTP && protocol != HTTPS) {
						LogMessage(MessageType::Error, _("Redirection to invalid or unsupported address: %s"), pData->m_newLocation.to_string());
						ResetOperation(FZ_REPLY_ERROR);
						return FZ_REPLY_ERROR;
					}

					unsigned short port = CServer::GetDefaultPort(protocol);
					if (pData->m_newLocation.port_ != 0) {
						port = pData->m_newLocation.port_;
					}

					m_current_uri = pData->m_newLocation;

					// International domain names
					std::wstring host = fz::to_wstring_from_utf8(m_current_uri.host_);
					if (host.empty()) {
						LogMessage(MessageType::Error, _("Invalid hostname: %s"), pData->m_newLocation.to_string());
						ResetOperation(FZ_REPLY_ERROR);
						return FZ_REPLY_ERROR;
					}

					int res = InternalConnect(host, port, protocol == HTTPS);
					if (res == FZ_REPLY_WOULDBLOCK) {
						res |= FZ_REPLY_REDIRECTED;
					}
					return res;
				}

				if (static_cast<CHttpFileTransferOpData*>(pData)->opId == Command::transfer) {
					CHttpFileTransferOpData* pTransfer = static_cast<CHttpFileTransferOpData*>(pData);
					if (pTransfer->resume && pData->m_responseCode != 206) {
						pTransfer->resume = false;
						int res = OpenFile(pTransfer);
						if (res != FZ_REPLY_OK) {
							return res;
						}
					}
				}

				pData->m_gotHeader = true;

				memmove(m_pRecvBuffer, m_pRecvBuffer + 2, m_recvBufferPos - 2);
				m_recvBufferPos -= 2;

				if (m_recvBufferPos) {
					int res;
					if (pData->m_transferEncoding == pData->chunked) {
						res = OnChunkedData(pData);
					}
					else {
						pData->m_receivedData += m_recvBufferPos;
						res = ProcessData(m_pRecvBuffer, m_recvBufferPos);
						m_recvBufferPos = 0;
					}
					return res;
				}

				return FZ_REPLY_WOULDBLOCK;
			}
			if (m_recvBufferPos > 12 && !memcmp(m_pRecvBuffer, "Location: ", 10)) {
				pData->m_newLocation = fz::uri(m_pRecvBuffer + 10);
				if (!pData->m_newLocation.empty()) {
					pData->m_newLocation.resolve(m_current_uri);
				}
			}
			else if (m_recvBufferPos > 21 && !memcmp(m_pRecvBuffer, "Transfer-Encoding: ", 19)) {
				if (!strcmp(m_pRecvBuffer + 19, "chunked")) {
					pData->m_transferEncoding = CHttpOpData::chunked;
				}
				else if (!strcmp(m_pRecvBuffer + 19, "identity")) {
					pData->m_transferEncoding = CHttpOpData::identity;
				}
				else {
					pData->m_transferEncoding = CHttpOpData::unknown;
				}
			}
			else if (i > 16 && !memcmp(m_pRecvBuffer, "Content-Length: ", 16)) {
				pData->m_totalSize = 0;
				char* p = m_pRecvBuffer + 16;
				while (*p) {
					if (*p < '0' || *p > '9') {
						LogMessage(MessageType::Error, _("Malformed header: %s"), _("Invalid Content-Length"));
						ResetOperation(FZ_REPLY_ERROR);
						return FZ_REPLY_ERROR;
					}
					pData->m_totalSize = pData->m_totalSize * 10 + *p++ - '0';
				}
			}
		}

		memmove(m_pRecvBuffer, m_pRecvBuffer + i + 2, m_recvBufferPos - i - 2);
		m_recvBufferPos -= i + 2;

		if (!m_recvBufferPos) {
			break;
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpControlSocket::OnChunkedData(CHttpOpData* pData)
{
	char* p = m_pRecvBuffer;
	unsigned int len = m_recvBufferPos;

	for (;;) {
		if (pData->m_chunkData.size != 0) {
			unsigned int dataLen = len;
			if (pData->m_chunkData.size < len) {
				dataLen = static_cast<unsigned int>(pData->m_chunkData.size);
			}
			pData->m_receivedData += dataLen;
			int res = ProcessData(p, dataLen);
			if (res != FZ_REPLY_WOULDBLOCK) {
				return res;
			}

			pData->m_chunkData.size -= dataLen;
			p += dataLen;
			len -= dataLen;

			if (pData->m_chunkData.size == 0) {
				pData->m_chunkData.terminateChunk = true;
			}

			if (!len) {
				break;
			}
		}

		// Find line ending
		unsigned int i = 0;
		for (i = 0; (i + 1) < len; ++i) {
			if (p[i] == '\r') {
				if (p[i + 1] != '\n') {
					LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Wrong line endings"));
					ResetOperation(FZ_REPLY_ERROR);
					return FZ_REPLY_ERROR;
				}
				break;
			}
		}
		if ((i + 1) >= len) {
			if (len == m_recvBufferLen) {
				// We don't support lines larger than 4096
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Line length exceeded"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
			break;
		}

		p[i] = 0;

		if (pData->m_chunkData.terminateChunk) {
			if (i) {
				// The chunk data has to end with CRLF. If i is nonzero,
				// it didn't end with just CRLF.
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Chunk data improperly terminated"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
			pData->m_chunkData.terminateChunk = false;
		}
		else if (pData->m_chunkData.getTrailer) {
			if (!i) {
				// We're done
				return ProcessData(0, 0);
			}

			// Ignore the trailer
		}
		else
		{
			// Read chunk size
			for (char* q = p; *q && *q != ';' && *q != ' '; ++q) {
				pData->m_chunkData.size *= 16;
				if (*q >= '0' && *q <= '9') {
					pData->m_chunkData.size += *q - '0';
				}
				else if (*q >= 'A' && *q <= 'F') {
					pData->m_chunkData.size += *q - 'A' + 10;
				}
				else if (*q >= 'a' && *q <= 'f') {
					pData->m_chunkData.size += *q - 'a' + 10;
				}
				else {
					// Invalid size
					LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Invalid chunk size"));
					ResetOperation(FZ_REPLY_ERROR);
					return FZ_REPLY_ERROR;
				}
			}
			if (!pData->m_chunkData.size) {
				pData->m_chunkData.getTrailer = true;
			}
		}

		p += i + 2;
		len -= i + 2;

		if (!len) {
			break;
		}
	}

	if (p != m_pRecvBuffer) {
		memmove(m_pRecvBuffer, p, len);
		m_recvBufferPos = len;
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpControlSocket::ResetOperation(int nErrorCode)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::ResetOperation(%d)", nErrorCode);

	if (m_pCurOpData && m_pCurOpData->opId == Command::transfer) {
		LogMessage(MessageType::Debug_Debug, L"Resetting a transfer, closing the file");
		CHttpFileTransferOpData *pData = static_cast<CHttpFileTransferOpData *>(m_pCurOpData);
		pData->file.close();
	}

	if (!m_pCurOpData || !m_pCurOpData->pNextOpData) {
		if (m_pBackend) {
			if (nErrorCode == FZ_REPLY_OK) {
				LogMessage(MessageType::Status, _("Disconnected from server"));
			}
			else {
				LogMessage(MessageType::Error, _("Disconnected from server"));
			}
		}
		ResetSocket();
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

void CHttpControlSocket::OnClose(int error)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::OnClose(%d)", error);

	if (error) {
		LogMessage(MessageType::Error, _("Disconnected from server: %s"), CSocket::GetErrorDescription(error));
		ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
		return;
	}

	// HTTP socket isn't connected outside operations
	if (!m_pCurOpData) {
		return;
	}

	if (m_pCurOpData->pNextOpData) {
		LogMessageRaw(MessageType::Debug_Verbose, L"Socket closed inside a nested operation");
		ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
		return;
	}

	auto httpOpData = reinterpret_cast<CHttpOpData*>(m_pCurOpData);
	if (!httpRequestOpData->m_gotHeader) {
		LogMessageRaw(MessageType::Debug_Verbose, L"Socket closed, headers not received");
		ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
		return;
	}

	if (httpRequestOpData->m_transferEncoding == CHttpOpData::chunked) {
		if (!httpRequestOpData->m_chunkData.getTrailer) {
			LogMessageRaw(MessageType::Debug_Verbose, L"Socket closed, chunk incomplete");
			ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
			return;
		}
	}
	else {
		if (httpRequestOpData->m_totalSize != -1 && httpRequestOpData->m_receivedData != httpRequestOpData->m_totalSize) {
			LogMessageRaw(MessageType::Debug_Verbose, L"Socket closed, content length not reached");
			ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
			return;
		}
	}

	ProcessData(0, 0);
}

void CHttpControlSocket::ResetSocket()
{
	delete[] m_pRecvBuffer;
	m_pRecvBuffer = 0;
	m_recvBufferPos = 0;

	if (m_pTlsSocket) {
		if (m_pTlsSocket != m_pBackend) {
			delete m_pTlsSocket;
		}
		m_pTlsSocket = 0;
	}

	CRealControlSocket::ResetSocket();
}

void CHttpControlSocket::ResetHttpData(CHttpOpData* pData)
{
	assert(pData);

	pData->m_gotHeader = false;
	pData->m_responseCode = -1;
	pData->m_transferEncoding = CHttpOpData::unknown;

	pData->m_chunkData.getTrailer = false;
	pData->m_chunkData.size = 0;
	pData->m_chunkData.terminateChunk = false;

	pData->m_totalSize = -1;
	pData->m_receivedData = 0;
}

int CHttpControlSocket::ProcessData(char* p, int len)
{
	int res;
	Command commandId = GetCurrentCommandId();
	switch (commandId)
	{
	case Command::transfer:
		res = FileTransferParseResponse(p, len);
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"No action for parsing data for command %d", commandId);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		res = FZ_REPLY_ERROR;
		break;
	}

	assert(p || !m_pCurOpData);

	return res;
}
*/

int CHttpControlSocket::ParseSubcommandResult(int prevResult, COpData const& opData)
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::ParseSubcommandResult(%d)", prevResult);
	if (!m_pCurOpData) {
		LogMessage(MessageType::Debug_Warning, L"ParseSubcommandResult called without active operation");
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	int res = m_pCurOpData->SubcommandResult(prevResult, opData);
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

int CHttpControlSocket::Disconnect()
{
	DoClose();
	return FZ_REPLY_OK;
}

void CHttpControlSocket::Connect(CServer const& server)
{
	currentServer_ = server;
	Push(new CHttpConnectOpData(*this));
}
