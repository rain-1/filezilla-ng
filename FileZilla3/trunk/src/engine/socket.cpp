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

#define WAIT_CONNECT 0x01
#define WAIT_READ	 0x02
#define WAIT_WRITE	 0x04
#define WAIT_ACCEPT  0x08
#define WAIT_CLOSE	 0x10
#define WAIT_EVENTCOUNT 5

namespace fz {

namespace {
// Union for strict aliasing-safe casting between
// the different address types
union sockaddr_u
{
	sockaddr_storage storage;
	sockaddr sockaddr_;
	sockaddr_in in4;
	sockaddr_in6 in6;
};
}

void remove_socket_events(event_handler * handler, socket_event_source const* const source)
{
	auto socket_event_filter = [&](event_loop::Events::value_type const& ev) -> bool {
		if (ev.first != handler) {
			return false;
		}
		else if (ev.second->derived_type() == socket_event::type()) {
			return std::get<0>(static_cast<socket_event const&>(*ev.second).v_) == source;
		}
		else if (ev.second->derived_type() == hostaddress_event::type()) {
			return std::get<0>(static_cast<hostaddress_event const&>(*ev.second).v_) == source;
		}
		return false;
	};

	handler->event_loop_.filter_events(socket_event_filter);
}

void change_socket_event_handler(event_handler * old_handler, event_handler * new_handler, socket_event_source const* const source)
{
	if (!old_handler) {
		return;
	}

	if (old_handler == new_handler) {
		return;
	}

	if (!new_handler) {
		remove_socket_events(old_handler, source);
	}
	else {
		auto socket_event_filter = [&](event_loop::Events::value_type & ev) -> bool {
			if (ev.first == old_handler) {
				if (ev.second->derived_type() == socket_event::type()) {
					if (std::get<0>(static_cast<socket_event const&>(*ev.second).v_) == source) {
						ev.first = new_handler;
					}
				}
				else if (ev.second->derived_type() == hostaddress_event::type()) {
					if (std::get<0>(static_cast<hostaddress_event const&>(*ev.second).v_) == source) {
						ev.first = new_handler;
					}
				}
			}
			return false;
		};

		old_handler->event_loop_.filter_events(socket_event_filter);
	}
}

namespace {
bool has_pending_event(event_handler * handler, socket_event_source const* const source, socket_event_flag event)
{
	bool ret = false;

	auto socket_event_filter = [&](event_loop::Events::value_type const& ev) -> bool {
		if (ev.first == handler && ev.second->derived_type() == socket_event::type()) {
			auto const& socket_ev = static_cast<socket_event const&>(*ev.second).v_;
			if (std::get<0>(socket_ev) == source && std::get<1>(socket_ev) == event) {
				ret = true;
			}
		}
		return false;
	};

	handler->event_loop_.filter_events(socket_event_filter);

	return ret;
}

#ifdef FZ_WINDOWS
static int convert_msw_error_code(int error)
{
	// Takes an MSW socket error and converts it into an equivalent POSIX error code.
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

int last_socket_error()
{
	return convert_msw_error_code(WSAGetLastError());
}
#else
inline int last_socket_error() { return errno; }
#endif
}

class socket_thread final
{
	friend class socket;
public:
	socket_thread()
		: mutex_(false)
	{
#ifdef FZ_WINDOWS
		sync_event_ = WSA_INVALID_EVENT;
#else
		pipe_[0] = -1;
		pipe_[1] = -1;
#endif
		for (int i = 0; i < WAIT_EVENTCOUNT; ++i) {
			triggered_errors_[i] = 0;
		}
	}

	~socket_thread()
	{
		thread_.join();
#ifdef FZ_WINDOWS
		if (sync_event_ != WSA_INVALID_EVENT) {
			WSACloseEvent(sync_event_);
		}
#else
		if (pipe_[0] != -1) {
			close(pipe_[0]);
		}
		if (pipe_[1] != -1) {
			close(pipe_[1]);
		}
#endif
	}

	void set_socket(socket* pSocket)
	{
		scoped_lock l(mutex_);
		set_socket(pSocket, l);
	}

	void set_socket(socket* pSocket, scoped_lock const&)
	{
		socket_ = pSocket;

		host_.clear();
		port_.clear();

		waiting_ = 0;
	}

	int connect(std::string const& bind)
	{
		assert(socket_);
		if (!socket_) {
			return EINVAL;
		}

		host_ = to_utf8(socket_->host_);
		if (host_.empty()) {
			return EINVAL;
		}

		bind_ = bind;

		// Connect method of socket ensures port is in range
		port_ = sprintf("%u", socket_->port_);

		start();

		return 0;
	}

