#include <filezilla.h>
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "ftp/ftpcontrolsocket.h"
#include "iothread.h"
#include "optionsbase.h"
#include "tlssocket.h"
#include "transfersocket.h"
#include "proxy.h"
#include "servercapabilities.h"

#include <libfilezilla/util.hpp>

CTransferSocket::CTransferSocket(CFileZillaEnginePrivate & engine, CFtpControlSocket & controlSocket, TransferMode transferMode)
: fz::event_handler(controlSocket.event_loop_)
, engine_(engine)
, controlSocket_(controlSocket)
, m_transferMode(transferMode)
{
}

CTransferSocket::~CTransferSocket()
{
	remove_handler();
	if (m_transferEndReason == TransferEndReason::none) {
		m_transferEndReason = TransferEndReason::successful;
	}
	ResetSocket();

	if (m_transferMode == TransferMode::upload || m_transferMode == TransferMode::download) {
		if (ioThread_) {
			if (m_transferMode == TransferMode::download) {
				FinalizeWrite();
			}
			ioThread_->SetEventHandler(0);
		}
	}
}

void CTransferSocket::ResetSocket()
{
	delete m_pProxyBackend;
	if (m_pBackend == m_pTlsSocket) {
		m_pBackend = 0;
	}
	delete m_pTlsSocket;
	delete m_pBackend;
	socketServer_.reset();
	socket_.reset();
	m_pProxyBackend = 0;
	m_pTlsSocket = 0;
	m_pBackend = 0;
}

std::wstring CTransferSocket::SetupActiveTransfer(std::string const& ip)
{
	ResetSocket();
	socketServer_ = CreateSocketServer();

	if (!socketServer_) {
		controlSocket_.LogMessage(MessageType::Debug_Warning, L"CreateSocketServer failed");
		return std::wstring();
	}

	int error;
	int port = socketServer_->GetLocalPort(error);
	if (port == -1)	{
		ResetSocket();

		controlSocket_.LogMessage(MessageType::Debug_Warning, L"GetLocalPort failed: %s", CSocket::GetErrorDescription(error));
		return std::wstring();
	}

	if (engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS)) {
		port += static_cast<int>(engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS_OFFSET));
		if (port <= 0 || port >= 65536) {
			controlSocket_.LogMessage(MessageType::Debug_Warning, L"Port outside valid range");
			return std::wstring();
		}
	}

	std::wstring portArguments;
	if (socketServer_->GetAddressFamily() == CSocket::ipv6) {
		portArguments = fz::sprintf(L"|2|%s|%d|", ip, port);
	}
	else {
		portArguments = fz::to_wstring(ip);
		fz::replace_substrings(portArguments, L".", L",");
		portArguments += fz::sprintf(L",%d,%d", port / 256, port % 256);
	}

	return portArguments;
}

void CTransferSocket::OnSocketEvent(CSocketEventSource*, SocketEventType t, int error)
{
	if (m_pProxyBackend) {
		switch (t)
		{
		case SocketEventType::connection:
			{
				if (error) {
					controlSocket_.LogMessage(MessageType::Error, _("Proxy handshake failed: %s"), CSocket::GetErrorDescription(error));
					TransferEnd(TransferEndReason::failure);
				}
				else {
					delete m_pProxyBackend;
					m_pProxyBackend = 0;
					OnConnect();
				}
			}
			return;
		case SocketEventType::close:
			{
				controlSocket_.LogMessage(MessageType::Error, _("Proxy handshake failed: %s"), CSocket::GetErrorDescription(error));
				TransferEnd(TransferEndReason::failure);
			}
			return;
		default:
			// Uninteresting
			break;
		}
		return;
	}

	if (socketServer_) {
		if (t == SocketEventType::connection) {
			OnAccept(error);
		}
		else {
			controlSocket_.LogMessage(MessageType::Debug_Info, L"Unhandled socket event %d from listening socket", t);
		}
		return;
	}

	switch (t)
	{
	case SocketEventType::connection:
		if (error) {
			if (m_transferEndReason == TransferEndReason::none) {
				controlSocket_.LogMessage(MessageType::Error, _("The data connection could not be established: %s"), CSocket::GetErrorDescription(error));
				TransferEnd(TransferEndReason::transfer_failure);
			}
		}
		else
			OnConnect();
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
		// Uninteresting
		break;
	}
}

