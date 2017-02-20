#include <libfilezilla/libfilezilla.hpp>
#ifdef FZ_WINDOWS
  #include <libfilezilla/private/windows.hpp>
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netdb.h>
#endif
#include <filezilla.h>
#include "engineprivate.h"
#include "proxy.h"
#include "ControlSocket.h"

#include <libfilezilla/iputils.hpp>

#include <algorithm>

#include <string.h>

enum handshake_state
{
	http_wait,

	socks5_method,
	socks5_auth,
	socks5_request,
	socks5_request_addrtype,
	socks5_request_address,

	socks4_handshake
};

CProxySocket::CProxySocket(fz::event_handler* pEvtHandler, CSocket* pSocket, CControlSocket* pOwner)
	: fz::event_handler(pOwner->event_loop_)
	, CBackend(pEvtHandler)
	, m_pSocket(pSocket)
	, m_pOwner(pOwner)
{
	m_pSocket->SetEventHandler(this);
}

CProxySocket::~CProxySocket()
{
	remove_handler();

	if (m_pSocket)
		m_pSocket->SetEventHandler(0);
	delete [] m_pSendBuffer;
	delete [] m_pRecvBuffer;
}

std::wstring CProxySocket::Name(ProxyType t)
{
	switch (t) {
	case HTTP:
		return L"HTTP";
	case SOCKS4:
		return L"SOCKS4";
	case SOCKS5:
		return L"SOCKS5";
	default:
		return _("unknown");
	}
}

int CProxySocket::Handshake(CProxySocket::ProxyType type, std::wstring const& host, unsigned int port, std::wstring const& user, std::wstring const& pass)
{
	if (type == CProxySocket::unknown || host.empty() || port < 1 || port > 65535) {
		return EINVAL;
	}

	if (m_proxyState != noconn) {
		return EALREADY;
	}

	if (type != HTTP && type != SOCKS5 && type != SOCKS4) {
		return EPROTONOSUPPORT;
	}

	m_user = fz::to_utf8(user);
	m_pass = fz::to_utf8(pass);
	m_host = host;
	m_port = port;
	m_proxyType = type;

	m_proxyState = handshake;

	if (type == HTTP) {
		m_handshakeState = http_wait;

		std::string auth;
		if (!user.empty()) {
			auth = "Proxy-Authorization: Basic ";
			auth += fz::base64_encode(m_user + ":" + m_pass);
			auth += "\r\n";
		}

		// Bit oversized, but be on the safe side
		std::string host_raw = fz::to_utf8(host);
		m_pSendBuffer = new char[70 + host_raw.size() * 2 + 2*5 + auth.size() + 23];

		m_sendBufferLen = sprintf(m_pSendBuffer, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n%sUser-Agent: %s\r\n\r\n",
			host_raw.c_str(), port,
			host_raw.c_str(), port,
			auth.c_str(),
			fz::replaced_substrings(PACKAGE_STRING, " ", "/").c_str());

		m_pRecvBuffer = new char[4096];
		m_recvBufferLen = 4096;
		m_recvBufferPos = 0;
	}
	else if (type == SOCKS4) {
		std::string ip;
		auto const addressType = fz::get_address_type(m_host);
		if (addressType == fz::address_type::ipv6) {
			m_pOwner->LogMessage(MessageType::Error, _("IPv6 addresses are not supported with SOCKS4 proxy"));
			return EINVAL;
		}
		else if (addressType == fz::address_type::ipv4) {
			ip = fz::to_string(m_host);
		}
		else {
			addrinfo hints{};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;

			addrinfo * result{};
			int res = getaddrinfo(fz::to_string(m_host).c_str(), 0, &hints, &result);
			if (!res && result) {
				if (result->ai_family == AF_INET) {
					ip = CSocket::AddressToString(result->ai_addr, result->ai_addrlen, false);
				}
				freeaddrinfo(result);
			}

			if (ip.empty()) {
				m_pOwner->LogMessage(MessageType::Error, _("Cannot resolve hostname to IPv4 address for use with SOCKS4 proxy."));
				return EINVAL;
			}
		}

		m_pOwner->LogMessage(MessageType::Status, _("SOCKS4 proxy will connect to: %s"), ip);

		m_pSendBuffer = new char[9];
		m_pSendBuffer[0] = 4; // Protocol version
		m_pSendBuffer[1] = 1; // Stream mode
		m_pSendBuffer[2] = (m_port >> 8) & 0xFF; // Port in network order
		m_pSendBuffer[3] = m_port & 0xFF;
		unsigned char *buf = (unsigned char*)m_pSendBuffer + 4;
		int i = 0;
		memset(buf, 0, 4);
		for (auto p = ip.c_str(); *p && i < 4; ++p) {
			auto const& c = *p;
			if (c == '.') {
				++i;
				continue;
			}
			buf[i] *= 10;
			buf[i] += c - '0';
		}
		m_pSendBuffer[8] = 0;
		m_sendBufferLen = 9;
		m_pRecvBuffer = new char[8];
		m_recvBufferLen = 8;
		m_recvBufferPos = 0;
		m_handshakeState = socks4_handshake;
	}
	else {
		if (m_user.size() > 255 || m_pass.size() > 255) {
			m_pOwner->LogMessage(MessageType::Status, _("SOCKS5 does not support usernames or passwords longer than 255 characters."));
			return EINVAL;
		}

		m_pSendBuffer = new char[4];
		m_pSendBuffer[0] = 5; // Protocol version
		if (!user.empty()) {
			m_pSendBuffer[1] = 2; // # auth methods supported
			m_pSendBuffer[2] = 0; // Method: No auth
			m_pSendBuffer[3] = 2; // Method: Username and password
			m_sendBufferLen = 4;
		}
		else {
			m_pSendBuffer[1] = 1; // # auth methods supported
			m_pSendBuffer[2] = 0; // Method: No auth
			m_sendBufferLen = 3;
		}

		m_pRecvBuffer = new char[1024];
		m_recvBufferLen = 2;
		m_recvBufferPos = 0;

		m_handshakeState = socks5_method;
	}

	return EINPROGRESS;
}

void CProxySocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<CSocketEvent, CHostAddressEvent>(ev, this,
		&CProxySocket::OnSocketEvent,
		&CProxySocket::OnHostAddress);
}

void CProxySocket::OnSocketEvent(CSocketEventSource*, SocketEventType t, int error)
{
	switch (t) {
	case SocketEventType::connection_next:
		if (error)
			m_pOwner->LogMessage(MessageType::Status, _("Connection attempt failed with \"%s\", trying next address."), CSocket::GetErrorDescription(error));
		break;
	case SocketEventType::connection:
		if (error) {
			if (m_proxyState == handshake)
				m_proxyState = noconn;
			m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::connection, error);
		}
		else {
			m_pOwner->LogMessage(MessageType::Status, _("Connection with proxy established, performing handshake..."));
		}
		break;
	case SocketEventType::read:
		OnReceive();
		break;
	case SocketEventType::write:
		OnSend();
		break;
	case SocketEventType::close:
		OnReceive();
		break;
	}
}

void CProxySocket::OnHostAddress(CSocketEventSource*, std::string const& address)
{
	m_pOwner->LogMessage(MessageType::Status, _("Connecting to %s..."), address);
}

void CProxySocket::Detach()
{
	if (!m_pSocket)
		return;

	m_pSocket->SetEventHandler(0);
	m_pSocket = 0;
}

