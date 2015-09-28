#ifndef LIBFILEZILLA_THREAD_HEADER
#define LIBFILEZILLA_THREAD_HEADER

#include "libfilezilla.hpp"

/*
Unfortunately std::thread isn't implemented on all MinGW flavors. Most
notably, MinGW as shipped by Debian Jessie does not have std::thread.

This class only supports joinable threads [1]. You _MUST_ join threads
in the destructor of the outermost derived class. ~thread calls abort()
if join has not previously been called.

Calling join blocks until the thread has quit.

Constructing the thread object creates the thread but does not yet start it
so that you can finish initialization. Call the run method.

[1] Detached threads, that's a race condition by design. You cannot use a
    detached thread and shutdown your program cleanly. Clean shutdown is hard
	enough with joinable threads already.
*/

namespace fz {

class thread
{
public:
	thread();
	virtual ~thread();

	bool run();

	void join();

	// Must not be called from the spawned thread.
	bool joinable() const;

protected:
	// The thread's entry point
	virtual void entry() = 0;

private:
	class impl;
	friend class impl;
	impl* impl_{};
};

}

#endif