void CTransferSocket::OnAccept(int error)
{
	controlSocket_.SetAlive();
	controlSocket_.LogMessage(MessageType::Debug_Verbose, L"CTransferSocket::OnAccept(%d)", error);

	if (!socketServer_) {
		controlSocket_.LogMessage(MessageType::Debug_Warning, L"No socket server in OnAccept", error);
		return;
	}

	socket_.reset(socketServer_->Accept(error));
	if (!socket_) {
		if (error == EAGAIN) {
			controlSocket_.LogMessage(MessageType::Debug_Verbose, L"No pending connection");
		}
		else {
			controlSocket_.LogMessage(MessageType::Status, _("Could not accept connection: %s"), CSocket::GetErrorDescription(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
		return;
	}
	socketServer_.reset();

	OnConnect();
}

void CTransferSocket::OnConnect()
{
	controlSocket_.SetAlive();
	controlSocket_.LogMessage(MessageType::Debug_Verbose, L"CTransferSocket::OnConnect");

	if (!socket_) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"CTransferSocket::OnConnect called without socket");
		return;
	}

	if (!m_pBackend) {
		if (!InitBackend()) {
			TransferEnd(TransferEndReason::transfer_failure);
			return;
		}
	}
	else if (m_pTlsSocket) {
		// Re-enable Nagle algorithm
		socket_->SetFlags(socket_->GetFlags() & (~CSocket::flag_nodelay));
		if (CServerCapabilities::GetCapability(controlSocket_.currentServer_, tls_resume) == unknown)	{
			CServerCapabilities::SetCapability(controlSocket_.currentServer_, tls_resume, m_pTlsSocket->ResumedSession() ? yes : no);
		}
	}

	if (m_bActive) {
		TriggerPostponedEvents();
	}
}

void CTransferSocket::OnReceive()
{
	controlSocket_.LogMessage(MessageType::Debug_Debug, L"CTransferSocket::OnReceive(), m_transferMode=%d", m_transferMode);

	if (!m_pBackend) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"Postponing receive, m_pBackend was false.");
		m_postponedReceive = true;
		return;
	}

	if (!m_bActive) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"Postponing receive, m_bActive was false.");
		m_postponedReceive = true;
		return;
	}

	if (m_transferMode == TransferMode::list) {
		for (;;) {
			char *pBuffer = new char[4096];
			int error;
			int numread = m_pBackend->Read(pBuffer, 4096, error);
			if (numread < 0) {
				delete [] pBuffer;
				if (error != EAGAIN) {
					controlSocket_.LogMessage(MessageType::Error, L"Could not read from transfer socket: %s", CSocket::GetErrorDescription(error));
					TransferEnd(TransferEndReason::transfer_failure);
				}
				else if (m_onCloseCalled && !m_pBackend->IsWaiting(CRateLimiter::inbound)) {
					TransferEnd(TransferEndReason::successful);
				}
				return;
			}

			if (numread > 0) {
				if (!m_pDirectoryListingParser->AddData(pBuffer, numread)) {
					TransferEnd(TransferEndReason::transfer_failure);
					return;
				}

				controlSocket_.SetActive(CFileZillaEngine::recv);
				if (!m_madeProgress) {
					m_madeProgress = 2;
					engine_.transfer_status_.SetMadeProgress();
				}
				engine_.transfer_status_.Update(numread);
			}
			else {
				delete [] pBuffer;
				TransferEnd(TransferEndReason::successful);
				return;
			}
		}
	}
	else if (m_transferMode == TransferMode::download) {
		int error;
		int numread;

		// Only do a certain number of iterations in one go to keep the event loop going.
		// Otherwise this behaves like a livelock on very large files written to a very fast
		// SSD downloaded from a very fast server.
		for (int i = 0; i < 100; ++i) {
			if (!CheckGetNextWriteBuffer()) {
				return;
			}

			numread = m_pBackend->Read(m_pTransferBuffer, m_transferBufferLen, error);
			if (numread <= 0) {
				break;
			}

			controlSocket_.SetActive(CFileZillaEngine::recv);
			if (!m_madeProgress) {
				m_madeProgress = 2;
				engine_.transfer_status_.SetMadeProgress();
			}
			engine_.transfer_status_.Update(numread);

			m_pTransferBuffer += numread;
			m_transferBufferLen -= numread;
		}

		if (numread < 0) {
			if (error != EAGAIN) {
				controlSocket_.LogMessage(MessageType::Error, L"Could not read from transfer socket: %s", CSocket::GetErrorDescription(error));
				TransferEnd(TransferEndReason::transfer_failure);
			}
			else if (m_onCloseCalled && !m_pBackend->IsWaiting(CRateLimiter::inbound)) {
				FinalizeWrite();
			}
		}
		else if (!numread) {
			FinalizeWrite();
		}
		else {
			send_event<CSocketEvent>(m_pBackend, SocketEventType::read, 0);
		}
	}
	else if (m_transferMode == TransferMode::resumetest) {
		for (;;) {
			char buffer[2];
			int error;
			int numread = m_pBackend->Read(buffer, 2, error);
			if (numread < 0) {
				if (error != EAGAIN) {
					controlSocket_.LogMessage(MessageType::Error, L"Could not read from transfer socket: %s", CSocket::GetErrorDescription(error));
					TransferEnd(TransferEndReason::transfer_failure);
				}
				else if (m_onCloseCalled && !m_pBackend->IsWaiting(CRateLimiter::inbound)) {
					if (m_transferBufferLen == 1) {
						TransferEnd(TransferEndReason::successful);
					}
					else {
						controlSocket_.LogMessage(MessageType::Debug_Warning, L"Server incorrectly sent %d bytes", m_transferBufferLen);
						TransferEnd(TransferEndReason::failed_resumetest);
					}
				}
				return;
			}

			if (!numread) {
				if (m_transferBufferLen == 1) {
					TransferEnd(TransferEndReason::successful);
				}
				else {
					controlSocket_.LogMessage(MessageType::Debug_Warning, L"Server incorrectly sent %d bytes", m_transferBufferLen);
					TransferEnd(TransferEndReason::failed_resumetest);
				}
				return;
			}
			m_transferBufferLen += numread;

			if (m_transferBufferLen > 1) {
				controlSocket_.LogMessage(MessageType::Debug_Warning, L"Server incorrectly sent %d bytes", m_transferBufferLen);
				TransferEnd(TransferEndReason::failed_resumetest);
				return;
			}
		}
	}
}

