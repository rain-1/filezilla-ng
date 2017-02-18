#ifndef FILEZILLA_ENGINE_SOCKET_HEADER
#define FILEZILLA_ENGINE_SOCKET_HEADER

#include <libfilezilla/event_handler.hpp>

#include <errno.h>

namespace fz {
class thread_pool;
}

// IPv6 capable, non-blocking socket class for use with wxWidgets.
// Error codes are the same as used by the POSIX socket functions,
// see 'man 2 socket', 'man 2 connect', ...

enum class SocketEventType
{
	// This is a nonfatal condition. It
	// means there are additional addresses to try.
	connection_next,

	connection,
	read,
	write,
	close
};

class CSocketEventSource
{
public:
	virtual ~CSocketEventSource() = default;
};

struct socket_event_type;
typedef fz::simple_event<socket_event_type, CSocketEventSource*, SocketEventType, int> CSocketEvent;

struct hostaddress_event_type;
typedef fz::simple_event<hostaddress_event_type, CSocketEventSource*, std::string> CHostAddressEvent;

void RemoveSocketEvents(fz::event_handler * handler, CSocketEventSource const* const source);
void ChangeSocketEventHandler(fz::event_handler * oldHandler, fz::event_handler * newHandler, CSocketEventSource const* const source);

class CSocketThread;
class CSocket final : public CSocketEventSource
{
	friend class CSocketThread;
public:
	CSocket(fz::thread_pool& pool, fz::event_handler* pEvtHandler);
	virtual ~CSocket();

	CSocket(CSocket const&) = delete;
	CSocket& operator=(CSocket const&) = delete;

	enum SocketState
	{
		// How the socket is initially
		none,

		// Only in listening and connecting states you can get a connection event.
		// After sending the event, socket is in connected state
		listening,
		connecting,

		// Only in this state you can get send or receive events
		connected,

		// Graceful shutdown, you get close event once done
		closing,
		closed
	};
	SocketState GetState();

	enum address_family
	{
		unspec, // AF_UNSPEC
		ipv4,   // AF_INET
		ipv6    // AF_INET6
	};

	// Connects to the given host, given as name, IPv4 or IPv6 address.
	// Returns 0 on success, else an error code. Note: EINPROGRESS is
	// not really an error. On success, you should still wait for the
	// connection event.
	// If host is a name that can be resolved, a hostaddress socket event gets sent.
	// Once connections got established, a connection event gets sent. If
	// connection could not be established, a close event gets sent.
	int Connect(fz::native_string const& host, unsigned int port, address_family family = unspec, std::string const& bind = std::string());

	// After receiving a send or receive event, you can call these functions
	// as long as their return value is positive.
	int Read(void *buffer, unsigned int size, int& error);
	int Peek(void *buffer, unsigned int size, int& error);
	int Write(const void *buffer, unsigned int size, int& error);

	int Close();

	// Returns empty string on error
	std::string GetLocalIP(bool strip_zone_index = false) const;
	std::string GetPeerIP(bool strip_zone_index = false) const;

	// Returns the hostname passed to Connect()
	fz::native_string GetPeerHost() const;

	// -1 on error
	int GetLocalPort(int& error);
	int GetRemotePort(int& error);

	// If connected, either ipv4 or ipv6, unspec otherwise
	address_family GetAddressFamily() const;

	static std::string GetErrorString(int error);
	static fz::native_string GetErrorDescription(int error);

	// Due to asynchronicity it is possible that the old handler receives one last
	// socket event when changing handlers.
	void SetEventHandler(fz::event_handler* pEvtHandler);

	static void Cleanup(bool force);

	static std::string AddressToString(const struct sockaddr* addr, int addr_len, bool with_port = true, bool strip_zone_index = false);
	static std::string AddressToString(char const* buf, int buf_len);

	int Listen(address_family family, int port = 0);
	CSocket* Accept(int& error);

	enum Flags
	{
		flag_nodelay = 0x01,
		flag_keepalive = 0x02
	};

	int GetFlags() const { return m_flags; }
	void SetFlags(int flags);

	// If called on listen socket, sizes will be inherited by
	// accepted sockets
	int SetBufferSizes(int size_receive, int size_send);

	// Duration must not be smaller than 5 minutes.
	// Default interval if 2 hours.
	void SetKeepaliveInterval(fz::duration const& d);

	// On a connected socket, gets the ideal send buffer size or
	// -1 if it cannot be determined.
	//
	// Currently only implemented for Windows.
	int GetIdealSendBufferSize();

protected:
	static int DoSetFlags(int fd, int flags, int flags_mask, fz::duration const&);
	static int DoSetBufferSizes(int fd, int size_read, int size_write);
	static int SetNonblocking(int fd);

	// Note: Unlocks the lock.
	void DetachThread(fz::scoped_lock & l);

	fz::thread_pool & thread_pool_;
	fz::event_handler* m_pEvtHandler;

	int m_fd{-1};

	SocketState m_state{none};

	CSocketThread* m_pSocketThread{};

	fz::native_string m_host;
	unsigned int m_port{};
	int m_family;

	int m_flags{};
	fz::duration m_keepalive_interval;

	int m_buffer_sizes[2];
};

#ifdef FZ_WINDOWS

#ifndef EISCONN
#define EISCONN WSAEISCONN
#endif
#ifndef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT WSAEAFNOSUPPORT
#endif
#ifndef EADDRINUSE
#define EADDRINUSE WSAEADDRINUSE
#endif
#ifndef ENOBUFS
#define ENOBUFS WSAENOBUFS
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT WSAEPROTONOSUPPORT
#endif
#ifndef EALREADY
#define EALREADY WSAEALREADY
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED WSAECONNREFUSED
#endif
#ifndef ENOTSOCK
#define ENOTSOCK WSAENOTSOCK
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT WSAETIMEDOUT
#endif
#ifndef ENETUNREACH
#define ENETUNREACH WSAENETUNREACH
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH WSAEHOSTUNREACH
#endif
#ifndef ENOTCONN
#define ENOTCONN WSAENOTCONN
#endif
#ifndef ENETRESET
#define ENETRESET WSAENETRESET
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP WSAEOPNOTSUPP
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN WSAESHUTDOWN
#endif
#ifndef EMSGSIZE
#define EMSGSIZE WSAEMSGSIZE
#endif
#ifndef ECONNABORTED
#define ECONNABORTED WSAECONNABORTED
#endif
#ifndef ECONNRESET
#define ECONNRESET WSAECONNRESET
#endif
#ifndef EHOSTDOWN
#define EHOSTDOWN WSAEHOSTDOWN
#endif

// For the future:
// Handle ERROR_NETNAME_DELETED=64
#endif //FZ_WINDOWS

#endif