	int start()
	{
		if (started_) {
			scoped_lock l(mutex_);
			assert(threadwait_);
			waiting_ = 0;
			wakeup_thread(l);
			return 0;
		}
		started_ = true;
#ifdef FZ_WINDOWS
		if (sync_event_ == WSA_INVALID_EVENT) {
			sync_event_ = WSACreateEvent();
		}
		if (sync_event_ == WSA_INVALID_EVENT) {
			return 1;
		}
#else
		if (pipe_[0] == -1) {
			if (pipe(pipe_)) {
				return errno;
			}
		}
#endif

		thread_ = socket_->thread_pool_.spawn([this]() { entry(); });

		return thread_ ? 0 : 1;
	}

	// Cancels select or idle wait
	void wakeup_thread()
	{
		scoped_lock l(mutex_);
		wakeup_thread(l);
	}

	void wakeup_thread(scoped_lock & l)
	{
		if (!started_ || finished_) {
			return;
		}

		if (threadwait_) {
			threadwait_ = false;
			condition_.signal(l);
			return;
		}

#ifdef FZ_WINDOWS
		WSASetEvent(sync_event_);
#else
		char tmp = 0;

		int ret;
		do {
			ret = write(pipe_[1], &tmp, 1);
		} while (ret == -1 && errno == EINTR);
#endif
	}

protected:
	static int create_socket_fd(addrinfo const& addr)
	{
		int fd;
#if defined(SOCK_CLOEXEC) && !defined(FZ_WINDOWS)
		fd = ::socket(addr.ai_family, addr.ai_socktype | SOCK_CLOEXEC, addr.ai_protocol);
		if (fd == -1 && errno == EINVAL)
#endif
		{
			fd = ::socket(addr.ai_family, addr.ai_socktype, addr.ai_protocol);
		}

		if (fd != -1) {
#if defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
			// We do not want SIGPIPE if writing to socket.
			const int value = 1;
			setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(int));
#endif
			socket::set_nonblocking(fd);
		}

		return fd;
	}

	static void close_socket_fd(int& fd)
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

	int try_connect_host(addrinfo & addr, sockaddr_u const& bindAddr, scoped_lock & l)
	{
		if (socket_->evt_handler_) {
			socket_->evt_handler_->send_event<hostaddress_event>(socket_, socket::address_to_string(addr.ai_addr, addr.ai_addrlen));
		}

		int fd = create_socket_fd(addr);
		if (fd == -1) {
			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_, addr.ai_next ? socket_event_flag::connection_next : socket_event_flag::connection, last_socket_error());
			}

			return 0;
		}

		if (bindAddr.sockaddr_.sa_family != AF_UNSPEC && bindAddr.sockaddr_.sa_family == addr.ai_family) {
			(void)bind(fd, &bindAddr.sockaddr_, sizeof(bindAddr));
		}

		socket::do_set_flags(fd, socket_->flags_, socket_->flags_, socket_->keepalive_interval_);
		socket::do_set_buffer_sizes(fd, socket_->buffer_sizes_[0], socket_->buffer_sizes_[1]);

		int res = ::connect(fd, addr.ai_addr, addr.ai_addrlen);
		if (res == -1) {
#ifdef FZ_WINDOWS
			// Map to POSIX error codes
			int error = WSAGetLastError();
			if (error == WSAEWOULDBLOCK) {
				res = EINPROGRESS;
			}
			else {
				res = last_socket_error();
			}
#else
			res = errno;
#endif
		}

		if (res == EINPROGRESS) {

			socket_->fd_ = fd;

			bool wait_successful;
			do {
				wait_successful = do_wait(WAIT_CONNECT, l);
				if ((triggered_ & WAIT_CONNECT)) {
					break;
				}
			} while (wait_successful);

			if (!wait_successful) {
				close_socket_fd(fd);
				if (socket_) {
					socket_->fd_ = -1;
				}
				return -1;
			}
			triggered_ &= ~WAIT_CONNECT;

			res = triggered_errors_[0];
		}

