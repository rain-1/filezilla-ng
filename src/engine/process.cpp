#include <filezilla.h>
#include "process.h"

namespace {
void ResetHandle(HANDLE& handle)
{
	if (handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
		handle = INVALID_HANDLE_VALUE;
	}
};

bool Uninherit(HANDLE& handle)
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

class Pipe final
{
public:
	Pipe() = default;

	~Pipe()
	{
		reset();
	}

	Pipe(Pipe const&) = delete;
	Pipe& operator=(Pipe const&) = delete;
	
	bool Create(bool local_is_input)
	{
		reset();

		SECURITY_ATTRIBUTES sa{};
		sa.bInheritHandle = TRUE;
		sa.nLength = sizeof(sa);

		BOOL res = CreatePipe(&read_, &write_, &sa, 0);
		if (res) {
			// We only want one side of the pipe to be inheritable
			if (!Uninherit(local_is_input ? read_ : write_)) {
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
		ResetHandle(read_);
		ResetHandle(write_);
	}

	HANDLE read_{INVALID_HANDLE_VALUE};
	HANDLE write_{INVALID_HANDLE_VALUE};
};
}

class CProcess::Impl
{
public:
	Impl() = default;
	~Impl()
	{
		Kill();
	}

	Impl(Impl const&) = delete;
	Impl& operator=(Impl const&) = delete;

	bool CreatePipes()
	{
		return 
			in_.Create(false) &&
			out_.Create(true) &&
			err_.Create(true);
	}

	bool Execute(wxString const& cmd, wxString const& args)
	{
		DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW;

		if (!CreatePipes()) {
			return false;
		}

		STARTUPINFO si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = in_.read_;
		si.hStdOutput = out_.write_;
		si.hStdError = err_.write_;

		auto cmdline = GetCmdLine(cmd, args);

		PROCESS_INFORMATION pi{};
		BOOL res = CreateProcess(cmd, cmdline.get(), 0, 0, TRUE, flags, 0, 0, &si, &pi);
		if (!res) {
			return false;
		}

		process_ = pi.hProcess;

		// We don't need to use these
		ResetHandle(in_.read_);
		ResetHandle(out_.write_);
		ResetHandle(err_.write_);

		return true;
	}

	void Kill()
	{
		if (process_ != INVALID_HANDLE_VALUE) {
			in_.reset();
			if (WaitForSingleObject(process_, 500) == WAIT_TIMEOUT) {
				TerminateProcess(process_, 0);
			}
			ResetHandle(process_);
			out_.reset();
			err_.reset();
		}
	}

	int Read(char* buffer, unsigned int len)
	{
		DWORD read = 0;
		BOOL res = ReadFile(out_.read_, buffer, len, &read, 0);
		if (!res || read < 0) {
			return -1;
		}
		return read;
	}

	bool Write(char const* buffer, unsigned int len)
	{
		while (len > 0) {
			DWORD written = 0;
			BOOL res = WriteFile(in_.write_, buffer, len, &written, 0);
			if (!res || written <= 0) {
				return false;
			}
			buffer += written;
			len -= written;
		}
		return true;
	}

private:
	std::unique_ptr<wxChar[]> GetCmdLine(wxString const& cmd, wxString const& args)
	{
		wxString cmdline = cmd; _T("\"") + cmd + _T("\" ") + args;
		std::unique_ptr<wxChar[]> ret;
		ret.reset(new wxChar[cmdline.size() + 1]);
		wxStrcpy(ret.get(), cmdline);
		return ret;
	}

	HANDLE process_{INVALID_HANDLE_VALUE};

	Pipe in_;
	Pipe out_;
	Pipe err_;
};

CProcess::CProcess()
	: impl_(make_unique<Impl>())
{
}

CProcess::~CProcess()
{
	impl_.reset();
}

bool CProcess::Execute(wxString const& cmd, wxString const& args)
{
	return impl_ ? impl_->Execute(cmd, args) : false;
}

void CProcess::Kill()
{
	if (impl_) {
		impl_->Kill();
	}
}

int CProcess::Read(char* buffer, unsigned int len)
{
	return impl_ ? impl_->Read(buffer, len) : -1;
}

bool CProcess::Write(char const* buffer, unsigned int len)
{
	return impl_ ? impl_->Write(buffer, len) : false;
}
