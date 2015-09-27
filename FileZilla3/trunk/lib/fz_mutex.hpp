#ifndef LIBFILEZILLA_MUTEX_HEADER
#define LIBFILEZILLA_MUTEX_HEADER

/* 
 * Unfortunately we can't use std::mutex and std::condition_variable as MinGW doesn't implement
 * C++11 threading yet. Even if it does, it doesn't make use of Vista+'s CONDITION_VARIABLE, so
 * it's quite slow for our needs.
 */
#include "libfilezilla.hpp"

#ifdef FZ_WINDOWS
#include "private/windows.hpp"
#else
#include <pthread.h>
#endif

namespace fz {

class mutex final
{
public:
	explicit mutex(bool recursive = true);
	~mutex();

	mutex(mutex const&) = delete;
	mutex& operator=(mutex const&) = delete;

	// Beware, manual locking isn't exception safe
	void lock();
	void unlock();

private:
	friend class condition;
	friend class scoped_lock;

#ifdef FZ_WINDOWS
	CRITICAL_SECTION m_;
#else
	pthread_mutex_t m_;
#endif
};

class scoped_lock final
{
public:
	explicit scoped_lock(mutex& m)
		: m_(&m.m_)
	{
#ifdef FZ_WINDOWS
		EnterCriticalSection(m_);
#else
		pthread_mutex_lock(m_);
#endif
	}

	~scoped_lock()
	{
		if (locked_) {
#ifdef FZ_WINDOWS
			LeaveCriticalSection(m_);
#else
			pthread_mutex_unlock(m_);
#endif
		}

	}

	scoped_lock(scoped_lock const&) = delete;
	scoped_lock& operator=(scoped_lock const&) = delete;

	void lock()
	{
		locked_ = true;
#ifdef FZ_WINDOWS
		EnterCriticalSection(m_);
#else
		pthread_mutex_lock(m_);
#endif
	}

	void unlock()
	{
		locked_ = false;
#ifdef FZ_WINDOWS
		LeaveCriticalSection(m_);
#else
		pthread_mutex_unlock(m_);
#endif
	}

private:
	friend class condition;

#ifdef FZ_WINDOWS
	CRITICAL_SECTION * const m_;
#else
	pthread_mutex_t * const m_;
#endif
	bool locked_{true};
};

class condition final
{
public:
	condition();
	~condition();

	condition(condition const&) = delete;
	condition& operator=(condition const&) = delete;

	void wait(scoped_lock& l);

	// Milliseconds
	// Returns false on timeout
	bool wait(scoped_lock& l, int timeout_ms);

	void signal(scoped_lock& l);

	bool signalled(scoped_lock const&) const { return signalled_; }
private:
#ifdef FZ_WINDOWS
	CONDITION_VARIABLE cond_;
#else
	pthread_cond_t cond_;
#endif
	bool signalled_{};
};

}
#endif