		if (res) {
			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_, addr.ai_next ? socket_event_flag::connection_next : socket_event_flag::connection, res);
			}

			close_socket_fd(fd);
			socket_->fd_ = -1;
		}
		else {
			socket_->fd_ = fd;
			socket_->state_ = socket::connected;

			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::connection, 0);
			}

			// We're now interested in all the other nice events
			waiting_ |= WAIT_READ | WAIT_WRITE;

			return 1;
		}

		return 0;
	}

	// Only call while locked
	bool do_connect(scoped_lock & l)
	{
		if (host_.empty() || port_.empty()) {
			socket_->state_ = socket::closed;
			return false;
		}

		std::string host, port, bind;
		std::swap(host, host_);
		std::swap(port, port_);
		std::swap(bind, bind_);

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
		hints.ai_family = socket_->family_;

		l.unlock();

		hints.ai_socktype = SOCK_STREAM;
#ifdef AI_IDN
		hints.ai_flags |= AI_IDN;
#endif

		addrinfo *addressList{};
		int res = getaddrinfo(host.c_str(), port.c_str(), &hints, &addressList);

		l.lock();

		if (should_quit()) {
			if (!res && addressList) {
				freeaddrinfo(addressList);
			}
			if (socket_) {
				socket_->state_ = socket::closed;
			}
			return false;
		}

		// If state isn't connecting, close() was called.
		// If host_ is set, close() was called and connect()
		// afterwards, state is back at connecting.
		// In either case, we need to abort this connection attempt.
		if (socket_->state_ != socket::connecting || !host_.empty()) {
			if (!res && addressList) {
				freeaddrinfo(addressList);
			}
			return false;
		}

		if (res) {
#ifdef FZ_WINDOWS
			res = convert_msw_error_code(res);
#endif

			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::connection, res);
			}
			socket_->state_ = socket::closed;

			return false;
		}

		for (addrinfo *addr = addressList; addr; addr = addr->ai_next) {
			res = try_connect_host(*addr, bindAddr, l);
			if (res == -1) {
				freeaddrinfo(addressList);
				if (socket_) {
					socket_->state_ = socket::closed;
				}
				return false;
			}
			else if (res) {
				freeaddrinfo(addressList);
				return true;
			}
		}
		freeaddrinfo(addressList);

		if (socket_->evt_handler_) {
			socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::connection, ECONNABORTED);
		}
		socket_->state_ = socket::closed;

		return false;
	}

	bool should_quit() const
	{
		return quit_ || !socket_;
	}

	// Call only while locked
	bool do_wait(int wait, scoped_lock & l)
	{
		waiting_ |= wait;

		for (;;) {
#ifdef FZ_WINDOWS
			int wait_events = FD_CLOSE;
			if (waiting_ & WAIT_CONNECT) {
				wait_events |= FD_CONNECT;
			}
			if (waiting_ & WAIT_READ) {
				wait_events |= FD_READ;
			}
			if (waiting_ & WAIT_WRITE) {
				wait_events |= FD_WRITE;
			}
			if (waiting_ & WAIT_ACCEPT) {
				wait_events |= FD_ACCEPT;
			}
			if (waiting_ & WAIT_CLOSE) {
				wait_events |= FD_CLOSE;
			}
			WSAEventSelect(socket_->fd_, sync_event_, wait_events);
			l.unlock();
			WSAWaitForMultipleEvents(1, &sync_event_, false, WSA_INFINITE, false);

			l.lock();
			if (should_quit()) {
				return false;
			}

			WSANETWORKEVENTS events;
			int res = WSAEnumNetworkEvents(socket_->fd_, sync_event_, &events);
			if (res) {
				res = last_socket_error();
				return false;
			}

			if (waiting_ & WAIT_CONNECT) {
				if (events.lNetworkEvents & FD_CONNECT) {
					triggered_ |= WAIT_CONNECT;
					triggered_errors_[0] = convert_msw_error_code(events.iErrorCode[FD_CONNECT_BIT]);
					waiting_ &= ~WAIT_CONNECT;
				}
			}
			if (waiting_ & WAIT_READ) {
				if (events.lNetworkEvents & FD_READ) {
					triggered_ |= WAIT_READ;
					triggered_errors_[1] = convert_msw_error_code(events.iErrorCode[FD_READ_BIT]);
					waiting_ &= ~WAIT_READ;
				}
			}
			if (waiting_ & WAIT_WRITE) {
				if (events.lNetworkEvents & FD_WRITE) {
					triggered_ |= WAIT_WRITE;
					triggered_errors_[2] = convert_msw_error_code(events.iErrorCode[FD_WRITE_BIT]);
					waiting_ &= ~WAIT_WRITE;
				}
			}
			if (waiting_ & WAIT_ACCEPT) {
				if (events.lNetworkEvents & FD_ACCEPT) {
					triggered_ |= WAIT_ACCEPT;
					triggered_errors_[3] = convert_msw_error_code(events.iErrorCode[FD_ACCEPT_BIT]);
					waiting_ &= ~WAIT_ACCEPT;
				}
			}
			if (waiting_ & WAIT_CLOSE) {
				if (events.lNetworkEvents & FD_CLOSE) {
					triggered_ |= WAIT_CLOSE;
					triggered_errors_[4] = convert_msw_error_code(events.iErrorCode[FD_CLOSE_BIT]);
					waiting_ &= ~WAIT_CLOSE;
				}
			}

			if (triggered_ || !waiting_) {
				return true;
			}
#else
			fd_set readfds;
			fd_set writefds;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);

			FD_SET(pipe_[0], &readfds);
			if (!(waiting_ & WAIT_CONNECT)) {
				FD_SET(socket_->fd_, &readfds);
			}

			if (waiting_ & (WAIT_WRITE | WAIT_CONNECT)) {
				FD_SET(socket_->fd_, &writefds);
			}

			int maxfd = std::max(pipe_[0], socket_->fd_) + 1;

			l.unlock();

			int res = select(maxfd, &readfds, &writefds, 0, 0);

			l.lock();

			if (res > 0 && FD_ISSET(pipe_[0], &readfds)) {
				char buffer[100];
				int damn_spurious_warning = read(pipe_[0], buffer, 100);
				(void)damn_spurious_warning; // We do not care about return value and this is definitely correct!
			}

			if (quit_ || !socket_ || socket_->fd_ == -1) {
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

			if (waiting_ & WAIT_CONNECT) {
				if (FD_ISSET(socket_->fd_, &writefds)) {
					int error;
					socklen_t len = sizeof(error);
					int getsockopt_res = getsockopt(socket_->fd_, SOL_SOCKET, SO_ERROR, &error, &len);
					if (getsockopt_res) {
						error = errno;
					}
					triggered_ |= WAIT_CONNECT;
					triggered_errors_[0] = error;
					waiting_ &= ~WAIT_CONNECT;
				}
			}
			else if (waiting_ & WAIT_ACCEPT) {
				if (FD_ISSET(socket_->fd_, &readfds)) {
					triggered_ |= WAIT_ACCEPT;
					waiting_ &= ~WAIT_ACCEPT;
				}
			}
			else if (waiting_ & WAIT_READ) {
				if (FD_ISSET(socket_->fd_, &readfds)) {
					triggered_ |= WAIT_READ;
					waiting_ &= ~WAIT_READ;
				}
			}
			if (waiting_ & WAIT_WRITE) {
				if (FD_ISSET(socket_->fd_, &writefds)) {
					triggered_ |= WAIT_WRITE;
					waiting_ &= ~WAIT_WRITE;
				}
			}

			if (triggered_ || !waiting_) {
				return true;
			}
#endif
		}
	}

	void send_events()
	{
		if (!socket_ || !socket_->evt_handler_) {
			return;
		}
		if (triggered_ & WAIT_READ) {
			socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::read, triggered_errors_[1]);
			triggered_ &= ~WAIT_READ;
		}
		if (triggered_ & WAIT_WRITE) {
			socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::write, triggered_errors_[2]);
			triggered_ &= ~WAIT_WRITE;
		}
		if (triggered_ & WAIT_ACCEPT) {
			socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::connection, triggered_errors_[3]);
			triggered_ &= ~WAIT_ACCEPT;
		}
		if (triggered_ & WAIT_CLOSE) {
			send_close_event();
		}
	}

	void send_close_event()
	{
		if (!socket_ || !socket_->evt_handler_) {
			return;
		}

#ifdef FZ_WINDOWS
		// MSDN says this:
		//   FD_CLOSE being posted after all data is read from a socket.
		//   An application should check for remaining data upon receipt
		//   of FD_CLOSE to avoid any possibility of losing data.
		// First half is actually plain wrong.
		char buf;
		if (!triggered_errors_[4] && recv( socket_->fd_, &buf, 1, MSG_PEEK ) > 0) {
			if (!(waiting_ & WAIT_READ)) {
				return;
			}
			socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::read, 0);
		}
		else