void CTransferSocket::OnSend()
{
	if (!m_pBackend) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"OnSend called without backend. Ignoring event.");
		return;
	}

	if (!m_bActive) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"Postponing send");
		m_postponedSend = true;
		return;
	}

	if (m_transferMode != TransferMode::upload) {
		return;
	}

	int error;
	int written;

	// Only do a certain number of iterations in one go to keep the event loop going.
	// Otherwise this behaves like a livelock on very large files read from a very fast
	// SSD uploaded to a very fast server.
	for (int i = 0; i < 100; ++i) {
		if (!CheckGetNextReadBuffer()) {
			return;
		}

		written = m_pBackend->Write(m_pTransferBuffer, m_transferBufferLen, error);
		if (written <= 0) {
			break;
		}

		controlSocket_.SetActive(CFileZillaEngine::send);
		if (m_madeProgress == 1) {
			controlSocket_.LogMessage(MessageType::Debug_Debug, L"Made progress in CTransferSocket::OnSend()");
			m_madeProgress = 2;
			engine_.transfer_status_.SetMadeProgress();
		}
		engine_.transfer_status_.Update(written);

		m_pTransferBuffer += written;
		m_transferBufferLen -= written;
	}

	if (written < 0) {
		if (error == EAGAIN) {
			if (!m_madeProgress) {
				controlSocket_.LogMessage(MessageType::Debug_Debug, L"First EAGAIN in CTransferSocket::OnSend()");
				m_madeProgress = 1;
				engine_.transfer_status_.SetMadeProgress();
			}
		}
		else {
			controlSocket_.LogMessage(MessageType::Error, L"Could not write to transfer socket: %s", CSocket::GetErrorDescription(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
	}
	else if (written > 0) {
		send_event<CSocketEvent>(m_pBackend, SocketEventType::write, 0);
	}
}

void CTransferSocket::OnClose(int error)
{
	controlSocket_.LogMessage(MessageType::Debug_Verbose, L"CTransferSocket::OnClose(%d)", error);
	m_onCloseCalled = true;

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (!m_pBackend) {
		if (!InitBackend()) {
			TransferEnd(TransferEndReason::transfer_failure);
			return;
		}
	}

	if (m_transferMode == TransferMode::upload) {
		if (m_shutdown && m_pTlsSocket) {
			if (m_pTlsSocket->Shutdown() != 0) {
				TransferEnd(TransferEndReason::transfer_failure);
			}
			else {
				TransferEnd(TransferEndReason::successful);
			}
		}
		else {
			TransferEnd(TransferEndReason::transfer_failure);
		}
		return;
	}

	if (error) {
		controlSocket_.LogMessage(MessageType::Error, _("Transfer connection interrupted: %s"), CSocket::GetErrorDescription(error));
		TransferEnd(TransferEndReason::transfer_failure);
		return;
	}

	char buffer[100];
	int numread = m_pBackend->Peek(&buffer, 100, error);
	if (numread > 0) {
#ifndef FZ_WINDOWS
		controlSocket_.LogMessage(MessageType::Debug_Warning, L"Peek isn't supposed to return data after close notification");
#endif

		// MSDN says this:
		//   FD_CLOSE being posted after all data is read from a socket.
		//   An application should check for remaining data upon receipt
		//   of FD_CLOSE to avoid any possibility of losing data.
		// First half is actually plain wrong.
		OnReceive();

		return;
	}
	else if (numread < 0 && error != EAGAIN) {
		controlSocket_.LogMessage(MessageType::Error, _("Transfer connection interrupted: %s"), CSocket::GetErrorDescription(error));
		TransferEnd(TransferEndReason::transfer_failure);
		return;
	}

	if (m_transferMode == TransferMode::resumetest) {
		if (m_transferBufferLen != 1) {
			TransferEnd(TransferEndReason::failed_resumetest);
			return;
		}
	}
	if (m_transferMode == TransferMode::download) {
		FinalizeWrite();
	}
	else {
		TransferEnd(TransferEndReason::successful);
	}
}

bool CTransferSocket::SetupPassiveTransfer(std::wstring const& host, int port)
{
	std::string ip;

	ResetSocket();

	socket_ = std::make_unique<CSocket>(engine_.GetThreadPool(), this);

	if (controlSocket_.m_pProxyBackend) {
		m_pProxyBackend = new CProxySocket(this, socket_.get(), &controlSocket_);

		int res = m_pProxyBackend->Handshake(controlSocket_.m_pProxyBackend->GetProxyType(),
											 host, port,
											 controlSocket_.m_pProxyBackend->GetUser(), controlSocket_.m_pProxyBackend->GetPass());

		if (res != EINPROGRESS) {
			ResetSocket();
			return false;
		}
		int error;
		ip = controlSocket_.m_pSocket->GetPeerIP();
		port = controlSocket_.m_pSocket->GetRemotePort(error);
		if (ip.empty() || port < 1) {
			controlSocket_.LogMessage(MessageType::Debug_Warning, L"Could not get peer address of control connection.");
			ResetSocket();
			return false;
		}
	}
	else {
		ip = fz::to_utf8(host);
	}

	SetSocketBufferSizes(*socket_);

	// Try to bind the source IP of the data connection to the same IP as the control connection.
	// We can do so either if
	// 1) the destination IP of the data connection matches peer IP of the control connection or
	// 2) we are using a proxy.
	//
	// In case destination IPs of control and data connection are different, do not bind to the
	// same source.

	std::string bindAddress;
	if (m_pProxyBackend) {
		bindAddress = controlSocket_.m_pSocket->GetLocalIP();
		controlSocket_.LogMessage(MessageType::Debug_Info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
	}
	else {
		if (controlSocket_.m_pSocket->GetPeerIP(true) == ip || controlSocket_.m_pSocket->GetPeerIP(false) == ip) {
			bindAddress = controlSocket_.m_pSocket->GetLocalIP();
			controlSocket_.LogMessage(MessageType::Debug_Info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
		}
		else {
			controlSocket_.LogMessage(MessageType::Debug_Warning, L"Destination IP of data connection does not match peer IP of control connection. Not binding source address of data connection.");
		}
	}

	int res = socket_->Connect(fz::to_native(ip), port, CSocket::unspec, bindAddress);
	if (res && res != EINPROGRESS) {
		ResetSocket();
		return false;
	}

	return true;
}

void CTransferSocket::SetActive()
{
	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}
	if (m_transferMode == TransferMode::download || m_transferMode == TransferMode::upload) {
		if (ioThread_) {
			ioThread_->SetEventHandler(this);
		}
	}

	m_bActive = true;
	if (!socket_) {
		return;
	}

	if (socket_->GetState() == CSocket::connected || socket_->GetState() == CSocket::closing) {
		TriggerPostponedEvents();
	}
}

void CTransferSocket::TransferEnd(TransferEndReason reason)
{
	controlSocket_.LogMessage(MessageType::Debug_Verbose, L"CTransferSocket::TransferEnd(%d)", reason);

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}
	m_transferEndReason = reason;

	ResetSocket();

	controlSocket_.send_event<TransferEndEvent>();
}

std::unique_ptr<CSocket> CTransferSocket::CreateSocketServer(int port)
{
	auto socket = std::make_unique<CSocket>(engine_.GetThreadPool(), this);
	int res = socket->Listen(controlSocket_.m_pSocket->GetAddressFamily(), port);
	if (res) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"Could not listen on port %d: %s", port, CSocket::GetErrorDescription(res));
		socket.reset();
	}
	else {
		SetSocketBufferSizes(*socket);
	}

	return socket;
}

