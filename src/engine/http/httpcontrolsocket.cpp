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

CHttpControlSocket::CHttpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
}

CHttpControlSocket::~CHttpControlSocket()
{
	remove_handler();
	DoClose();
}

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


void CHttpControlSocket::OnReceive()
{
	if (!m_pCurOpData || m_pCurOpData->opId != PrivCommand::http_request) {
		LogMessage(MessageType::Debug_Warning, L"OnReceive called while not processing http request");
	}

	int res = static_cast<CHttpRequestOpData*>(m_pCurOpData)->OnReceive();
	if (res == FZ_REPLY_CONTINUE) {
		SendNextCommand();
	}
	else if (res != FZ_REPLY_WOULDBLOCK) {
		ResetOperation(res);
	}
}

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

void CHttpControlSocket::InternalConnect(std::wstring const& host, unsigned short port, bool tls)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::InternalConnect()");

	if (fz::get_address_type(host) == fz::address_type::unknown) {
		LogMessage(MessageType::Status, _("Resolving address of %s"), host);
	}

	Push(new CHttpInternalConnectOpData(*this, ConvertDomainName(host), port, tls));
}

int CHttpControlSocket::ResetOperation(int nErrorCode)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::ResetOperation(%d)", nErrorCode);

	if (m_pCurOpData && m_pCurOpData->opId == PrivCommand::http_request) {
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

	if (m_pCurOpData && m_pCurOpData->opId == PrivCommand::http_request) {
		auto requestData = reinterpret_cast<CHttpRequestOpData*>(m_pCurOpData);
		int res = requestData->OnClose();
		ResetOperation(res);
	}
	else {
		ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
	}
}

void CHttpControlSocket::ResetSocket()
{
	if (m_pTlsSocket) {
		if (m_pTlsSocket != m_pBackend) {
			delete m_pTlsSocket;
		}
		m_pTlsSocket = 0;
	}

	CRealControlSocket::ResetSocket();
}

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