#endif
		{
			socket_->evt_handler_->send_event<socket_event>(socket_, socket_event_flag::close, triggered_errors_[4]);
			triggered_ &= ~WAIT_CLOSE;
		}
	}

	// Call only while locked
	bool idle_loop(scoped_lock & l)
	{
		if (quit_) {
			return false;
		}
		while (!socket_ || (!waiting_ && host_.empty())) {
			threadwait_ = true;
			condition_.wait(l);

			if (quit_) {
				return false;
			}
		}

		return true;
	}

	void entry()
	{
		scoped_lock l(mutex_);
		for (;;) {
			if (!idle_loop(l)) {
				break;
			}

			if (socket_->state_ == socket::listening) {
				while (idle_loop(l)) {
					if (socket_->fd_ == -1) {
						waiting_ = 0;
						break;
					}
					if (!do_wait(0, l)) {
						break;
					}
					send_events();
				}
			}
			else {
				if (socket_->state_ == socket::connecting) {
					if (!do_connect(l)) {
						continue;
					}
				}

#ifdef FZ_WINDOWS
				waiting_ |= WAIT_CLOSE;
				int wait_close = WAIT_CLOSE;
#endif
				while (idle_loop(l)) {
					if (socket_->fd_ == -1) {
						waiting_ = 0;
						break;
					}
					bool res = do_wait(0, l);

					if (triggered_ & WAIT_CLOSE && socket_) {
						socket_->state_ = socket::closing;
#ifdef FZ_WINDOWS
						wait_close = 0;
#endif
					}

					if (!res) {
						break;
					}

					send_events();
#ifdef FZ_WINDOWS
					waiting_ |= wait_close;
#endif
				}
			}
		}

		if (thread_) {
			finished_ = true;
		}
		else {
			l.unlock();
			delete this;
		}
		return;
	}

	socket* socket_{};

	std::string host_;
	std::string port_;
	std::string bind_;

