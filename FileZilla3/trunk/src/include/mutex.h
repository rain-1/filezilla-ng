#ifndef FILEZILLA_MUTEX_HEADER
#define FILEZILLA_MUTEX_HEADER

/* A mutex, or critical section, should be as lightweight as possible.
 * Unfortunately, wxWidgets' mutex isn't lightweight at all.
 * Testing has shown that locking or unlocking a wxMutex consists of
 * 60% useless cruft, e.g. deadlock detection (Why try to detect deadlocks?
 * Deadlocks detect itself!)
 *
 * Likewise, wxCondition carries a heavy overhead as well. In particular, under MSW
 * it doesn't and can't (due to XP compatibility) use Vista+'s CONDITION_VARIABLE.
 *
 * Unfortunately we can't use std::mutex for the rescue as MinGW doesn't implement
 * C++11 threading yet in common configurations.
 */

#ifndef __WXMSW__
#include <pthread.h>
#endif

template<typename Mutex>
class scoped_lock;

template<typename Mutex>
class condition;

class mutex final
{
public:
	explicit mutex();
	~mutex();

	mutex( mutex const& ) = delete;
	mutex& operator=( mutex const& ) = delete;

	// Beware, manual locking isn't exception safe
	void lock();
	void unlock();

private:
	friend class scoped_lock<mutex>;
	friend class condition<mutex>;

#ifdef __WXMSW__
	CRITICAL_SECTION m_;
#else
	pthread_mutex_t m_;
#endif
};

class smutex final
{
public:
	explicit smutex();
	~smutex();

	smutex(smutex const&) = delete;
	smutex& operator=(smutex const&) = delete;

	// Beware, manual locking isn't exception safe
	void lock();
	void unlock();

private:
	friend class scoped_lock<smutex>;
	friend class condition<smutex>;

#ifdef __WXMSW__
	SRWLOCK m_{SRWLOCK_INIT};
#else
	pthread_mutex_t m_;
#endif
};

template<>
class scoped_lock<mutex> final
{
public:
	explicit scoped_lock(mutex& m)
		: m_(&m.m_)
	{
#ifdef __WXMSW__
		EnterCriticalSection(m_);
#else
		pthread_mutex_lock(m_);
#endif
	}

	~scoped_lock()
	{
		if (locked_) {
	#ifdef __WXMSW__
			LeaveCriticalSection(m_);
	#else
			pthread_mutex_unlock(m_);
	#endif
		}

	}

	scoped_lock( scoped_lock const& ) = delete;
	scoped_lock& operator=( scoped_lock const& ) = delete;

	void lock()
	{
		locked_ = true;
#ifdef __WXMSW__
		EnterCriticalSection(m_);
#else
		pthread_mutex_lock(m_);
#endif
	}

	void unlock()
	{
		locked_ = false;
#ifdef __WXMSW__
		LeaveCriticalSection(m_);
#else
		pthread_mutex_unlock(m_);
#endif
	}

private:
	friend class condition<mutex>;

#ifdef __WXMSW__
	CRITICAL_SECTION * const m_;
#else
	pthread_mutex_t * const m_;
#endif
	bool locked_{true};
};

template<>
class scoped_lock<smutex> final
{
public:
	explicit scoped_lock(smutex& m)
		: m_(&m.m_)
	{
#ifdef __WXMSW__
		AcquireSRWLockExclusive(m_);
#else
		pthread_mutex_lock(m_);
#endif
	}

	~scoped_lock()
	{
		if (locked_) {
#ifdef __WXMSW__
			ReleaseSRWLockExclusive(m_);
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
#ifdef __WXMSW__
		AcquireSRWLockExclusive(m_);
#else
		pthread_mutex_lock(m_);
#endif
	}

	void unlock()
	{
		locked_ = false;
#ifdef __WXMSW__
		ReleaseSRWLockExclusive(m_);
#else
		pthread_mutex_unlock(m_);
#endif
	}

private:
	friend class condition<smutex>;

#ifdef __WXMSW__
	SRWLOCK * const m_;
#else
	pthread_mutex_t * const m_;
#endif
	bool locked_{ true };
};


template<typename Mutex>
class condition final
{
public:
	condition();
	~condition();

	condition(condition const&) = delete;
	condition& operator=(condition const&) = delete;

	void wait(scoped_lock<Mutex>& l);

	// Milliseconds
	// Returns false on timeout
	bool wait(scoped_lock<Mutex>& l, int timeout_ms);

	void signal(scoped_lock<Mutex>& l);

	bool signalled(scoped_lock<Mutex> const&) const { return signalled_; }
private:
#ifdef __WXMSW__
	CONDITION_VARIABLE cond_{CONDITION_VARIABLE_INIT};
#else
	pthread_cond_t cond_;
#endif
	bool signalled_;
};

#endif
