#include <libfilezilla/libfilezilla.hpp>
#ifdef FZ_WINDOWS
  #include <libfilezilla/private/windows.hpp>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h>
#endif
#include <filezilla.h>
#include <libfilezilla/format.hpp>
#include <libfilezilla/mutex.hpp>
#include <libfilezilla/thread_pool.hpp>
#include "socket.h"
#ifndef FZ_WINDOWS
  #define mutex mutex_override // Sadly on some platforms system headers include conflicting names
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
    #include <signal.h>
  #endif
  #undef mutex
#endif

#include <string.h>

// Fixups needed on FreeBSD
#if !defined(EAI_ADDRFAMILY) && defined(EAI_FAMILY)
  #define EAI_ADDRFAMILY EAI_FAMILY
#endif

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0
#endif

// Union for strict aliasing-safe casting between
// the different address types
union sockaddr_u
{
	struct sockaddr_storage storage;
	struct sockaddr sockaddr;
	struct sockaddr_in in4;
	struct sockaddr_in6 in6;
};

#define WAIT_CONNECT 0x01
#define WAIT_READ	 0x02
#define WAIT_WRITE	 0x04
#define WAIT_ACCEPT  0x08
#define WAIT_CLOSE	 0x10
#define WAIT_EVENTCOUNT 5

class CSocketThread;

namespace {
static std::vector<CSocketThread*> waiting_socket_threads;
static fz::mutex waiting_socket_threads_mutex{ false };
}

void RemoveSocketEvents(fz::event_handler * handler, CSocketEventSource const* const source)
{
	auto socketEventFilter = [&](fz::event_loop::Events::value_type const& ev) -> bool {
		if (ev.first != handler) {
			return false;
		}
		else if (ev.second->derived_type() == CSocketEvent::type()) {
			return std::get<0>(static_cast<CSocketEvent const&>(*ev.second).v_) == source;
		}
		else if (ev.second->derived_type() == CHostAddressEvent::type()) {
			return std::get<0>(static_cast<CHostAddressEvent const&>(*ev.second).v_) == source;
		}
		return false;
	};

	handler->event_loop_.filter_events(socketEventFilter);
}

void ChangeSocketEventHandler(fz::event_handler * oldHandler, fz::event_handler * newHandler, CSocketEventSource const* const source)
{
	if (!oldHandler)
		return;
	if (oldHandler == newHandler) {
		return;
	}

	if (!newHandler) {
		RemoveSocketEvents(oldHandler, source);
	}
	else {
		auto socketEventFilter = [&](fz::event_loop::Events::value_type & ev) -> bool {
			if (ev.first == oldHandler) {
				if (ev.second->derived_type() == CSocketEvent::type()) {
					if (std::get<0>(static_cast<CSocketEvent const&>(*ev.second).v_) == source) {
						ev.first = newHandler;
					}
				}
				else if (ev.second->derived_type() == CHostAddressEvent::type()) {
					if (std::get<0>(static_cast<CHostAddressEvent const&>(*ev.second).v_) == source) {
						ev.first = newHandler;
					}
				}
			}
			return false;
		};

		oldHandler->event_loop_.filter_events(socketEventFilter);
	}
}

namespace {
#ifdef FZ_WINDOWS
static int ConvertMSWErrorCode(int error)
{
	switch (error)
	{
	case WSAECONNREFUSED:
		return ECONNREFUSED;
	case WSAECONNABORTED:
		return ECONNABORTED;
	case WSAEINVAL:
		return EAI_BADFLAGS;
	case WSANO_RECOVERY:
		return EAI_FAIL;
	case WSAEAFNOSUPPORT:
		return EAI_FAMILY;
	case WSA_NOT_ENOUGH_MEMORY:
		return EAI_MEMORY;
	case WSANO_DATA:
		return EAI_NODATA;
	case WSAHOST_NOT_FOUND:
		return EAI_NONAME;
	case WSATYPE_NOT_FOUND:
		return EAI_SERVICE;
	case WSAESOCKTNOSUPPORT:
		return EAI_SOCKTYPE;
	case WSAEWOULDBLOCK:
		return EAGAIN;
	case WSAEMFILE:
		return EMFILE;
	case WSAEINTR:
		return EINTR;
	case WSAEFAULT:
		return EFAULT;
	case WSAEACCES:
		return EACCES;
	case WSAETIMEDOUT:
		return ETIMEDOUT;
	case WSAECONNRESET:
		return ECONNRESET;
	case WSAEHOSTDOWN:
		return EHOSTDOWN;
	case WSAENETUNREACH:
		return ENETUNREACH;
	case WSAEADDRINUSE:
		return EADDRINUSE;
	default:
		return error;
	}
}

int GetLastSocketError()
{
	return ConvertMSWErrorCode(WSAGetLastError());
}
#else
inline int GetLastSocketError() { return errno; }
#endif
}

class CSocketThread final
{
	friend class CSocket;
public:
	CSocketThread()
		: m_sync(false)
	{
#ifdef FZ_WINDOWS
		m_sync_event = WSA_INVALID_EVENT;
#else
		m_pipe[0] = -1;
		m_pipe[1] = -1;
#endif
		for (int i = 0; i < WAIT_EVENTCOUNT; ++i) {
			m_triggered_errors[i] = 0;
		}
	}

	~CSocketThread()
	{
		thread_.join();
#ifdef FZ_WINDOWS
		if (m_sync_event != WSA_INVALID_EVENT)
			WSACloseEvent(m_sync_event);
#else
		if (m_pipe[0] != -1)
			close(m_pipe[0]);
		if (m_pipe[1] != -1)
			close(m_pipe[1]);
#endif
	}

	void SetSocket(CSocket* pSocket)
	{
		fz::scoped_lock l(m_sync);
		SetSocket(pSocket, l);
	}

	void SetSocket(CSocket* pSocket, fz::scoped_lock const&)
	{
		m_pSocket = pSocket;

		m_host.clear();
		m_port.clear();

		m_waiting = 0;
	}