#ifdef FZ_WINDOWS
	// We wait on this using WSAWaitForMultipleEvents
	WSAEVENT sync_event_;
#else
	// A pipe is used to unblock select
	int pipe_[2];
#endif

	mutex mutex_;
	condition condition_;

	bool started_{};
	bool quit_{};
	bool finished_{};

	// The socket events we are waiting for
	int waiting_{};

	// The triggered socket events
	int triggered_{};
	int triggered_errors_[WAIT_EVENTCOUNT];

	// Thread waits for instructions
	bool threadwait_{};

	async_task thread_;
};

socket::socket(thread_pool & pool, event_handler* evt_handler)
	: thread_pool_(pool)
	, evt_handler_(evt_handler)
	, keepalive_interval_(duration::from_hours(2))
{
	family_ = AF_UNSPEC;

	buffer_sizes_[0] = -1;
	buffer_sizes_[1] = -1;
}

socket::~socket()
{
	if (state_ != none) {
		close();
	}

	if (socket_thread_) {
		scoped_lock l(socket_thread_->mutex_);
		detach_thread(l);
	}
}

void socket::detach_thread(scoped_lock & l)
{
	if (!socket_thread_) {
		return;
	}

	socket_thread_->set_socket(0, l);
	if (socket_thread_->finished_) {
		socket_thread_->wakeup_thread(l);
		l.unlock();
		delete socket_thread_;
		socket_thread_ = 0;
	}
	else {
		if (!socket_thread_->started_) {
			auto thread = socket_thread_;
			socket_thread_ = 0;
			l.unlock();
			delete thread;
		}
		else {
			socket_thread_->quit_ = true;
			socket_thread_->thread_.detach();
			socket_thread_->wakeup_thread(l);
			socket_thread_ = 0;
		}
	}
}

int socket::connect(native_string const& host, unsigned int port, address_type family, std::string const& bind)
{
	if (state_ != none) {
		return EISCONN;
	}

	if (port < 1 || port > 65535) {
		return EINVAL;
	}

	if (host.empty()) {
		return EINVAL;
	}

	int af{};

	switch (family)
	{
	case address_type::unknown:
		af = AF_UNSPEC;
		break;
	case address_type::ipv4:
		af = AF_INET;
		break;
	case address_type::ipv6:
		af = AF_INET6;
		break;
	default:
		return EINVAL;
	}

	if (socket_thread_ && socket_thread_->started_) {
		scoped_lock l(socket_thread_->mutex_);
		if (!socket_thread_->threadwait_) {
			// Possibly inside a blocking call, e.g. getaddrinfo.
			// Detach the thread so that we can continue.
			detach_thread(l);
		}
	}
	if (!socket_thread_) {
		socket_thread_ = new socket_thread();
		socket_thread_->set_socket(this);
	}

	family_ = af;
	state_ = connecting;

	host_ = host;
	port_ = port;
	int res = socket_thread_->connect(bind);
	if (res) {
		state_ = none;
		delete socket_thread_;
		socket_thread_ = 0;
		return res;
	}

	return EINPROGRESS;
}

