#ifndef FILEZILLA_ENGINE_PROXY_HEADER
#define FILEZILLA_ENGINE_PROXY_HEADER

#include "backend.h"
#include "socket.h"

#include <libfilezilla/buffer.hpp>

class CControlSocket;
class CProxySocket final : protected fz::event_handler, public CBackend
{
public:
	CProxySocket(event_handler* pEvtHandler, fz::socket* pSocket, CControlSocket* pOwner);
	virtual ~CProxySocket();

	enum ProxyState {
		noconn,
		handshake,
		conn
	};

	enum ProxyType {
		unknown,
		HTTP,
		SOCKS5,
		SOCKS4,

		proxytype_count
	};
	static std::wstring Name(ProxyType t);

	int Handshake(ProxyType type, std::wstring const& host, unsigned int port, std::wstring const& user, std::wstring const& pass);

	ProxyState GetState() const { return m_proxyState; }

	// Past the initial handshake, proxies are transparent.
	// Class users should detach socket and use a normal socket backend instead.
	virtual void OnRateAvailable(CRateLimiter::rate_direction) override {};
	virtual int Read(void *buffer, unsigned int size, int& error) override;
	virtual int Peek(void *buffer, unsigned int size, int& error) override;
	virtual int Write(const void *buffer, unsigned int size, int& error) override;

	void Detach();
	bool Detached() const { return socket_ == nullptr; }

	ProxyType GetProxyType() const { return m_proxyType; }
	std::wstring GetUser() const;
	std::wstring GetPass() const;

protected:
	fz::socket* socket_;
	CControlSocket* m_pOwner;

	ProxyType m_proxyType{unknown};
	std::wstring m_host;
	int m_port{};
	std::string m_user;
	std::string m_pass;

	ProxyState m_proxyState{noconn};

	int m_handshakeState{};

	fz::buffer sendBuffer_;

	char* m_pRecvBuffer{};
	int m_recvBufferPos{};
	int m_recvBufferLen{};

	virtual void operator()(fz::event_base const& ev) override;
	void OnSocketEvent(socket_event_source* source, fz::socket_event_flag t, int error);
	void OnHostAddress(socket_event_source* source, std::string const& address);

	void OnReceive();
	void OnSend();

	bool m_can_write{};
	bool m_can_read{};
};

#endif