	int Connect(std::string const& bind)
	{
		assert(m_pSocket);
		if (!m_pSocket) {
			return EINVAL;
		}

		m_host = fz::to_utf8(m_pSocket->m_host);
		if (m_host.empty()) {
			return EINVAL;
		}

		m_bind = bind;

		// Connect method of CSocket ensures port is in range
		char tmp[7];
		sprintf(tmp, "%u", m_pSocket->m_port);
		tmp[5] = 0;
		m_port = tmp;

		Start();

		return 0;
	}

	int Start()
	{
		if (m_started) {
			fz::scoped_lock l(m_sync);
			assert(m_threadwait);
			m_waiting = 0;
			WakeupThread(l);
			return 0;
		}
		m_started = true;
#ifdef FZ_WINDOWS
		if (m_sync_event == WSA_INVALID_EVENT)
			m_sync_event = WSACreateEvent();
		if (m_sync_event == WSA_INVALID_EVENT)
			return 1;
#else
		if (m_pipe[0] == -1) {
			if (pipe(m_pipe))
				return errno;
		}
#endif

		thread_ = m_pSocket->thread_pool_.spawn([this]() { entry(); });

		return thread_ ? 0 : 1;
	}

	// Cancels select or idle wait
	void WakeupThread()
	{
		fz::scoped_lock l(m_sync);
		WakeupThread(l);
	}

	void WakeupThread(fz::scoped_lock & l)
	{
		if (!m_started || m_finished) {
			return;
		}

		if (m_threadwait) {
			m_threadwait = false;
			m_condition.signal(l);
			return;
		}

#ifdef FZ_WINDOWS
		WSASetEvent(m_sync_event);
#else
		char tmp = 0;

		int ret;
		do {
			ret = write(m_pipe[1], &tmp, 1);
		} while (ret == -1 && errno == EINTR);
#endif
	}

protected:
	static int CreateSocketFd(addrinfo const& addr)
	{
		int fd;
#if defined(SOCK_CLOEXEC) && !defined(FZ_WINDOWS)
		fd = socket(addr.ai_family, addr.ai_socktype | SOCK_CLOEXEC, addr.ai_protocol);
		if (fd == -1 && errno == EINVAL)
#endif
		{
			fd = socket(addr.ai_family, addr.ai_socktype, addr.ai_protocol);
		}

		if (fd != -1) {
#if defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
			// We do not want SIGPIPE if writing to socket.
			const int value = 1;
			setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(int));
#endif
			CSocket::SetNonblocking(fd);
		}