void socket::set_event_handler(event_handler* pEvtHandler)
{
	if (socket_thread_) {
		scoped_lock l(socket_thread_->mutex_);

		if (evt_handler_ == pEvtHandler) {
			return;
		}

		change_socket_event_handler(evt_handler_, pEvtHandler, this);

		evt_handler_ = pEvtHandler;

		if (pEvtHandler && state_ == connected) {
#ifdef FZ_WINDOWS
			// If a graceful shutdown is going on in background already,
			// no further events are recorded. Send out events we're not
			// waiting for (i.e. they got triggered already) manually.

			if (!(socket_thread_->waiting_ & WAIT_WRITE)) {
				pEvtHandler->send_event<socket_event>(this, socket_event_flag::write, 0);
			}

			pEvtHandler->send_event<socket_event>(this, socket_event_flag::read, 0);
			if (socket_thread_->waiting_ & WAIT_READ) {
				socket_thread_->waiting_ &= ~WAIT_READ;
				socket_thread_->wakeup_thread(l);
			}
#else
			socket_thread_->waiting_ |= WAIT_READ | WAIT_WRITE;
			socket_thread_->wakeup_thread(l);
#endif
		}
		else if (pEvtHandler && state_ == closing) {
			if (!(socket_thread_->triggered_ & WAIT_READ)) {
				socket_thread_->waiting_ |= WAIT_READ;
			}
			socket_thread_->send_events();
		}
	}
	else {
		change_socket_event_handler(evt_handler_, pEvtHandler, this);
		evt_handler_ = pEvtHandler;
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

std::string socket::error_string(int error)
{
	for (int i = 0; error_table[i].code; ++i) {
		if (error != error_table[i].code) {
			continue;
		}

		return error_table[i].name;
	}

	return sprintf("%d", error);
}

native_string socket::error_description(int error)
{
	for (int i = 0; error_table[i].code; ++i) {
		if (error != error_table[i].code) {
			continue;
		}

		return to_native(to_native(std::string(error_table[i].name)) + fzT(" - ") + to_native(translate(error_table[i].description)));
	}

	return sprintf(fzT("%d"), error);
}

int socket::close()
{
	if (socket_thread_) {
		scoped_lock l(socket_thread_->mutex_);
		int fd = fd_;
		fd_ = -1;

		socket_thread_->host_.clear();
		socket_thread_->port_.clear();

		socket_thread_->wakeup_thread(l);

		socket_thread::close_socket_fd(fd);
		state_ = none;

		socket_thread_->triggered_ = 0;
		for (int i = 0; i < WAIT_EVENTCOUNT; ++i) {
			socket_thread_->triggered_errors_[i] = 0;
		}

		if (evt_handler_) {
			remove_socket_events(evt_handler_, this);
			evt_handler_ = 0;
		}
	}
	else {
		int fd = fd_;
		fd_ = -1;
		socket_thread::close_socket_fd(fd);
		state_ = none;

		if (evt_handler_) {
			remove_socket_events(evt_handler_, this);
			evt_handler_ = 0;
		}
	}

	return 0;
}

socket::socket_state socket::get_state()
{
	socket_state state;
	if (socket_thread_) {
		socket_thread_->mutex_.lock();
	}
	state = state_;
	if (socket_thread_) {
		socket_thread_->mutex_.unlock();
	}

	return state;
}

int socket::read(void* buffer, unsigned int size, int& error)
{
	int res = recv(fd_, (char*)buffer, size, 0);

	if (res == -1) {
		error = last_socket_error();
		if (error == EAGAIN) {
			if (socket_thread_) {
				scoped_lock l(socket_thread_->mutex_);
				if (!(socket_thread_->waiting_ & WAIT_READ)) {
					socket_thread_->waiting_ |= WAIT_READ;
					socket_thread_->wakeup_thread(l);
				}
			}
		}
	}
	else {
		error = 0;
	}

	return res;
}

int socket::peek(void* buffer, unsigned int size, int& error)
{
	int res = recv(fd_, (char*)buffer, size, MSG_PEEK);

	if (res == -1) {
		error = last_socket_error();
	}
	else {
		error = 0;
	}

	return res;
}

int socket::write(const void* buffer, unsigned int size, int& error)
{
#ifdef MSG_NOSIGNAL
	const int flags = MSG_NOSIGNAL;
#else
	const int flags = 0;

#if !defined(SO_NOSIGPIPE) && !defined(FZ_WINDOWS)
	// Some systems have neither. Need to block signal
	sigaction old_action;
	sigaction action = {};
	action.sa_handler = SIG_IGN;
	int signal_set = sigaction(SIGPIPE, &action, &old_action);
#endif

#endif

	int res = send(fd_, (const char*)buffer, size, flags);

#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE) && !defined(FZ_WINDOWS)
	// Restore previous signal handler
	if (!signal_set)
		sigaction(SIGPIPE, &old_action, 0);
#endif

	if (res == -1) {
		error = last_socket_error();
		if (error == EAGAIN) {
			if (socket_thread_) {
				scoped_lock l (socket_thread_->mutex_);
				if (!(socket_thread_->waiting_ & WAIT_WRITE)) {
					socket_thread_->waiting_ |= WAIT_WRITE;
					socket_thread_->wakeup_thread(l);
				}
			}
		}
	}
	else
		error = 0;

	return res;
}

std::string socket::address_to_string(sockaddr const* addr, int addr_len, bool with_port, bool strip_zone_index)
{
	char hostbuf[NI_MAXHOST];
	char portbuf[NI_MAXSERV];

	int res = getnameinfo(addr, addr_len, hostbuf, NI_MAXHOST, portbuf, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
	if (res) { // Should never fail
		return std::string();
	}

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
		if (with_port) {
			host = "[" + host + "]";
		}
	}

	if (with_port) {
		return host + ":" + port;
	}
	else {
		return host;
	}
}

std::string socket::address_to_string(char const* buf, int buf_len)
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

	return address_to_string(&addr.sockaddr_, sizeof(addr), false, true);
}

std::string socket::local_ip(bool strip_zone_index) const
{
	sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int res = getsockname(fd_, (sockaddr*)&addr, &addr_len);
	if (res) {
		return std::string();
	}

	return address_to_string((sockaddr *)&addr, addr_len, false, strip_zone_index);
}

std::string socket::peer_ip(bool strip_zone_index) const
{
	sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int res = getpeername(fd_, (sockaddr*)&addr, &addr_len);
	if (res) {
		return std::string();
	}

	return address_to_string((sockaddr *)&addr, addr_len, false, strip_zone_index);
}

address_type socket::address_family() const
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	int res = getsockname(fd_, &addr.sockaddr_, &addr_len);
	if (res) {
		return address_type::unknown;
	}

	switch (addr.sockaddr_.sa_family)
	{
	case AF_INET:
		return address_type::ipv4;
	case AF_INET6:
		return address_type::ipv6;
	default:
		return address_type::unknown;
	}
}

