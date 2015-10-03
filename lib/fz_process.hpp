#ifndef LIBFILEZILLA_PROCESS_HEADER
#define LIBFILEZILLA_PROCESS_HEADER

#include "libfilezilla.hpp"

/*
The process class manages an asynchronous process with redirected IO.
No console window is being created.

To use, spawn the process and, since it's blocking, call read from a different thread.
*/

#include <memory>
#include <vector>

namespace fz {

class process final
{
public:
	process();
	~process();

	process(process const&) = delete;
	process& operator=(process const&) = delete;

	bool spawn(native_string const& cmd, std::vector<native_string> const& args);

	void kill();

	// Blocking function. Returns Number of bytes read, 0 on EOF, -1 on error.
	int read(char* buffer, unsigned int len);

	// Blocking function
	bool write(char const* buffer, unsigned int len);

private:
	class impl;
	std::unique_ptr<impl> impl_;
};

}

#endif