		return fd;
	}

	static void CloseSocketFd(int& fd)
	{
		if (fd != -1) {
	#ifdef FZ_WINDOWS
			closesocket(fd);
	#else
			close(fd);
	#endif
			fd = -1;
		}
	}

	int TryConnectHost(addrinfo & addr, sockaddr_u const& bindAddr, fz::scoped_lock & l)
	{
		if (m_pSocket->m_pEvtHandler) {
			m_pSocket->m_pEvtHandler->send_event<CHostAddressEvent>(m_pSocket, CSocket::AddressToString(addr.ai_addr, addr.ai_addrlen));
		}

		int fd = CreateSocketFd(addr);
		if (fd == -1) {
			if (m_pSocket->m_pEvtHandler) {
				m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, addr.ai_next ? SocketEventType::connection_next : SocketEventType::connection, GetLastSocketError());
			}

			return 0;
		}

		if (bindAddr.sockaddr.sa_family != AF_UNSPEC && bindAddr.sockaddr.sa_family == addr.ai_family) {
			(void)bind(fd, &bindAddr.sockaddr, sizeof(bindAddr));
		}

		CSocket::DoSetFlags(fd, m_pSocket->m_flags, m_pSocket->m_flags, m_pSocket->m_keepalive_interval);
		CSocket::DoSetBufferSizes(fd, m_pSocket->m_buffer_sizes[0], m_pSocket->m_buffer_sizes[1]);

		int res = connect(fd, addr.ai_addr, addr.ai_addrlen);
		if (res == -1) {
#ifdef FZ_WINDOWS
			// Map to POSIX error codes
			int error = WSAGetLastError();
			if (error == WSAEWOULDBLOCK)
				res = EINPROGRESS;
			else
				res = GetLastSocketError();
#else
			res = errno;
#endif
		}

		if (res == EINPROGRESS) {

			m_pSocket->m_fd = fd;

			bool wait_successful;
			do {
				wait_successful = DoWait(WAIT_CONNECT, l);
				if ((m_triggered & WAIT_CONNECT))
					break;
			} while (wait_successful);

			if (!wait_successful) {
				CloseSocketFd(fd);
				if (m_pSocket)
					m_pSocket->m_fd = -1;
				return -1;
			}
			m_triggered &= ~WAIT_CONNECT;

			res = m_triggered_errors[0];
		}

		if (res) {
			if (m_pSocket->m_pEvtHandler) {
				m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, addr.ai_next ? SocketEventType::connection_next : SocketEventType::connection, res);
			}

			CloseSocketFd(fd);
			m_pSocket->m_fd = -1;
		}
		else {
			m_pSocket->m_fd = fd;
			m_pSocket->m_state = CSocket::connected;

			if (m_pSocket->m_pEvtHandler) {
				m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::connection, 0);
			}

			// We're now interested in all the other nice events
			m_waiting |= WAIT_READ | WAIT_WRITE;

			return 1;
		}

		return 0;
	}

	// Only call while locked
	bool DoConnect(fz::scoped_lock & l)
	{
		if (m_host.empty() || m_port.empty()) {
			m_pSocket->m_state = CSocket::closed;
			return false;
		}

		std::string host, port, bind;
		std::swap(host, m_host);
		std::swap(port, m_port);
		std::swap(bind, m_bind);

		sockaddr_u bindAddr{};

		if (!bind.empty()) {
			// Convert bind address
			addrinfo bind_hints{};
			bind_hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
			bind_hints.ai_socktype = SOCK_STREAM;
			addrinfo *bindAddressList{};
			int res = getaddrinfo(bind.empty() ? 0 : bind.c_str(), "0", &bind_hints, &bindAddressList);
			if (!res && bindAddressList) {
				if (bindAddressList->ai_addr) {
					memcpy(&bindAddr.storage, bindAddressList->ai_addr, bindAddressList->ai_addrlen);
				}
				freeaddrinfo(bindAddressList);
			}
		}

		addrinfo hints{};
		hints.ai_family = m_pSocket->m_family;

		l.unlock();

		hints.ai_socktype = SOCK_STREAM;
#ifdef AI_IDN
		hints.ai_flags |= AI_IDN;
#endif

		addrinfo *addressList{};
		int res = getaddrinfo(host.c_str(), port.c_str(), &hints, &addressList);

		l.lock();

		if (ShouldQuit()) {
			if (!res && addressList)
				freeaddrinfo(addressList);
			if (m_pSocket)
				m_pSocket->m_state = CSocket::closed;
			return false;
		}

		// If state isn't connecting, Close() was called.
		// If m_pHost is set, Close() was called and Connect()
		// afterwards, state is back at connecting.
		// In either case, we need to abort this connection attempt.
		if (m_pSocket->m_state != CSocket::connecting || !m_host.empty()) {
			if (!res && addressList)
				freeaddrinfo(addressList);
			return false;
		}

		if (res) {
#ifdef FZ_WINDOWS
			res = ConvertMSWErrorCode(res);
#endif

			if (m_pSocket->m_pEvtHandler) {
				m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::connection, res);
			}
			m_pSocket->m_state = CSocket::closed;

			return false;
		}

		for (addrinfo *addr = addressList; addr; addr = addr->ai_next) {
			res = TryConnectHost(*addr, bindAddr, l);
			if (res == -1) {
				freeaddrinfo(addressList);
				if (m_pSocket)
					m_pSocket->m_state = CSocket::closed;
				return false;
			}
			else if (res) {
				freeaddrinfo(addressList);
				return true;
			}
		}
		freeaddrinfo(addressList);

		if (m_pSocket->m_pEvtHandler) {
			m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::connection, ECONNABORTED);
		}
		m_pSocket->m_state = CSocket::closed;

		return false;
	}

	bool ShouldQuit() const
	{
		return m_quit || !m_pSocket;
	}

	// Call only while locked
	bool DoWait(int wait, fz::scoped_lock & l)
	{
		m_waiting |= wait;

		for (;;) {
#ifdef FZ_WINDOWS
			int wait_events = FD_CLOSE;
			if (m_waiting & WAIT_CONNECT) {
				wait_events |= FD_CONNECT;
			}
			if (m_waiting & WAIT_READ) {
				wait_events |= FD_READ;
			}
			if (m_waiting & WAIT_WRITE) {
				wait_events |= FD_WRITE;
			}
			if (m_waiting & WAIT_ACCEPT) {
				wait_events |= FD_ACCEPT;
			}
			if (m_waiting & WAIT_CLOSE) {
				wait_events |= FD_CLOSE;
			}
			WSAEventSelect(m_pSocket->m_fd, m_sync_event, wait_events);
			l.unlock();
			WSAWaitForMultipleEvents(1, &m_sync_event, false, WSA_INFINITE, false);

			l.lock();
			if (ShouldQuit()) {
				return false;
			}

			WSANETWORKEVENTS events;
			int res = WSAEnumNetworkEvents(m_pSocket->m_fd, m_sync_event, &events);
			if (res) {
				res = GetLastSocketError();
				return false;
			}

			if (m_waiting & WAIT_CONNECT) {
				if (events.lNetworkEvents & FD_CONNECT) {
					m_triggered |= WAIT_CONNECT;
					m_triggered_errors[0] = ConvertMSWErrorCode(events.iErrorCode[FD_CONNECT_BIT]);
					m_waiting &= ~WAIT_CONNECT;
				}
			}
			if (m_waiting & WAIT_READ) {
				if (events.lNetworkEvents & FD_READ) {
					m_triggered |= WAIT_READ;
					m_triggered_errors[1] = ConvertMSWErrorCode(events.iErrorCode[FD_READ_BIT]);
					m_waiting &= ~WAIT_READ;
				}
			}
			if (m_waiting & WAIT_WRITE) {
				if (events.lNetworkEvents & FD_WRITE) {
					m_triggered |= WAIT_WRITE;
					m_triggered_errors[2] = ConvertMSWErrorCode(events.iErrorCode[FD_WRITE_BIT]);
					m_waiting &= ~WAIT_WRITE;
				}
			}
			if (m_waiting & WAIT_ACCEPT) {
				if (events.lNetworkEvents & FD_ACCEPT) {
					m_triggered |= WAIT_ACCEPT;
					m_triggered_errors[3] = ConvertMSWErrorCode(events.iErrorCode[FD_ACCEPT_BIT]);
					m_waiting &= ~WAIT_ACCEPT;
				}
			}
			if (m_waiting & WAIT_CLOSE) {
				if (events.lNetworkEvents & FD_CLOSE) {
					m_triggered |= WAIT_CLOSE;
					m_triggered_errors[4] = ConvertMSWErrorCode(events.iErrorCode[FD_CLOSE_BIT]);
					m_waiting &= ~WAIT_CLOSE;
				}
			}

			if (m_triggered || !m_waiting)
				return true;
#else
			fd_set readfds;
			fd_set writefds;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);

			FD_SET(m_pipe[0], &readfds);
			if (!(m_waiting & WAIT_CONNECT)) {
				FD_SET(m_pSocket->m_fd, &readfds);
			}

			if (m_waiting & (WAIT_WRITE | WAIT_CONNECT)) {
				FD_SET(m_pSocket->m_fd, &writefds);
			}

			int maxfd = std::max(m_pipe[0], m_pSocket->m_fd) + 1;

			l.unlock();

			int res = select(maxfd, &readfds, &writefds, 0, 0);

			l.lock();

			if (res > 0 && FD_ISSET(m_pipe[0], &readfds)) {
				char buffer[100];
				int damn_spurious_warning = read(m_pipe[0], buffer, 100);
				(void)damn_spurious_warning; // We do not care about return value and this is definitely correct!
			}

			if (m_quit || !m_pSocket || m_pSocket->m_fd == -1) {
				return false;
			}

			if (!res) {
				continue;
			}
			if (res == -1) {
				res = errno;

				if (res == EINTR) {
					continue;
				}

				return false;
			}

			if (m_waiting & WAIT_CONNECT) {
				if (FD_ISSET(m_pSocket->m_fd, &writefds)) {
					int error;
					socklen_t len = sizeof(error);
					int getsockopt_res = getsockopt(m_pSocket->m_fd, SOL_SOCKET, SO_ERROR, &error, &len);
					if (getsockopt_res) {
						error = errno;
					}
					m_triggered |= WAIT_CONNECT;
					m_triggered_errors[0] = error;
					m_waiting &= ~WAIT_CONNECT;
				}
			}
			else if (m_waiting & WAIT_ACCEPT) {
				if (FD_ISSET(m_pSocket->m_fd, &readfds)) {
					m_triggered |= WAIT_ACCEPT;
					m_waiting &= ~WAIT_ACCEPT;
				}
			}
			else if (m_waiting & WAIT_READ) {
				if (FD_ISSET(m_pSocket->m_fd, &readfds)) {
					m_triggered |= WAIT_READ;
					m_waiting &= ~WAIT_READ;
				}
			}
			if (m_waiting & WAIT_WRITE) {
				if (FD_ISSET(m_pSocket->m_fd, &writefds)) {
					m_triggered |= WAIT_WRITE;
					m_waiting &= ~WAIT_WRITE;
				}
			}

			if (m_triggered || !m_waiting) {
				return true;
			}
#endif
		}
	}

	void SendEvents()
	{
		if (!m_pSocket || !m_pSocket->m_pEvtHandler)
			return;
		if (m_triggered & WAIT_READ) {
			m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::read, m_triggered_errors[1]);
			m_triggered &= ~WAIT_READ;
		}
		if (m_triggered & WAIT_WRITE) {
			m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::write, m_triggered_errors[2]);
			m_triggered &= ~WAIT_WRITE;
		}
		if (m_triggered & WAIT_ACCEPT) {
			m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::connection, m_triggered_errors[3]);
			m_triggered &= ~WAIT_ACCEPT;
		}
		if (m_triggered & WAIT_CLOSE) {
			SendCloseEvent();
		}
	}

	void SendCloseEvent()
	{
		if( !m_pSocket || !m_pSocket->m_pEvtHandler ) {
			return;
		}

#ifdef FZ_WINDOWS
		// MSDN says this:
		//   FD_CLOSE being posted after all data is read from a socket.
		//   An application should check for remaining data upon receipt
		//   of FD_CLOSE to avoid any possibility of losing data.
		// First half is actually plain wrong.
		char buf;
		if( !m_triggered_errors[4] && recv( m_pSocket->m_fd, &buf, 1, MSG_PEEK ) > 0) {
			if( !(m_waiting & WAIT_READ) ) {
				return;
			}
			m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::read, 0);
		}
		else
