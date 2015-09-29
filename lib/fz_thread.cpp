#include "fz_thread.hpp"
#include "fz_mutex.hpp"

#include <thread>

#if defined(FZ_WINDOWS) && (defined(__MINGW32__) || defined(__MINGW64__))
#define USE_CUSTOM_THREADS 1
#include <process.h>
#endif

namespace fz {

thread::thread()
{
}

bool thread::joinable() const
{
	return impl_ == 0;
}

#ifdef USE_CUSTOM_THREADS
// MinGW is a special snowflake...

namespace {
class entry_dispatch
{
public:
	virtual ~entry_dispatch() {}

	static void call_entry(entry_dispatch & e)
	{
		e.entry();
	}

	virtual void entry() = 0;
};

static unsigned __stdcall thread_proc(void* arg)
{
	entry_dispatch* e = static_cast<entry_dispatch*>(arg);
	if (e) {
		entry_dispatch::call_entry(*e);
	}
	return 0;
}
}

class thread::impl final : entry_dispatch
{
public:
	impl(thread& t);

	thread& t_;

	HANDLE handle_{INVALID_HANDLE_VALUE};

	mutex m_{false};

	virtual void entry();
};

thread::impl::impl(thread& t)
	: t_(t)
{
}

void thread::impl::entry()
{
	{
		// Obtain mutex once. Once we have it, handle_ is assigned.
		scoped_lock l(m_);
	}
	t_.entry();
}

bool thread::run()
{
	if (impl_) {
		return false;
	}

	impl_ = new impl(*this);

	{
		scoped_lock l(impl_->m_);
		impl_->handle_ = reinterpret_cast<HANDLE>(_beginthreadex(0, 0, thread_proc, impl_, 0, 0));
	}

	if (impl_->handle_ == INVALID_HANDLE_VALUE) {
		delete impl_;
		impl_ = 0;
	}

	return impl_ != 0;
}

void thread::join()
{
	if (impl_) {
		WaitForSingleObject(impl_->handle_, INFINITE);
		CloseHandle(impl_->handle_);
		delete impl_;
		impl_ = 0;
	}
}

#else

// Canonical implentation for everyone else
class thread::impl final
{
public:
	std::thread t_;

	mutex m_{false};

	static void entry(thread & t);
};


void thread::impl::entry(thread& t)
{
	if (!t.impl_) {
		return;
	}

	{
		// Obtain mutex once. Once we have it, t.impl_->t_ is assigned.
		scoped_lock l(t.impl_->m_);
	}

	t.entry();
}

bool thread::run()
{
	if (impl_) {
		return false;
	}
	
	try {
		impl_ = new impl;
		
		scoped_lock l(impl_->m_);
		std::thread t(impl::entry, std::ref(*this));
		impl_->t_ = std::move(t);
	}
	catch (std::exception const&) {
		delete impl_;
		impl_ = 0;
	}

	return impl_ != 0;
}

void thread::join()
{
	if (impl_) {
		impl_->t_.join();
		delete impl_;
		impl_ = 0;
	}
}

#endif

thread::~thread()
{
	if (!joinable()) {
		std::abort();
	}
	delete impl_;
}

}
