#include "fz_thread.hpp"
#include "fz_mutex.hpp"

#include <thread>

#if defined(FZ_WINDOWS) && (defined(__MINGW32__) || defined(__MINGW64__))
#define USE_CUSTOM_THREADS 1
#include <process.h>
#endif

namespace fz {

class thread::impl final
{
public:
	std::thread t_;

	mutex m_{false};

	static void run(thread & t);
};


void thread::impl::run(thread& t)
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

thread::thread()
{
}

thread::~thread()
{
	if (!joinable()) {
		std::abort();
	}
	delete impl_;
}

bool thread::run()
{
	if (impl_) {
		return false;
	}
	
	try {
		impl_ = new impl;
		
		scoped_lock l(impl_->m_);
		std::thread t(impl::run, std::ref(*this));
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

bool thread::joinable() const
{
	return impl_ == 0;
}

}