#endif
		{
			m_pSocket->m_pEvtHandler->send_event<CSocketEvent>(m_pSocket, SocketEventType::close, m_triggered_errors[4]);
			m_triggered &= ~WAIT_CLOSE;
		}
	}

	// Call only while locked
	bool IdleLoop(fz::scoped_lock & l)
	{
		if (m_quit)
			return false;
		while (!m_pSocket || (!m_waiting && m_host.empty())) {
			m_threadwait = true;
			m_condition.wait(l);

			if (m_quit)
				return false;
		}

		return true;
	}

	void entry()
	{
		fz::scoped_lock l(m_sync);
		for (;;) {
			if (!IdleLoop(l)) {
				m_finished = true;
				return;
			}

			if (m_pSocket->m_state == CSocket::listening) {
				while (IdleLoop(l)) {
					if (m_pSocket->m_fd == -1) {
						m_waiting = 0;
						break;
					}
					if (!DoWait(0, l))
						break;
					SendEvents();
				}
			}
			else {
				if (m_pSocket->m_state == CSocket::connecting) {
					if (!DoConnect(l))
						continue;
				}

#ifdef FZ_WINDOWS
				m_waiting |= WAIT_CLOSE;
				int wait_close = WAIT_CLOSE;
#endif
				while (IdleLoop(l)) {
					if (m_pSocket->m_fd == -1) {
						m_waiting = 0;
						break;
					}
					bool res = DoWait(0, l);

					if (m_triggered & WAIT_CLOSE && m_pSocket) {
						m_pSocket->m_state = CSocket::closing;
#ifdef FZ_WINDOWS
						wait_close = 0;
#endif
					}

					if (!res)
						break;

					SendEvents();
#ifdef FZ_WINDOWS
					m_waiting |= wait_close;
#endif
				}
			}
		}

		m_finished = true;
		return;
	}

	CSocket* m_pSocket{};

	std::string m_host;
	std::string m_port;
	std::string m_bind;

#ifdef FZ_WINDOWS
	// We wait on this using WSAWaitForMultipleEvents
	WSAEVENT m_sync_event;
#else
	// A pipe is used to unblock select
	int m_pipe[2];
#endif

	fz::mutex m_sync;
	fz::condition m_condition;

	bool m_started{};
	bool m_quit{};
	bool m_finished{};

	// The socket events we are waiting for
	int m_waiting{};

	// The triggered socket events
	int m_triggered{};
	int m_triggered_errors[WAIT_EVENTCOUNT];

	// Thread waits for instructions
	bool m_threadwait{};

	fz::async_task thread_;
};

CSocket::CSocket(fz::thread_pool & pool, fz::event_handler* pEvtHandler)
	: thread_pool_(pool)
	, m_pEvtHandler(pEvtHandler)
	, m_keepalive_interval(fz::duration::from_hours(2))
{
	m_family = AF_UNSPEC;

	m_buffer_sizes[0] = -1;
	m_buffer_sizes[1] = -1;
}

CSocket::~CSocket()
{
	if (m_state != none) {
		Close();
	}

	if (m_pSocketThread) {
		fz::scoped_lock l(m_pSocketThread->m_sync);
		DetachThread(l);
	}
}

void CSocket::DetachThread(fz::scoped_lock & l)
{
	if (!m_pSocketThread) {
		return;
	}

	m_pSocketThread->SetSocket(0, l);
	if (m_pSocketThread->m_finished) {
		m_pSocketThread->WakeupThread(l);
		l.unlock();
		delete m_pSocketThread;
	}
	else {
		if (!m_pSocketThread->m_started) {
			l.unlock();
			delete m_pSocketThread;
		}
		else {
			m_pSocketThread->m_quit = true;
			m_pSocketThread->WakeupThread(l);
			l.unlock();

			fz::scoped_lock wl(waiting_socket_threads_mutex);
			waiting_socket_threads.push_back(m_pSocketThread);
		}
	}
	m_pSocketThread = 0;

	Cleanup(false);
}

