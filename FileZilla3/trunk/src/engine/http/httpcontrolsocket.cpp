#include <filezilla.h>

#include "connect.h"
#include "ControlSocket.h"
#include "engineprivate.h"
#include "filetransfer.h"
#include "httpcontrolsocket.h"
#include "internalconnect.h"
#include "request.h"
#include "tlssocket.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/uri.hpp>

void HttpResponse::reset()
{
	flags_ = 0;
	code_ = 0;
	headers_.clear();

}

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
	if (operations_.empty() || !operations_.back()->waitForAsyncRequest) {
		LogMessage(MessageType::Debug_Info, L"Not waiting for request reply, ignoring request reply %d", pNotification->GetRequestID());
	}

	operations_.back()->waitForAsyncRequest = false;

	switch (pNotification->GetRequestID())
	{
	case reqId_fileexists:
		{
			if (operations_.back()->opId != Command::transfer) {
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
	if (operations_.empty() || operations_.back()->opId != PrivCommand::http_request) {
		uint8_t buffer;
		int error{};
		int read = m_pBackend->Read(&buffer, 1, error);
		if (!read) {
			LogMessage(MessageType::Debug_Warning, L"Idle socket got closed");
			ResetSocket();
		}
		else if (read == -1) {
			if (error != EAGAIN) {
				LogMessage(MessageType::Debug_Warning, L"OnReceive called while not processing http request");
				ResetSocket();
			}
		}
		else if (read) {
			LogMessage(MessageType::Debug_Warning, L"Server sent data while not in an active HTTP request");
			ResetSocket();
		}
		return;
	}

	int res = static_cast<CHttpRequestOpData&>(*operations_.back()).OnReceive();
	if (res == FZ_REPLY_CONTINUE) {
		SendNextCommand();
	}
	else if (res != FZ_REPLY_WOULDBLOCK) {
		ResetOperation(res);
	}
}

void CHttpControlSocket::OnConnect()
{
	if (operations_.empty() || operations_.back()->opId != PrivCommand::http_connect) {
		LogMessage(MessageType::Debug_Warning, L"Discarding stale OnConnect");
		return;
	}

	auto & data = static_cast<CHttpInternalConnectOpData &>(*operations_.back());

	if (data.tls_) {
		if (!m_pTlsSocket) {
			LogMessage(MessageType::Status, _("Connection established, initializing TLS..."));

			delete m_pBackend;
			m_pTlsSocket = new CTlsSocket(this, *socket_, this);
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
	}
	else {
		LogMessage(MessageType::Status, _("Connection established, sending HTTP request"));
		ResetOperation(FZ_REPLY_OK);
	}
}

void CHttpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::FileTransfer()");

	if (download) {
		LogMessage(MessageType::Status, _("Downloading %s"), remotePath.FormatFilename(remoteFile));
	}

	Push(std::make_unique<CHttpFileTransferOpData>(*this, download, localFile, remoteFile, remotePath));
}

void CHttpControlSocket::Request(HttpRequest & request, HttpResponse & response)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::Request()");
	Push(std::make_unique<CHttpRequestOpData>(*this, request, response));
}

void CHttpControlSocket::InternalConnect(std::wstring const& host, unsigned short port, bool tls)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::InternalConnect()");

	if (Connected() && m_pBackend && host == connected_host_ && port == connected_port_ && tls == connected_tls_) {
		LogMessage(MessageType::Debug_Verbose, L"Reusing an existing connection");
		is_reusing_ = true;
	}
	else {
		ResetSocket();
		connected_host_ = host;
		connected_port_ = port;
		connected_tls_ = tls;
		Push(std::make_unique<CHttpInternalConnectOpData>(*this, ConvertDomainName(host), port, tls));
		is_reusing_ = false;
	}
}

void CHttpControlSocket::OnClose(int error)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::OnClose(%d)", error);

	if (operations_.empty() || (operations_.back()->opId != PrivCommand::http_connect && operations_.back()->opId != PrivCommand::http_request)) {
		LogMessage(MessageType::Debug_Warning, L"Idle socket got closed");
		ResetSocket();
		return;
	}

	if (error) {
		LogMessage(MessageType::Error, _("Disconnected from server: %s"), fz::socket::error_description(error));
		ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
		return;
	}

	if (!operations_.empty() && operations_.back()->opId == PrivCommand::http_request) {
		auto & data = static_cast<CHttpRequestOpData&>(*operations_.back());
		int res = data.OnClose();
		ResetOperation(res);
	}
	else {
		ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
	}
}

void CHttpControlSocket::ResetSocket()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpControlSocket::ResetSocket()");
	if (m_pTlsSocket) {
		if (m_pTlsSocket != m_pBackend) {
			delete m_pTlsSocket;
		}
		m_pTlsSocket = nullptr;
	}

	CRealControlSocket::ResetSocket();
}

int CHttpControlSocket::Disconnect()
{
	DoClose();
	return FZ_REPLY_OK;
}

void CHttpControlSocket::Connect(CServer const& server, Credentials const&)
{
	currentServer_ = server;
	Push(std::make_unique<CHttpConnectOpData>(*this));
}


int CHttpControlSocket::OnSend()
{
	int res = CRealControlSocket::OnSend();
	if (res == FZ_REPLY_CONTINUE) {
		if (!operations_.empty() && operations_.back()->opId == PrivCommand::http_request && operations_.back()->opState == request_send) {
			return SendNextCommand();
		}
	}
	return res;
}