int socket::listen(address_type family, int port)
{
	if (state_ != none) {
		return EALREADY;
	}

	if (port < 0 || port > 65535) {
		return EINVAL;
	}

	switch (family)
	{
	case address_type::unknown:
		family_ = AF_UNSPEC;
		break;
	case address_type::ipv4:
		family_ = AF_INET;
		break;
	case address_type::ipv6:
		family_ = AF_INET6;
		break;
	default:
		return EINVAL;
	}

	{
		addrinfo hints = {};
		hints.ai_family = family_;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
#ifdef AI_NUMERICSERV
		// Some systems like Windows or OS X don't know AI_NUMERICSERV.
		hints.ai_flags |= AI_NUMERICSERV;
#endif

		std::string portstring = sprintf("%d", port);

		addrinfo* addressList = 0;
		int res = getaddrinfo(0, portstring.c_str(), &hints, &addressList);

		if (res) {
#ifdef FZ_WINDOWS
			return convert_msw_error_code(res);
#else
			return res;
#endif
		}

		for (addrinfo* addr = addressList; addr; addr = addr->ai_next) {
			fd_ = socket_thread::create_socket_fd(*addr);
			res = last_socket_error();

			if (fd_ == -1) {
				continue;
			}

			res = bind(fd_, addr->ai_addr, addr->ai_addrlen);
			if (!res) {
				break;
			}

			res = last_socket_error();
			socket_thread::close_socket_fd(fd_);
		}
		freeaddrinfo(addressList);
		if (fd_ == -1) {
			return res;
		}
	}

	int res = ::listen(fd_, 1);
	if (res) {
		res = last_socket_error();
		socket_thread::close_socket_fd(fd_);
		fd_ = -1;
		return res;
	}

	state_ = listening;

	socket_thread_ = new socket_thread();
	socket_thread_->set_socket(this);

	socket_thread_->waiting_ = WAIT_ACCEPT;

	socket_thread_->start();

	return 0;
}

int socket::local_port(int& error)
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	error = getsockname(fd_, &addr.sockaddr_, &addr_len);
	if (error) {
#ifdef FZ_WINDOWS
		error = convert_msw_error_code(error);
#endif
		return -1;
	}

	if (addr.storage.ss_family == AF_INET) {
		return ntohs(addr.in4.sin_port);
	}
	else if (addr.storage.ss_family == AF_INET6) {
		return ntohs(addr.in6.sin6_port);
	}

	error = EINVAL;
	return -1;
}

int socket::remote_port(int& error)
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	error = getpeername(fd_, &addr.sockaddr_, &addr_len);
	if (error) {
#ifdef FZ_WINDOWS
		error = convert_msw_error_code(error);
#endif
		return -1;
	}

	if (addr.storage.ss_family == AF_INET) {
		return ntohs(addr.in4.sin_port);
	}
	else if (addr.storage.ss_family == AF_INET6) {
		return ntohs(addr.in6.sin6_port);
	}

	error = EINVAL;
	return -1;
}

socket* socket::accept(int &error)
{
	if (socket_thread_) {
		scoped_lock l(socket_thread_->mutex_);
		socket_thread_->waiting_ |= WAIT_ACCEPT;
		socket_thread_->wakeup_thread(l);
	}
	int fd = ::accept(fd_, 0, 0);
	if (fd == -1) {
		error = last_socket_error();
		return 0;
	}

#if defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
	// We do not want SIGPIPE if writing to socket.
	const int value = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(int));