int CSocket::Connect(fz::native_string const& host, unsigned int port, address_family family, std::string const& bind)
{
	if (m_state != none)
		return EISCONN;

	if (port < 1 || port > 65535)
		return EINVAL;

	if (host.empty()) {
		return EINVAL;
	}

	int af{};

	switch (family)
	{
	case unspec:
		af = AF_UNSPEC;
		break;
	case ipv4:
		af = AF_INET;
		break;
	case ipv6:
		af = AF_INET6;
		break;
	default:
		return EINVAL;
	}

	if (m_pSocketThread && m_pSocketThread->m_started) {
		fz::scoped_lock l(m_pSocketThread->m_sync);
		if (!m_pSocketThread->m_threadwait) {
			// Possibly inside a blocking call, e.g. getaddrinfo.
			// Detach the thread so that we can continue.
			DetachThread(l);
		}
	}
	if (!m_pSocketThread) {
		m_pSocketThread = new CSocketThread();
		m_pSocketThread->SetSocket(this);
	}

	m_family = af;
	m_state = connecting;

	m_host = host;
	m_port = port;
	int res = m_pSocketThread->Connect(bind);
	if (res) {
		m_state = none;
		delete m_pSocketThread;
		m_pSocketThread = 0;
		return res;
	}

	return EINPROGRESS;
}

void CSocket::SetEventHandler(fz::event_handler* pEvtHandler)
{
	if (m_pSocketThread) {
		fz::scoped_lock l(m_pSocketThread->m_sync);

		if (m_pEvtHandler == pEvtHandler) {
			return;
		}

		ChangeSocketEventHandler(m_pEvtHandler, pEvtHandler, this);

		m_pEvtHandler = pEvtHandler;

		if (pEvtHandler && m_state == connected) {
#ifdef FZ_WINDOWS
			// If a graceful shutdown is going on in background already,
			// no further events are recorded. Send out events we're not
			// waiting for (i.e. they got triggered already) manually.

			if (!(m_pSocketThread->m_waiting & WAIT_WRITE)) {
				pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::write, 0);
			}

			pEvtHandler->send_event<CSocketEvent>(this, SocketEventType::read, 0);
			if (m_pSocketThread->m_waiting & WAIT_READ) {
				m_pSocketThread->m_waiting &= ~WAIT_READ;
				m_pSocketThread->WakeupThread(l);
			}
#else
			m_pSocketThread->m_waiting |= WAIT_READ | WAIT_WRITE;
			m_pSocketThread->WakeupThread(l);
#endif
		}
		else if (pEvtHandler && m_state == closing) {
			if (!(m_pSocketThread->m_triggered & WAIT_READ)) {
				m_pSocketThread->m_waiting |= WAIT_READ;
			}
			m_pSocketThread->SendEvents();
		}
	}
	else {
		ChangeSocketEventHandler(m_pEvtHandler, pEvtHandler, this);
		m_pEvtHandler = pEvtHandler;
	}
}

#define ERRORDECL(c, desc) { c, #c, desc },

struct Error_table
{
	int code;
	char const* const name;
	char const* const description;
};

static Error_table const error_table[] =
{
	ERRORDECL(EACCES, fztranslate_mark("Permission denied"))
	ERRORDECL(EADDRINUSE, fztranslate_mark("Local address in use"))
	ERRORDECL(EAFNOSUPPORT, fztranslate_mark("The specified address family is not supported"))
	ERRORDECL(EINPROGRESS, fztranslate_mark("Operation in progress"))
	ERRORDECL(EINVAL, fztranslate_mark("Invalid argument passed"))
	ERRORDECL(EMFILE, fztranslate_mark("Process file table overflow"))
	ERRORDECL(ENFILE, fztranslate_mark("System limit of open files exceeded"))
	ERRORDECL(ENOBUFS, fztranslate_mark("Out of memory"))
	ERRORDECL(ENOMEM, fztranslate_mark("Out of memory"))
	ERRORDECL(EPERM, fztranslate_mark("Permission denied"))
	ERRORDECL(EPROTONOSUPPORT, fztranslate_mark("Protocol not supported"))
	ERRORDECL(EAGAIN, fztranslate_mark("Resource temporarily unavailable"))
	ERRORDECL(EALREADY, fztranslate_mark("Operation already in progress"))
	ERRORDECL(EBADF, fztranslate_mark("Bad file descriptor"))
	ERRORDECL(ECONNREFUSED, fztranslate_mark("Connection refused by server"))
	ERRORDECL(EFAULT, fztranslate_mark("Socket address outside address space"))
	ERRORDECL(EINTR, fztranslate_mark("Interrupted by signal"))
	ERRORDECL(EISCONN, fztranslate_mark("Socket already connected"))
	ERRORDECL(ENETUNREACH, fztranslate_mark("Network unreachable"))
	ERRORDECL(ENOTSOCK, fztranslate_mark("File descriptor not a socket"))
	ERRORDECL(ETIMEDOUT, fztranslate_mark("Connection attempt timed out"))
	ERRORDECL(EHOSTUNREACH, fztranslate_mark("No route to host"))
	ERRORDECL(ENOTCONN, fztranslate_mark("Socket not connected"))
	ERRORDECL(ENETRESET, fztranslate_mark("Connection reset by network"))
	ERRORDECL(EOPNOTSUPP, fztranslate_mark("Operation not supported"))
	ERRORDECL(ESHUTDOWN, fztranslate_mark("Socket has been shut down"))
	ERRORDECL(EMSGSIZE, fztranslate_mark("Message too large"))
	ERRORDECL(ECONNABORTED, fztranslate_mark("Connection aborted"))
	ERRORDECL(ECONNRESET, fztranslate_mark("Connection reset by peer"))
	ERRORDECL(EPIPE, fztranslate_mark("Local endpoint has been closed"))
	ERRORDECL(EHOSTDOWN, fztranslate_mark("Host is down"))

	// Getaddrinfo related
#ifdef EAI_ADDRFAMILY
	ERRORDECL(EAI_ADDRFAMILY, fztranslate_mark("Network host does not have any network addresses in the requested address family"))
#endif
	ERRORDECL(EAI_AGAIN, fztranslate_mark("Temporary failure in name resolution"))
	ERRORDECL(EAI_BADFLAGS, fztranslate_mark("Invalid value for ai_flags"))
#ifdef EAI_BADHINTS
	ERRORDECL(EAI_BADHINTS, fztranslate_mark("Invalid value for hints"))
#endif
	ERRORDECL(EAI_FAIL, fztranslate_mark("Nonrecoverable failure in name resolution"))
	ERRORDECL(EAI_FAMILY, fztranslate_mark("The ai_family member is not supported"))
	ERRORDECL(EAI_MEMORY, fztranslate_mark("Memory allocation failure"))
#ifdef EAI_NODATA
	ERRORDECL(EAI_NODATA, fztranslate_mark("No address associated with nodename"))
#endif
	ERRORDECL(EAI_NONAME, fztranslate_mark("Neither nodename nor servname provided, or not known"))
#ifdef EAI_OVERFLOW
	ERRORDECL(EAI_OVERFLOW, fztranslate_mark("Argument buffer overflow"))
#endif
#ifdef EAI_PROTOCOL
	ERRORDECL(EAI_PROTOCOL, fztranslate_mark("Resolved protocol is unknown"))
#endif
	ERRORDECL(EAI_SERVICE, fztranslate_mark("The servname parameter is not supported for ai_socktype"))
	ERRORDECL(EAI_SOCKTYPE, fztranslate_mark("The ai_socktype member is not supported"))
#ifdef EAI_SYSTEM
	ERRORDECL(EAI_SYSTEM, fztranslate_mark("Other system error"))
#endif

	// Codes that have no POSIX equivalence
#ifdef FZ_WINDOWS
	ERRORDECL(WSANOTINITIALISED, fztranslate_mark("Not initialized, need to call WSAStartup"))
	ERRORDECL(WSAENETDOWN, fztranslate_mark("System's network subsystem has failed"))
	ERRORDECL(WSAEPROTOTYPE, fztranslate_mark("Protocol not supported on given socket type"))
	ERRORDECL(WSAESOCKTNOSUPPORT, fztranslate_mark("Socket type not supported for address family"))
	ERRORDECL(WSAEADDRNOTAVAIL, fztranslate_mark("Cannot assign requested address"))
	ERRORDECL(ERROR_NETNAME_DELETED, fztranslate_mark("The specified network name is no longer available"))
#endif
	{ 0, 0, 0 }
};