void CProxySocket::OnReceive()
{
	m_can_read = true;

	if (m_proxyState != handshake)
		return;

	switch (m_handshakeState)
	{
	case http_wait:
		for (;;) {
			int error;
			int do_read = m_recvBufferLen - m_recvBufferPos - 1;
			char* end = 0;
			for (int i = 0; i < 2; ++i) {
				int read;
				if (!i)
					read = m_pSocket->Peek(m_pRecvBuffer + m_recvBufferPos, do_read, error);
				else
					read = m_pSocket->Read(m_pRecvBuffer + m_recvBufferPos, do_read, error);
				if (read == -1) {
					if (error != EAGAIN) {
						m_proxyState = noconn;
						m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, error);
					}
					else
						m_can_read = false;
					return;
				}
				if (!read) {
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
					return;
				}
				if (m_pSendBuffer) {
					m_proxyState = noconn;
					m_pOwner->LogMessage(MessageType::Debug_Warning, L"Incoming data before request fully sent");
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
					return;
				}

				if (!i) {
					// Response ends with strstr
					m_pRecvBuffer[m_recvBufferPos + read] = 0;
					end = strstr(m_pRecvBuffer, "\r\n\r\n");
					if (!end) {
						if (m_recvBufferPos + read + 1 == m_recvBufferLen) {
							m_proxyState = noconn;
							m_pOwner->LogMessage(MessageType::Debug_Warning, L"Incoming header too large");
							m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ENOMEM);
							return;
						}
						do_read = read;
					}
					else
						do_read = end - m_pRecvBuffer + 4 - m_recvBufferPos;
				}
				else {
					if (read != do_read) {
						m_proxyState = noconn;
						m_pOwner->LogMessage(MessageType::Debug_Warning, "Could not read what got peeked");
						m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
						return;
					}
					m_recvBufferPos += read;
				}
			}

			if (!end)
				continue;

			end = strchr(m_pRecvBuffer, '\r'); // Never fails as old value of end exists and starts with CR, we just look for an earlier case.
			*end = 0;
			std::wstring const reply = fz::to_wstring_from_utf8(m_pRecvBuffer);
			m_pOwner->LogMessage(MessageType::Response, _("Proxy reply: %s"), reply);

			if (reply.substr(0, 10) != L"HTTP/1.1 2" && reply.substr(0, 10) != L"HTTP/1.0 2") {
				m_proxyState = noconn;
				m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNRESET);
				return;
			}

			m_proxyState = conn;
			m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::connection, 0);
			return;
		}
	case socks4_handshake:
		while (m_recvBufferLen && m_can_read && m_proxyState == handshake) {
			int read_error;
			int read = m_pSocket->Read(m_pRecvBuffer + m_recvBufferPos, m_recvBufferLen, read_error);
			if (read == -1) {
				if (read_error != EAGAIN) {
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, read_error);
				}
				else
					m_can_read = false;
				return;
			}

			if (!read) {
				m_proxyState = noconn;
				m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
				return;
			}
			m_recvBufferPos += read;
			m_recvBufferLen -= read;

			if (m_recvBufferLen)
				continue;

			m_recvBufferPos = 0;

			if (m_pRecvBuffer[1] != 0x5A) {
				std::wstring error;
				switch (m_pRecvBuffer[1]) {
					case 0x5B:
						error = _("Request rejected or failed");
						break;
					case 0x5C:
						error = _("Request failed - client is not running identd (or not reachable from server)");
						break;
					case 0x5D:
						error = _("Request failed - client's identd could not confirm the user ID string");
						break;
					default:
						error = fz::sprintf(_("Unassigned error code %d"), (int)(unsigned char)m_pRecvBuffer[1]);
						break;
				}
				m_pOwner->LogMessage(MessageType::Error, _("Proxy request failed: %s"), error);
				m_proxyState = noconn;
				m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
				return;
			}
			m_proxyState = conn;
			m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::connection, 0);
		}
		return;
	case socks5_method:
	case socks5_auth:
	case socks5_request:
	case socks5_request_addrtype:
	case socks5_request_address:
		if (m_pSendBuffer)
			return;
		while (m_recvBufferLen && m_can_read && m_proxyState == handshake) {
			int error;
			int read = m_pSocket->Read(m_pRecvBuffer + m_recvBufferPos, m_recvBufferLen, error);
			if (read == -1) {
				if (error != EAGAIN) {
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, error);
				}
				else
					m_can_read = false;
				return;
			}
			if (!read) {
				m_proxyState = noconn;
				m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
				return;
			}
			m_recvBufferPos += read;
			m_recvBufferLen -= read;

			if (m_recvBufferLen)
				continue;

			m_recvBufferPos = 0;

			// All data got read, parse it
			switch (m_handshakeState) {
			default:
				if (m_pRecvBuffer[0] != 5) {
					m_pOwner->LogMessage(MessageType::Error, _("Unknown SOCKS protocol version: %d"), (int)m_pRecvBuffer[0]);
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
					return;
				}
				break;
			case socks5_auth:
				if (m_pRecvBuffer[0] != 1) {
					m_pOwner->LogMessage(MessageType::Error, _("Unknown protocol version of SOCKS Username/Password Authentication subnegotiation: %d"), (int)m_pRecvBuffer[0]);
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
					return;
				}
				break;
			case socks5_request_address:
			case socks5_request_addrtype:
				// Nothing to do
				break;
			}
			switch (m_handshakeState) {
			case socks5_method:
				{
					const char method = m_pRecvBuffer[1];
					switch (method)
					{
					case 0:
						m_handshakeState = socks5_request;
						break;
					case 2:
						m_handshakeState = socks5_auth;
						break;
					default:
						m_pOwner->LogMessage(MessageType::Error, _("No supported SOCKS5 auth method"));
						m_proxyState = noconn;
						m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
						return;
					}
				}
				break;
			case socks5_auth:
				if (m_pRecvBuffer[1] != 0) {
					m_pOwner->LogMessage(MessageType::Error, _("Proxy authentication failed"));
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
					return;
				}
				m_handshakeState = socks5_request;
				break;
			case socks5_request:
				if (m_pRecvBuffer[1]) {
					std::wstring errorMsg;
					switch (m_pRecvBuffer[1])
					{
					case 1:
						errorMsg = _("General SOCKS server failure");
						break;
					case 2:
						errorMsg = _("Connection not allowed by ruleset");
						break;
					case 3:
						errorMsg = _("Network unreachable");
						break;
					case 4:
						errorMsg = _("Host unreachable");
						break;
					case 5:
						errorMsg = _("Connection refused");
						break;
					case 6:
						errorMsg = _("TTL expired");
						break;
					case 7:
						errorMsg = _("Command not supported");
						break;
					case 8:
						errorMsg = _("Address type not supported");
						break;
					default:
						errorMsg = fz::sprintf(_("Unassigned error code %d"), (int)(unsigned char)m_pRecvBuffer[1]);
						break;
					}

					m_pOwner->LogMessage(MessageType::Error, _("Proxy request failed. Reply from proxy: %s"), errorMsg);
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
					return;
				}
				m_handshakeState = socks5_request_addrtype;
				m_recvBufferLen = 3;
				break;
			case socks5_request_addrtype:
				// We need to parse the returned address type to determine the length of the address that follows.
				// Unfortunately the information in the type and address is useless, many proxies just return
				// syntactically valid bogus values
				switch (m_pRecvBuffer[1])
				{
				case 1:
					m_recvBufferLen = 5;
					break;
				case 3:
					m_recvBufferLen = m_pRecvBuffer[2] + 2;
					break;
				case 4:
					m_recvBufferLen = 17;
					break;
				default:
					m_pOwner->LogMessage(MessageType::Error, _("Proxy request failed: Unknown address type in CONNECT reply"));
					m_proxyState = noconn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
					return;
				}
				m_handshakeState = socks5_request_address;
				break;
			case socks5_request_address:
				{
					// We're done
					m_proxyState = conn;
					m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::connection, 0);
					return;
				}
			default:
				assert(false);
				break;
			}

			switch (m_handshakeState)
			{
			case socks5_auth:
				{
					auto ulen = static_cast<unsigned char>(std::min(m_user.size(), size_t(255)));
					auto plen = static_cast<unsigned char>(std::min(m_pass.size(), size_t(255)));
					m_sendBufferLen = ulen + plen + 3;
					m_pSendBuffer = new char[m_sendBufferLen];
					m_pSendBuffer[0] = 1;
					m_pSendBuffer[1] = ulen;
					memcpy(m_pSendBuffer + 2, m_user.c_str(), ulen);
					m_pSendBuffer[ulen + 2] = plen;
					memcpy(m_pSendBuffer + ulen + 3, m_pass.c_str(), plen);
					m_recvBufferLen = 2;
				}
				break;
			case socks5_request:
				{
					std::string host = fz::to_utf8(m_host);
					size_t addrlen = std::max(host.size(), size_t(16));

					m_pSendBuffer = new char[7 + addrlen];
					m_pSendBuffer[0] = 5;
					m_pSendBuffer[1] = 1; // CONNECT
					m_pSendBuffer[2] = 0; // Reserved

					auto const type = fz::get_address_type(host);
					if (type == fz::address_type::ipv6) {
						auto ipv6 = fz::get_ipv6_long_form(host);
						addrlen = 16;
						for (auto i = 0; i < 16; ++i) {
							m_pSendBuffer[4 + i] = (fz::hex_char_to_int(ipv6[i * 2 + i / 2]) << 4) + fz::hex_char_to_int(ipv6[i * 2 + 1 + i / 2]);
						}

						m_pSendBuffer[3] = 4; // IPv6
					}
					else if (type == fz::address_type::ipv4) {
						unsigned char *buf = (unsigned char*)m_pSendBuffer + 4;
						int i = 0;
						memset(buf, 0, 4);
						for (auto p = host.c_str(); *p && i < 4; ++p) {
							auto const& c = *p;
							if (c == '.') {
								++i;
								continue;
							}
							buf[i] *= 10;
							buf[i] += c - '0';
						}

						addrlen = 4;

						m_pSendBuffer[3] = 1; // IPv4
					}
					else {
						m_pSendBuffer[3] = 3; // Domain name

						auto hlen = static_cast<unsigned char>(std::min(host.size(), size_t(255)));
						m_pSendBuffer[4] = hlen;
						memcpy(m_pSendBuffer + 5, host.c_str(), hlen);
						addrlen = hlen + 1;
					}

					m_pSendBuffer[addrlen + 4] = (m_port >> 8) & 0xFF; // Port in network order
					m_pSendBuffer[addrlen + 5] = m_port & 0xFF;

					m_sendBufferLen = 6 + addrlen;
					m_recvBufferLen = 2;
				}
				break;
			case socks5_request_addrtype:
			case socks5_request_address:
				// Nothing to send, we simply need to wait for more data
				break;
			default:
				assert(false);
				break;
			}
			if (m_pSendBuffer && m_can_write)
				OnSend();
		}
		break;
	default:
		m_proxyState = noconn;
		m_pOwner->LogMessage(MessageType::Debug_Warning, L"Unhandled handshake state %d", m_handshakeState);
		m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, ECONNABORTED);
		return;
	}
}