#endif

	set_nonblocking(fd);

	do_set_buffer_sizes(fd, buffer_sizes_[0], buffer_sizes_[1]);

	socket* pSocket = new socket(thread_pool_, 0);
	pSocket->state_ = connected;
	pSocket->fd_ = fd;
	pSocket->socket_thread_ = new socket_thread();
	pSocket->socket_thread_->set_socket(pSocket);
	pSocket->socket_thread_->waiting_ = WAIT_READ | WAIT_WRITE;
	pSocket->socket_thread_->start();

	return pSocket;
}

int socket::set_nonblocking(int fd)
{
	// Set socket to non-blocking.
#ifdef FZ_WINDOWS
	unsigned long nonblock = 1;
	int res = ioctlsocket(fd, FIONBIO, &nonblock);
	if (!res) {
		return 0;
	}
	else {
		return convert_msw_error_code(WSAGetLastError());
	}
#else
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1) {
		return errno;
	}
	int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (res == -1) {
		return errno;
	}
	return 0;
#endif
}

void socket::set_flags(int flags)
{
	if (socket_thread_) {
		socket_thread_->mutex_.lock();
	}

	if (fd_ != -1) {
		do_set_flags(fd_, flags, flags ^ flags_, keepalive_interval_);
	}
	flags_ = flags;

	if (socket_thread_) {
		socket_thread_->mutex_.unlock();
	}
}

int socket::do_set_flags(int fd, int flags, int flags_mask, duration const& keepalive_interval)
{
	if (flags_mask & flag_nodelay) {
		const int value = (flags & flag_nodelay) ? 1 : 0;
		int res = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value));
		if (res != 0) {
			return last_socket_error();
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
			return last_socket_error();
		}
#else
		const int value = (flags & flag_keepalive) ? 1 : 0;
		int res = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&value, sizeof(value));
		if (res != 0) {
			return last_socket_error();
		}
#ifdef TCP_KEEPIDLE
		int const idle = keepalive_interval.get_seconds();
		res = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&idle, sizeof(idle));
		if (res != 0) {
			return last_socket_error();
		}
#endif
#endif
	}

	return 0;
}

int socket::set_buffer_sizes(int size_receive, int size_send)
{
	int ret = 0;

	if (socket_thread_) {
		socket_thread_->mutex_.lock();
	}

	buffer_sizes_[0] = size_receive;
	buffer_sizes_[1] = size_send;

	if (fd_ != -1) {
		ret = do_set_buffer_sizes(fd_, size_receive, size_send);
	}

	if (socket_thread_) {
		socket_thread_->mutex_.unlock();
	}

	return ret;
}

int socket::ideal_send_buffer_size()
{
	int size = -1;

#ifdef FZ_WINDOWS
	if (socket_thread_) {
		socket_thread_->mutex_.lock();
	}

	if (fd_ != -1) {
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
		if (!WSAIoctl(fd_, SIO_IDEAL_SEND_BACKLOG_QUERY, 0, 0, &v, sizeof(v), &outlen, 0, 0)) {
			size = v;
		}
	}

	if (socket_thread_) {
		socket_thread_->mutex_.unlock();
	}
#endif

	return size;
}


int socket::do_set_buffer_sizes(int fd, int size_read, int size_write)
{
	int ret = 0;
	if (size_read != -1) {
		int res = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&size_read, sizeof(size_read));
		if (res != 0) {
			ret = last_socket_error();
		}
	}

	if (size_write != -1) {
		int res = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&size_write, sizeof(size_write));
		if (res != 0) {
			ret = last_socket_error();
		}
	}

	return ret;
}

native_string socket::peer_host() const
{
	return host_;
}

void socket::set_keepalive_interval(duration const& d)
{
	if (d < duration::from_minutes(1)) {
		return;
	}

	if (socket_thread_) {
		socket_thread_->mutex_.lock();
	}

	keepalive_interval_ = d;
	if (fd_ != -1) {
		do_set_flags(fd_, flags_, flag_keepalive, keepalive_interval_);
	}

	if (socket_thread_) {
		socket_thread_->mutex_.unlock();
	}
}

void socket::retrigger(socket_event_flag event)
{
	if (!socket_thread_) {
		return;
	}

	if (event != socket_event_flag::read && event != socket_event_flag::write) {
		return;
	}
	
	fz::scoped_lock l(socket_thread_->mutex_);

	if (has_pending_event(evt_handler_, this, event)) {
		return;
	}

	int const wait_flag = (event == socket_event_flag::read) ? WAIT_READ : WAIT_WRITE;
	if (!(socket_thread_->waiting_ & wait_flag)) {
		socket_thread_->waiting_ |= wait_flag;
		socket_thread_->wakeup_thread(l);
	}
}

}