std::string CSocket::GetErrorString(int error)
{
	for (int i = 0; error_table[i].code; ++i) {
		if (error != error_table[i].code) {
			continue;
		}

		return error_table[i].name;
	}

	return fz::sprintf("%d", error);
}

fz::native_string CSocket::GetErrorDescription(int error)
{
	for (int i = 0; error_table[i].code; ++i) {
		if (error != error_table[i].code) {
			continue;
		}

		return fz::to_native(fz::to_native(std::string(error_table[i].name)) + fzT(" - ") + fz::to_native(fz::translate(error_table[i].description)));
	}

	return fz::sprintf(fzT("%d"), error);
}

int CSocket::Close()
{
	if (m_pSocketThread) {
		fz::scoped_lock l(m_pSocketThread->m_sync);
		int fd = m_fd;
		m_fd = -1;

		m_pSocketThread->m_host.clear();
		m_pSocketThread->m_port.clear();

		m_pSocketThread->WakeupThread(l);

		CSocketThread::CloseSocketFd(fd);
		m_state = none;

		m_pSocketThread->m_triggered = 0;
		for (int i = 0; i < WAIT_EVENTCOUNT; ++i) {
			m_pSocketThread->m_triggered_errors[i] = 0;
		}

		if (m_pEvtHandler) {
			RemoveSocketEvents(m_pEvtHandler, this);
			m_pEvtHandler = 0;
		}
	}
	else {
		int fd = m_fd;
		m_fd = -1;
		CSocketThread::CloseSocketFd(fd);
		m_state = none;

		if (m_pEvtHandler) {
			RemoveSocketEvents(m_pEvtHandler, this);
			m_pEvtHandler = 0;
		}
	}

	return 0;
}

CSocket::SocketState CSocket::GetState()
{
	SocketState state;
	if (m_pSocketThread)
		m_pSocketThread->m_sync.lock();
	state = m_state;
	if (m_pSocketThread)
		m_pSocketThread->m_sync.unlock();

	return state;
}

void CSocket::Cleanup(bool force)
{
	fz::scoped_lock wl(waiting_socket_threads_mutex);
	auto iter = waiting_socket_threads.begin();
	for (; iter != waiting_socket_threads.end(); ++iter) {
		CSocketThread *const pThread = *iter;

		if (!force) {
			fz::scoped_lock l(pThread->m_sync);
			if (!pThread->m_finished) {
				break;
			}
		}

		delete pThread;
	}
	waiting_socket_threads.erase(waiting_socket_threads.begin(), iter);
}

int CSocket::Read(void* buffer, unsigned int size, int& error)
{
	int res = recv(m_fd, (char*)buffer, size, 0);

	if (res == -1) {
		error = GetLastSocketError();
		if (error == EAGAIN) {
			if (m_pSocketThread) {
				fz::scoped_lock l(m_pSocketThread->m_sync);
				if (!(m_pSocketThread->m_waiting & WAIT_READ)) {
					m_pSocketThread->m_waiting |= WAIT_READ;
					m_pSocketThread->WakeupThread(l);
				}
			}
		}
	}
	else
		error = 0;

	return res;
}

int CSocket::Peek(void* buffer, unsigned int size, int& error)
{
	int res = recv(m_fd, (char*)buffer, size, MSG_PEEK);

	if (res == -1) {
		error = GetLastSocketError();
	}
	else
		error = 0;

	return res;
}