void CProxySocket::OnSend()
{
	m_can_write = true;
	if (m_proxyState != handshake || !m_pSendBuffer)
		return;

	for (;;) {
		int error;
		int written = m_pSocket->Write(m_pSendBuffer, m_sendBufferLen, error);
		if (written == -1) {
			if (error != EAGAIN) {
				m_proxyState = noconn;
				m_pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::close, error);
			}
			else
				m_can_write = false;

			return;
		}

		if (written == m_sendBufferLen) {
			delete [] m_pSendBuffer;
			m_pSendBuffer = 0;

			if (m_can_read)
				OnReceive();
			return;
		}
		memmove(m_pSendBuffer, m_pSendBuffer + written, m_sendBufferLen - written);
		m_sendBufferLen -= written;
	}
}

int CProxySocket::Read(void *, unsigned int, int& error)
{
	error = EAGAIN;
	return -1;
}

int CProxySocket::Peek(void *, unsigned int, int& error)
{
	error = EAGAIN;
	return -1;
}

int CProxySocket::Write(const void *, unsigned int, int& error)
{
	error = EAGAIN;
	return -1;
}

std::wstring CProxySocket::GetUser() const
{
	return fz::to_wstring_from_utf8(m_user);
}

std::wstring CProxySocket::GetPass() const
{
	return fz::to_wstring_from_utf8(m_pass);
}