std::unique_ptr<CSocket> CTransferSocket::CreateSocketServer()
{
	if (!engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS)) {
		// Ask the systen for a port
		return CreateSocketServer(0);
	}

	// Try out all ports in the port range.
	// Upon first call, we try to use a random port fist, after that
	// increase the port step by step

	// Windows only: I think there's a bug in the socket implementation of
	// Windows: Even if using SO_REUSEADDR, using the same local address
	// twice will fail unless there are a couple of minutes between the
	// connection attempts. This may cause problems if transferring lots of
	// files with a narrow port range.

	static int start = 0;

	int low = engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS_LOW);
	int high = engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS_HIGH);
	if (low > high) {
		low = high;
	}

	if (start < low || start > high) {
		start = fz::random_number(low, high);
		assert(start >= low && start <= high);
	}

	std::unique_ptr<CSocket> server;

	int count = high - low + 1;
	while (count--) {
		server = CreateSocketServer(start++);
		if (server) {
			break;
		}
		if (start > high) {
			start = low;
		}
	}

	return server;
}

bool CTransferSocket::CheckGetNextWriteBuffer()
{
	if (!m_transferBufferLen) {
		int res = ioThread_->GetNextWriteBuffer(&m_pTransferBuffer);

		if (res == IO_Again) {
			return false;
		}
		else if (res == IO_Error) {
			std::wstring error = ioThread_->GetError();
			if (error.empty()) {
				controlSocket_.LogMessage(MessageType::Error, _("Can't write data to file."));
			}
			else {
				controlSocket_.LogMessage(MessageType::Error, _("Can't write data to file: %s"), error);
			}
			TransferEnd(TransferEndReason::transfer_failure_critical);
			return false;
		}

		m_transferBufferLen = BUFFERSIZE;
	}

	return true;
}