int CSocket::Write(const void* buffer, unsigned int size, int& error)
{
#ifdef MSG_NOSIGNAL
	const int flags = MSG_NOSIGNAL;
#else
	const int flags = 0;

#if !defined(SO_NOSIGPIPE) && !defined(FZ_WINDOWS)
	// Some systems have neither. Need to block signal
	struct sigaction old_action;
	struct sigaction action = {};
	action.sa_handler = SIG_IGN;
	int signal_set = sigaction(SIGPIPE, &action, &old_action);
#endif

#endif

	int res = send(m_fd, (const char*)buffer, size, flags);

#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE) && !defined(FZ_WINDOWS)
	// Restore previous signal handler
	if (!signal_set)
		sigaction(SIGPIPE, &old_action, 0);
#endif

	if (res == -1) {
		error = GetLastSocketError();
		if (error == EAGAIN) {
			if (m_pSocketThread) {
				fz::scoped_lock l (m_pSocketThread->m_sync);
				if (!(m_pSocketThread->m_waiting & WAIT_WRITE)) {
					m_pSocketThread->m_waiting |= WAIT_WRITE;
					m_pSocketThread->WakeupThread(l);
				}
			}
		}
	}
	else
		error = 0;

	return res;
}

std::string CSocket::AddressToString(const struct sockaddr* addr, int addr_len, bool with_port, bool strip_zone_index)
{
	char hostbuf[NI_MAXHOST];
	char portbuf[NI_MAXSERV];

	int res = getnameinfo(addr, addr_len, hostbuf, NI_MAXHOST, portbuf, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
	if (res) // Should never fail
		return std::string();

	std::string host = hostbuf;
	std::string port = portbuf;

	// IPv6 uses colons as separator, need to enclose address
	// to avoid ambiguity if also showing port
	if (addr->sa_family == AF_INET6) {
		if (strip_zone_index) {
			auto pos = host.find('%');
			if (pos != std::string::npos) {
				host = host.substr(0, pos);
			}
		}
		if (with_port)
			host = "[" + host + "]";
	}

	if (with_port)
		return host + ":" + port;
	else
		return host;
}

std::string CSocket::AddressToString(char const* buf, int buf_len)
{
	if (buf_len != 4 && buf_len != 16) {
		return std::string();
	}

	sockaddr_u addr;
	if (buf_len == 16) {
		memcpy(&addr.in6.sin6_addr, buf, buf_len);
		addr.in6.sin6_family = AF_INET6;
	}
	else {
		memcpy(&addr.in4.sin_addr, buf, buf_len);
		addr.in4.sin_family = AF_INET;
	}

	return AddressToString(&addr.sockaddr, sizeof(addr), false, true);
}

std::string CSocket::GetLocalIP(bool strip_zone_index) const
{
	struct sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int res = getsockname(m_fd, (sockaddr*)&addr, &addr_len);
	if (res) {
		return std::string();
	}

	return AddressToString((sockaddr *)&addr, addr_len, false, strip_zone_index);
}

std::string CSocket::GetPeerIP(bool strip_zone_index) const
{
	struct sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int res = getpeername(m_fd, (sockaddr*)&addr, &addr_len);
	if (res) {
		return std::string();
	}

	return AddressToString((sockaddr *)&addr, addr_len, false, strip_zone_index);
}

CSocket::address_family CSocket::GetAddressFamily() const
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	int res = getsockname(m_fd, &addr.sockaddr, &addr_len);
	if (res) {
		return unspec;
	}

	switch (addr.sockaddr.sa_family)
	{
	case AF_INET:
		return ipv4;
	case AF_INET6:
		return ipv6;
	default:
		return unspec;
	}
}

int CSocket::Listen(address_family family, int port)
{
	if (m_state != none)
		return EALREADY;

	if (port < 0 || port > 65535)
		return EINVAL;

	switch (family)
	{
	case unspec:
		m_family = AF_UNSPEC;
		break;
	case ipv4:
		m_family = AF_INET;
		break;
	case ipv6:
		m_family = AF_INET6;
		break;
	default:
		return EINVAL;
	}

	{
		struct addrinfo hints = {};
		hints.ai_family = m_family;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
#ifdef AI_NUMERICSERV
		// Some systems like Windows or OS X don't know AI_NUMERICSERV.
		hints.ai_flags |= AI_NUMERICSERV;
#endif

		char portstring[6];
		sprintf(portstring, "%d", port);

		struct addrinfo* addressList = 0;
		int res = getaddrinfo(0, portstring, &hints, &addressList);

		if (res) {
#ifdef FZ_WINDOWS
			return ConvertMSWErrorCode(res);
#else
			return res;
#endif
		}

		for (struct addrinfo* addr = addressList; addr; addr = addr->ai_next) {
			m_fd = CSocketThread::CreateSocketFd(*addr);
			res = GetLastSocketError();

			if (m_fd == -1)
				continue;

			res = bind(m_fd, addr->ai_addr, addr->ai_addrlen);
			if (!res)
				break;

			res = GetLastSocketError();
			CSocketThread::CloseSocketFd(m_fd);
		}
		freeaddrinfo(addressList);
		if (m_fd == -1)
			return res;
	}

	int res = listen(m_fd, 1);
	if (res) {
		res = GetLastSocketError();
		CSocketThread::CloseSocketFd(m_fd);
		m_fd = -1;
		return res;
	}

	m_state = listening;

	m_pSocketThread = new CSocketThread();
	m_pSocketThread->SetSocket(this);

	m_pSocketThread->m_waiting = WAIT_ACCEPT;

	m_pSocketThread->Start();

	return 0;
}

int CSocket::GetLocalPort(int& error)
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	error = getsockname(m_fd, &addr.sockaddr, &addr_len);
	if (error) {
#ifdef FZ_WINDOWS
		error = ConvertMSWErrorCode(error);
#endif
		return -1;
	}

	if (addr.storage.ss_family == AF_INET)
		return ntohs(addr.in4.sin_port);
	else if (addr.storage.ss_family == AF_INET6)
		return ntohs(addr.in6.sin6_port);

	error = EINVAL;
	return -1;
}

int CSocket::GetRemotePort(int& error)
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	error = getpeername(m_fd, &addr.sockaddr, &addr_len);
	if (error)
	{
#ifdef FZ_WINDOWS
		error = ConvertMSWErrorCode(error);
#endif
		return -1;
	}

	if (addr.storage.ss_family == AF_INET)
		return ntohs(addr.in4.sin_port);
	else if (addr.storage.ss_family == AF_INET6)
		return ntohs(addr.in6.sin6_port);

	error = EINVAL;
	return -1;
}

