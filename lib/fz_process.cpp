#include "fz_process.hpp"

#ifdef FZ_WINDOWS

#include "private/windows.hpp"

namespace fz {

namespace {
void reset_handle(HANDLE& handle)
{
	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
	}
};

bool uninherit(HANDLE& handle)
{
	if (handle != INVALID_HANDLE_VALUE) {
		HANDLE newHandle = INVALID_HANDLE_VALUE;

		if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &newHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
			newHandle = INVALID_HANDLE_VALUE;
		}
		CloseHandle(handle);
		handle = newHandle;
	}

	return handle != INVALID_HANDLE_VALUE;
}

class pipe final
{
public:
	pipe() = default;

	~pipe()
	{
		reset();
	}

	pipe(pipe const&) = delete;
	pipe& operator=(pipe const&) = delete;

	bool create(bool local_is_input)
	{
		reset();

		SECURITY_ATTRIBUTES sa{};
		sa.bInheritHandle = TRUE;
		sa.nLength = sizeof(sa);

		BOOL res = CreatePipe(&read_, &write_, &sa, 0);
		if (res) {
			// We only want one side of the pipe to be inheritable
			if (!uninherit(local_is_input ? read_ : write_)) {
				reset();
			}
		}
		else {
			read_ = INVALID_HANDLE_VALUE;
			write_ = INVALID_HANDLE_VALUE;
		}
		return valid();
	}

	bool valid() const {
		return read_ != INVALID_HANDLE_VALUE && write_ != INVALID_HANDLE_VALUE;
	}

	void reset()
	{
		reset_handle(read_);
		reset_handle(write_);
	}

	HANDLE read_{INVALID_HANDLE_VALUE};
	HANDLE write_{INVALID_HANDLE_VALUE};
};
}

class process::impl
{
public:
	impl() = default;
	~impl()
	{
		kill();
	}

	impl(impl const&) = delete;
	impl& operator=(impl const&) = delete;

	bool create_pipes()
	{
		return
			in_.create(false) &&
			out_.create(true) &&
			err_.create(true);
	}

	bool spawn(native_string const& cmd, std::vector<native_string> const& args)
	{
		DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW;

		if (!create_pipes()) {
			return false;
		}

		STARTUPINFO si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = in_.read_;
		si.hStdOutput = out_.write_;
		si.hStdError = err_.write_;

		auto cmdline = get_cmd_line(cmd, args);

		PROCESS_INFORMATION pi{};

		auto cmdline_buf = &cmdline[0];

		BOOL res = CreateProcess(cmd.c_str(), cmdline_buf, 0, 0, TRUE, flags, 0, 0, &si, &pi);
		if (!res) {
			return false;
		}

		process_ = pi.hProcess;

		// We don't need to use these
		reset_handle(pi.hThread);
		reset_handle(in_.read_);
		reset_handle(out_.write_);
		reset_handle(err_.write_);

		return true;
	}

	void kill()
	{
		if (process_ != INVALID_HANDLE_VALUE) {
			in_.reset();
			if (WaitForSingleObject(process_, 500) == WAIT_TIMEOUT) {
				TerminateProcess(process_, 0);
			}
			reset_handle(process_);
			out_.reset();
			err_.reset();
		}
	}

	int read(char* buffer, unsigned int len)
	{
		DWORD read = 0;
		BOOL res = ReadFile(out_.read_, buffer, len, &read, 0);
		if (!res) {
			return -1;
		}
		return read;
	}

	bool write(char const* buffer, unsigned int len)
	{
		while (len > 0) {
			DWORD written = 0;
			BOOL res = WriteFile(in_.write_, buffer, len, &written, 0);
			if (!res || written == 0) {
				return false;
			}
			buffer += written;
			len -= written;
		}
		return true;
	}

private:
	native_string escape_argument(native_string const& arg)
	{
		native_string ret;

		// Treat newlines are whitespace just to be sure, even if MSDN doesn't mention it
		if (arg.find_first_of(fzT(" \"\t\r\n\v")) != native_string::npos) {
			// Quite horrible, as per MSDN: 
			// Backslashes are interpreted literally, unless they immediately precede a double quotation mark.
			// If an even number of backslashes is followed by a double quotation mark, one backslash is placed in the argv array for every pair of backslashes, and the double quotation mark is interpreted as a string delimiter.
			// If an odd number of backslashes is followed by a double quotation mark, one backslash is placed in the argv array for every pair of backslashes, and the double quotation mark is "escaped" by the remaining backslash, causing a literal double quotation mark (") to be placed in argv.

			ret = fzT("\"");
			int backslashCount = 0;
			for (auto it = arg.begin(); it != arg.end(); ++it) {
				if (*it == '\\') {
					++backslashCount;
				}
				else {
					if (*it == '"') {
						// Escape all preceeding backslashes and escape the quote
						ret += native_string(backslashCount + 1, '\\');
					}
					backslashCount = 0;
				}
				ret += *it;
			}
			if (backslashCount) {
				// Escape all preceeding backslashes
				ret += native_string(backslashCount, '\\');
			}

			ret += fzT("\"");
		}
		else {
			ret = arg;
		}

		return ret;
	}

