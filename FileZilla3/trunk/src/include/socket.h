#ifndef FILEZILLA_ENGINE_SOCKET_HEADER
#define FILEZILLA_ENGINE_SOCKET_HEADER

#include <libfilezilla/event_handler.hpp>

#include <errno.h>

namespace fz {
class thread_pool;

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

/**
 * \brief All classes sending socket events should derive from this.
 *
 * Allows implementing socket layers, e.g. for TLS.
 *
 * \sa fz::RemoveSocketEvents
 * \sa fz::ChangeSocketEventHandler
 */
class socket_event_source
{
public:
	virtual ~socket_event_source() = default;
};

/// \private
struct socket_event_type;

/**
 * All socket events are sent through this. See \ref fz::SocketEventType
 */
typedef fz::simple_event<socket_event_type, socket_event_source*, SocketEventType, int> CSocketEvent;

/// \private
struct hostaddress_event_type;

/**
* Whenever a hostname has been resolved to an IP address, this event is sent with the resolved IP address literal .
*/
typedef fz::simple_event<hostaddress_event_type, socket_event_source*, std::string> CHostAddressEvent;

/**
 * \brief Remove all pendinmg socket events from source sent to handler.
 *
 * Useful e.g. if you want to destroy the handler but keep the source.
 * This function is called, through ChangeSocketEventHandler, by socket::set_event_handler(0)
 */
void RemoveSocketEvents(fz::event_handler * handler, socket_event_source const* const source);

/**
 * \brief Changes all pending socket events from source
 *
 * If newHandler is null, RemoveSocketEvents is called.
 *
 * This function is called by socket::set_event_handler().
 *
 * \example Possible use-cases: Handoff after proxy handshakes, or handoff to TLS classes in
			case of STARTTLS mechanism
 */
void ChangeSocketEventHandler(fz::event_handler * oldHandler, fz::event_handler * newHandler, socket_event_source const* const source);

/// \private
class socket_thread;

/**
 * \brief IPv6 capable, non-blocking socket class
 *
 * Uses and edge-triggered socket events.
 *
 * Error codes are the same as used by the POSIX socket functions,
 * see 'man 2 socket', 'man 2 connect', ...
 */
class socket final : public socket_event_source
{
	friend class socket_thread;
public:
	socket(fz::thread_pool& pool, fz::event_handler* pEvtHandler);
	virtual ~socket();

	socket(socket const&) = delete;
	socket& operator=(socket const&) = delete;

	enum socket_state
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
	socket_state get_state();

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

	void set_event_handler(fz::event_handler* pEvtHandler);

	static void cleanup(bool force);

	static std::string AddressToString(const struct sockaddr* addr, int addr_len, bool with_port = true, bool strip_zone_index = false);
	static std::string AddressToString(char const* buf, int buf_len);

	int listen(address_family family, int port = 0);
	socket* accept(int& error);

	enum
	{
		flag_nodelay = 0x01,
		flag_keepalive = 0x02
	};

	int GetFlags() const { return flags_; }
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

private:
	static int DoSetFlags(int fd, int flags, int flags_mask, fz::duration const&);
	static int DoSetBufferSizes(int fd, int size_read, int size_write);
	static int SetNonblocking(int fd);

	// Note: Unlocks the lock.
	void DetachThread(fz::scoped_lock & l);

	fz::thread_pool & thread_pool_;
	fz::event_handler* evt_handler_;

	int m_fd{ -1 };

	socket_state m_state{ none };

	socket_thread* socket_thread_{};

	fz::native_string m_host;
	unsigned int m_port{};
	int m_family;

	int flags_{};
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

}

#endif