CSocket* CSocket::Accept(int &error)
{
	if (m_pSocketThread) {
		fz::scoped_lock l(m_pSocketThread->m_sync);
		m_pSocketThread->m_waiting |= WAIT_ACCEPT;
		m_pSocketThread->WakeupThread(l);
	}
	int fd = accept(m_fd, 0, 0);
	if (fd == -1) {
		error = GetLastSocketError();
		return 0;
	}

#if defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
	// We do not want SIGPIPE if writing to socket.
	const int value = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(int));
#endif

	SetNonblocking(fd);

	DoSetBufferSizes(fd, m_buffer_sizes[0], m_buffer_sizes[1]);

	CSocket* pSocket = new CSocket(thread_pool_, 0);
	pSocket->m_state = connected;
	pSocket->m_fd = fd;
	pSocket->m_pSocketThread = new CSocketThread();
	pSocket->m_pSocketThread->SetSocket(pSocket);
	pSocket->m_pSocketThread->m_waiting = WAIT_READ | WAIT_WRITE;
	pSocket->m_pSocketThread->Start();

	return pSocket;
}

int CSocket::SetNonblocking(int fd)
{
	// Set socket to non-blocking.
#ifdef FZ_WINDOWS
	unsigned long nonblock = 1;
	int res = ioctlsocket(fd, FIONBIO, &nonblock);
	if (!res)
		return 0;
	else
		return ConvertMSWErrorCode(WSAGetLastError());
#else
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1)
		return errno;
	int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (res == -1)
		return errno;
	return 0;
#endif
}

void CSocket::SetFlags(int flags)
{
	if (m_pSocketThread) {
		m_pSocketThread->m_sync.lock();
	}

	if (m_fd != -1) {
		DoSetFlags(m_fd, flags, flags ^ m_flags, m_keepalive_interval);
	}
	m_flags = flags;

	if (m_pSocketThread) {
		m_pSocketThread->m_sync.unlock();
	}
}

int CSocket::DoSetFlags(int fd, int flags, int flags_mask, fz::duration const& keepalive_interval)
{
	if (flags_mask & flag_nodelay) {
		const int value = (flags & flag_nodelay) ? 1 : 0;
		int res = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value));
		if (res != 0) {
			return GetLastSocketError();
		}
	}
	if (flags_mask & flag_keepalive) {
#if FZ_WINDOWS
		tcp_keepalive v{};
		v.onoff = (flags & flag_keepalive) ? 1 : 0;
		v.keepalivetime = static_cast<ULONG>(keepalive_interval.get_milliseconds());
		v.keepaliveinterval = 1000;
		DWORD tmp{};
		int res = WSAIoctl(fd, SIO_KEEPALIVE_VALS, &v, sizeof(v), 0, 0, &tmp, 0, 0);
		if (res != 0) {
			return GetLastSocketError();
		}
#else
		const int value = (flags & flag_keepalive) ? 1 : 0;
		int res = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&value, sizeof(value));
		if (res != 0) {
			return GetLastSocketError();
		}
#ifdef TCP_KEEPIDLE
		int const idle = keepalive_interval.get_seconds();
		res = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&idle, sizeof(idle));
		if (res != 0) {
			return GetLastSocketError();
		}
#endif
#endif
	}

	return 0;
}

int CSocket::SetBufferSizes(int size_receive, int size_send)
{
	int ret = 0;

	if (m_pSocketThread)
		m_pSocketThread->m_sync.lock();

	m_buffer_sizes[0] = size_receive;
	m_buffer_sizes[1] = size_send;

	if (m_fd != -1)
		ret = DoSetBufferSizes(m_fd, size_receive, size_send);

	if (m_pSocketThread)
		m_pSocketThread->m_sync.unlock();

	return ret;
}

int CSocket::GetIdealSendBufferSize()
{
	int size = -1;

#ifdef FZ_WINDOWS
	if (m_pSocketThread)
		m_pSocketThread->m_sync.lock();

	if (m_fd != -1) {
		// MSDN says this:
		// "Dynamic send buffering for TCP was added on Windows 7 and Windows
		// Server 2008 R2.By default, dynamic send buffering for TCP is
		// enabled unless an application sets the SO_SNDBUF socket option on
		// the stream socket"
		//
		// Guess what: It doesn't do it by itself. Programs need to
		// periodically and manually update SO_SNDBUF based on what
		// SIO_IDEAL_SEND_BACKLOG_QUERY returns.
#ifndef SIO_IDEAL_SEND_BACKLOG_QUERY
#define SIO_IDEAL_SEND_BACKLOG_QUERY 0x4004747b
#endif
		ULONG v{};
		DWORD outlen{};
		if (!WSAIoctl(m_fd, SIO_IDEAL_SEND_BACKLOG_QUERY, 0, 0, &v, sizeof(v), &outlen, 0, 0)) {
			size = v;
		}
	}

	if (m_pSocketThread)
		m_pSocketThread->m_sync.unlock();
#endif

	return size;
}


int CSocket::DoSetBufferSizes(int fd, int size_read, int size_write)
{
	int ret = 0;
	if (size_read != -1) {
		int res = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&size_read, sizeof(size_read));
		if (res != 0) {
			ret = GetLastSocketError();
		}
	}

	if (size_write != -1) {
		int res = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&size_write, sizeof(size_write));
		if (res != 0) {
			ret = GetLastSocketError();
		}
	}

	return ret;
}

fz::native_string CSocket::GetPeerHost() const
{
	return m_host;
}

void CSocket::SetKeepaliveInterval(fz::duration const& d)
{
	if (d < fz::duration::from_minutes(1)) {
		return;
	}

	if (m_pSocketThread) {
		m_pSocketThread->m_sync.lock();
	}

	m_keepalive_interval = d;
	if (m_fd != -1) {
		DoSetFlags(m_fd, m_flags, flag_keepalive, m_keepalive_interval);
	}

	if (m_pSocketThread) {
		m_pSocketThread->m_sync.unlock();
	}
}