	native_string get_cmd_line(native_string const& cmd, std::vector<native_string> const& args)
	{
		native_string cmdline = escape_argument(cmd);

		for (auto const& arg : args) {
			if (!arg.empty()) {
				cmdline += fzT(" ") + escape_argument(arg);
			}
		}

		return cmdline;
	}

	HANDLE process_{INVALID_HANDLE_VALUE};

	pipe in_;
	pipe out_;
	pipe err_;
};

#else

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

namespace {
void reset_fd(int& fd)
{
	if (fd != -1) {
		close(fd);
		fd = -1;
	}
}

class pipe final
{
public:
	pipe() = default;

	~pipe()
	{
		reset();
	}

	pipe(pipe const&) = delete;
	pipe& operator=(pipe const&) = delete;

	bool create()
	{
		reset();

		int fds[2];
		if (pipe(fds) != 0) {
			return false;
		}

		read_ = fds[0];
		write_ = fds[1];

		return valid();
	}

	bool valid() const {
		return read_ != -1 && write_ != -1;
	}

	void reset()
	{
		reset_fd(read_);
		reset_fd(write_);
	}

	int read_{-1};
	int write_{-1};
};
}

class process::impl
{
public:
	impl() = default;
	~impl()
	{
		kill();
	}

	impl(impl const&) = delete;
	impl& operator=(impl const&) = delete;

	bool create_pipes()
	{
		return
			in_.create() &&
			out_.create() &&
			err_.create();
	}

	void make_arg(native_string const& arg, std::vector<std::unique_ptr<native_string::value_type[]>> & argList)
	{
		// FIXME
		wxCharBuffer buf = arg.mb_str();
		std::unique_ptr<char[]> ret;
		ret.reset(new char[buf.length() + 1]);
		strcpy(ret.get(), buf);
		argList.push_back(std::move(ret));
	}

	void get_argv(native_string const& cmd, std::vector<native_string> const& args, std::vector<std::unique_ptr<char[]>> & argList, std::unique_ptr<char *[]> & argV)
	{
		make_arg(cmd, argList);
		for (auto const& a : args) {
			MakeArg(a, argList);
		}

		argV.reset(new char *[argList.size() + 1]);
		char ** v = argV.get();
		for (auto const& a : argList) {
			*(v++) = a.get();
		}
		*v = 0;
	}

	bool spawn(native_string const& cmd, std::vector<native_string> const& args)
	{
		if (!create_pipes()) {
			return false;
		}

		int pid = fork();
		if (pid < 0) {
			return false;
		}
		else if (!pid) {
			// We're the child.

			// Close uneeded descriptors
			reset_fd(in_.write_);
			reset_fd(out_.read_);
			reset_fd(err_.read_);

			// Redirect to pipe
			if (dup2(in_.read_, STDIN_FILENO) == -1 ||
				dup2(out_.write_, STDOUT_FILENO) == -1 ||
				dup2(err_.write_, STDERR_FILENO) == -1)
			{
				_exit(-1);
			}

			std::vector<std::unique_ptr<char[]>> argList;
			std::unique_ptr<char *[]> argV;
			get_argv(cmd, args, argList, argV);

			// Execute process
			execv(cmd.mb_str(), argV.get()); // noreturn on success

			_exit(-1);
		}
		else {
			// We're the parent
			pid_ = pid;

			// Close unneeded descriptors
			reset_fd(in_.read_);
			reset_fd(out_.write_);
			reset_fd(err_.write_);
		}

		return true;
	}

	void kill()
	{
		in_.reset();

		if (pid_ != -1) {
			kill(pid_, SIGTERM);

			int ret;
			do {
			} while ((ret = waitpid(pid_, 0, 0)) == -1 && errno == EINTR);

			(void)ret;

			pid_ = -1;
		}

		out_.reset();
		err_.reset();
	}

	int read(char* buffer, unsigned int len)
	{
		int r;
		do {
			r = read(out_.read_, buffer, len);
		} while (r == -1 && (errno == EAGAIN || errno == EINTR));

		return r;
	}

	bool write(char const* buffer, unsigned int len)
	{
		while (len) {
			int written;
			do {
				written = write(in_.write_, buffer, len);
			} while (written == -1 && (errno == EAGAIN || errno == EINTR));

			if (written <= 0) {
				return false;
			}

			len -= written;
			buffer += written;
		}
		return true;
	}

	pipe in_;
	pipe out_;
	pipe err_;

	int pid_{-1};
};

#endif


process::process()
	: impl_(std::make_unique<impl>())
{
}

process::~process()
{
	impl_.reset();
}

bool process::spawn(native_string const& cmd, std::vector<native_string> const& args)
{
	return impl_ ? impl_->spawn(cmd, args) : false;
}

void process::kill()
{
	if (impl_) {
		impl_->kill();
	}
}

int process::read(char* buffer, unsigned int len)
{
	return impl_ ? impl_->read(buffer, len) : -1;
}

bool process::write(char const* buffer, unsigned int len)
{
	return impl_ ? impl_->write(buffer, len) : false;
}

}