bool CTransferSocket::CheckGetNextReadBuffer()
{
	if (!m_transferBufferLen) {
		int res = ioThread_->GetNextReadBuffer(&m_pTransferBuffer);
		if (res == IO_Again) {
			return false;
		}
		else if (res == IO_Error) {
			controlSocket_.LogMessage(MessageType::Error, _("Can't read from file"));
			TransferEnd(TransferEndReason::transfer_failure);
			return false;
		}
		else if (res == IO_Success) {
			if (m_pTlsSocket) {
				m_shutdown = true;

				int error = m_pTlsSocket->Shutdown();
				if (error != 0) {
					if (error != EAGAIN) {
						TransferEnd(TransferEndReason::transfer_failure);
					}
					return false;
				}
			}
			TransferEnd(TransferEndReason::successful);
			return false;
		}
		m_transferBufferLen = res;
	}

	return true;
}

void CTransferSocket::OnIOThreadEvent()
{
	if (!m_bActive || m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (m_transferMode == TransferMode::download) {
		OnReceive();
	}
	else if (m_transferMode == TransferMode::upload) {
		OnSend();
	}
}

void CTransferSocket::FinalizeWrite()
{
	bool res = ioThread_->Finalize(BUFFERSIZE - m_transferBufferLen);
	m_transferBufferLen = BUFFERSIZE;

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (res) {
		TransferEnd(TransferEndReason::successful);
	}
	else {
		std::wstring error = ioThread_->GetError();
		if (error.empty()) {
			controlSocket_.LogMessage(MessageType::Error, _("Can't write data to file."));
		}
		else {
			controlSocket_.LogMessage(MessageType::Error, _("Can't write data to file: %s"), error);
		}
		TransferEnd(TransferEndReason::transfer_failure_critical);
	}
}

bool CTransferSocket::InitTls(const CTlsSocket* pPrimaryTlsSocket)
{
	// Disable Nagle algorithm during TlS handshake
	socket_->SetFlags(socket_->GetFlags() | CSocket::flag_nodelay);

	assert(!m_pBackend);
	m_pTlsSocket = new CTlsSocket(this, *socket_, &controlSocket_);

	if (!m_pTlsSocket->Init()) {
		delete m_pTlsSocket;
		m_pTlsSocket = 0;
		return false;
	}

	bool try_resume = CServerCapabilities::GetCapability(controlSocket_.currentServer_, tls_resume) != no;

	int res = m_pTlsSocket->Handshake(pPrimaryTlsSocket, try_resume);
	if (res && res != FZ_REPLY_WOULDBLOCK) {
		delete m_pTlsSocket;
		m_pTlsSocket = 0;
		return false;
	}

	m_pBackend = m_pTlsSocket;

	return true;
}

void CTransferSocket::TriggerPostponedEvents()
{
	assert(m_bActive);

	if (m_postponedReceive) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"Executing postponed receive");
		m_postponedReceive = false;
		OnReceive();
		if (m_transferEndReason != TransferEndReason::none) {
			return;
		}
	}
	if (m_postponedSend) {
		controlSocket_.LogMessage(MessageType::Debug_Verbose, L"Executing postponed send");
		m_postponedSend = false;
		OnSend();
		if (m_transferEndReason != TransferEndReason::none) {
			return;
		}
	}
	if (m_onCloseCalled) {
		OnClose(0);
	}
}

bool CTransferSocket::InitBackend()
{
	if (m_pBackend) {
		return true;
	}

#ifdef FZ_WINDOWS
	// For send buffer tuning
	add_timer(fz::duration::from_seconds(1), false);
#endif

	if (controlSocket_.m_protectDataChannel) {
		if (!InitTls(controlSocket_.m_pTlsSocket)) {
			return false;
		}
	}
	else {
		m_pBackend = new CSocketBackend(this, *socket_, engine_.GetRateLimiter());
	}

	return true;
}

void CTransferSocket::SetSocketBufferSizes(CSocket& socket)
{
	const int size_read = engine_.GetOptions().GetOptionVal(OPTION_SOCKET_BUFFERSIZE_RECV);
#if FZ_WINDOWS
	const int size_write = -1;
#else
	const int size_write = engine_.GetOptions().GetOptionVal(OPTION_SOCKET_BUFFERSIZE_SEND);
#endif
	socket.SetBufferSizes(size_read, size_write);
}

void CTransferSocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<CSocketEvent, CIOThreadEvent, fz::timer_event>(ev, this,
		&CTransferSocket::OnSocketEvent,
		&CTransferSocket::OnIOThreadEvent,
		&CTransferSocket::OnTimer);
}

void CTransferSocket::OnTimer(fz::timer_id)
{
	if (socket_ && socket_->GetState() == CSocket::connected) {
		int const ideal_send_buffer = socket_->GetIdealSendBufferSize();
		if (ideal_send_buffer != -1) {
			socket_->SetBufferSizes(-1, ideal_send_buffer);
		}
	}
}
