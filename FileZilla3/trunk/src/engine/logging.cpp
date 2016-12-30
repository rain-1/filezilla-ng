#include <filezilla.h>

#include "logging_private.h"

#include <errno.h>

#ifndef FZ_WINDOWS
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

bool CLogging::m_logfile_initialized = false;
#ifdef FZ_WINDOWS
HANDLE CLogging::m_log_fd = INVALID_HANDLE_VALUE;
#else
int CLogging::m_log_fd = -1;
#endif
std::string CLogging::m_prefixes[static_cast<int>(MessageType::count)];
unsigned int CLogging::m_pid;
int CLogging::m_max_size;
fz::native_string CLogging::m_file;

int CLogging::m_refcount = 0;
fz::mutex CLogging::mutex_(false);

thread_local int CLogging::debug_level_{};
thread_local int CLogging::raw_listing_{};

CLogging::CLogging(CFileZillaEnginePrivate & engine)
	: engine_(engine)
{
	fz::scoped_lock l(mutex_);
	m_refcount++;
}

CLogging::~CLogging()
{
	fz::scoped_lock l(mutex_);
	m_refcount--;

	if (!m_refcount) {
#ifdef FZ_WINDOWS
		if (m_log_fd != INVALID_HANDLE_VALUE) {
			CloseHandle(m_log_fd);
			m_log_fd = INVALID_HANDLE_VALUE;
		}
#else
		if (m_log_fd != -1) {
			close(m_log_fd);
			m_log_fd = -1;
		}
#endif
		m_logfile_initialized = false;
	}
}

bool CLogging::ShouldLog(MessageType nMessageType) const
{
	switch (nMessageType) {
	case MessageType::Debug_Warning:
		if (!debug_level_)
			return false;
		break;
	case MessageType::Debug_Info:
		if (debug_level_ < 2)
			return false;
		break;
	case MessageType::Debug_Verbose:
		if (debug_level_ < 3)
			return false;
		break;
	case MessageType::Debug_Debug:
		if (debug_level_ != 4)
			return false;
		break;
	case MessageType::RawList:
		if (!raw_listing_)
			return false;
		break;
	default:
		break;
	}
	return true;
}

bool CLogging::InitLogFile(fz::scoped_lock& l) const
{
	if (m_logfile_initialized)
		return true;

	m_logfile_initialized = true;

	m_file = fz::to_native(engine_.GetOptions().GetOption(OPTION_LOGGING_FILE));
	if (m_file.empty())
		return false;

#ifdef FZ_WINDOWS
	m_log_fd = CreateFile(m_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (m_log_fd == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
#else
	m_log_fd = open(m_file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
	if (m_log_fd == -1) {
		int err = errno;
#endif
		l.unlock(); //Avoid recursion
		LogMessage(MessageType::Error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
		return false;
	}

	m_prefixes[static_cast<int>(MessageType::Status)] = fz::to_utf8(_("Status:"));
	m_prefixes[static_cast<int>(MessageType::Error)] = fz::to_utf8(_("Error:"));
	m_prefixes[static_cast<int>(MessageType::Command)] = fz::to_utf8(_("Command:"));
	m_prefixes[static_cast<int>(MessageType::Response)] = fz::to_utf8(_("Response:"));
	m_prefixes[static_cast<int>(MessageType::Debug_Warning)] = fz::to_utf8(_("Trace:"));
	m_prefixes[static_cast<int>(MessageType::Debug_Info)] = m_prefixes[static_cast<int>(MessageType::Debug_Warning)];
	m_prefixes[static_cast<int>(MessageType::Debug_Verbose)] = m_prefixes[static_cast<int>(MessageType::Debug_Warning)];
	m_prefixes[static_cast<int>(MessageType::Debug_Debug)] = m_prefixes[static_cast<int>(MessageType::Debug_Warning)];
	m_prefixes[static_cast<int>(MessageType::RawList)] = fz::to_utf8(_("Listing:"));

#if FZ_WINDOWS
	m_pid = static_cast<unsigned int>(GetCurrentProcessId());
#else
	m_pid = static_cast<unsigned int>(getpid());
#endif

	m_max_size = engine_.GetOptions().GetOptionVal(OPTION_LOGGING_FILE_SIZELIMIT);
	if (m_max_size < 0)
		m_max_size = 0;
	else if (m_max_size > 2000)
		m_max_size = 2000;
	m_max_size *= 1024 * 1024;

	return true;
}

void CLogging::LogToFile(MessageType nMessageType, std::wstring const& msg) const
{
	fz::scoped_lock l(mutex_);

	if (!m_logfile_initialized) {
		if (!InitLogFile(l)) {
			return;
		}
	}
#ifdef FZ_WINDOWS
	if (m_log_fd == INVALID_HANDLE_VALUE) {
		return;
	}
#else
	if (m_log_fd == -1) {
		return;
	}
#endif

	fz::datetime now = fz::datetime::now();
	std::string const out = fz::sprintf("%s %u %u %s %s"
#ifdef FZ_WINDOWS
		"\r\n",
#else
		"\n",
#endif
		now.format("%Y-%m-%d %H:%M:%S", fz::datetime::local), m_pid, engine_.GetEngineId(), m_prefixes[static_cast<int>(nMessageType)], fz::to_utf8(msg));

#ifdef FZ_WINDOWS
	if (m_max_size) {
		LARGE_INTEGER size;
		if (!GetFileSizeEx(m_log_fd, &size) || size.QuadPart > m_max_size) {
			CloseHandle(m_log_fd);
			m_log_fd = INVALID_HANDLE_VALUE;

			// m_log_fd might no longer be the original file.
			// Recheck on a new handle. Proteced with a mutex against other processes
			HANDLE hMutex = ::CreateMutexW(0, true, L"FileZilla 3 Logrotate Mutex");
			if (!hMutex) {
				DWORD err = GetLastError();
				l.unlock();
				LogMessage(MessageType::Error, _("Could not create logging mutex: %s"), GetSystemErrorDescription(err));
				return;
			}

			HANDLE hFile = CreateFileW(m_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
			if (hFile == INVALID_HANDLE_VALUE) {
				DWORD err = GetLastError();

				// Oh dear..
				ReleaseMutex(hMutex);
				CloseHandle(hMutex);

				l.unlock(); // Avoid recursion
				LogMessage(MessageType::Error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}

			DWORD err{};
			if (GetFileSizeEx(hFile, &size) && size.QuadPart > m_max_size) {
				CloseHandle(hFile);

				// MoveFileEx can fail if trying to access a deleted file for which another process still has
				// a handle. Move it far away first.
				// Todo: Handle the case in which logdir and tmpdir are on different volumes.
				// (Why is everthing so needlessly complex on MSW?)

				wchar_t tempDir[MAX_PATH + 1];
				DWORD res = GetTempPath(MAX_PATH, tempDir);
				if (res && res <= MAX_PATH) {
					tempDir[MAX_PATH] = 0;

					wchar_t tempFile[MAX_PATH + 1];
					res = GetTempFileNameW(tempDir, L"fz3", 0, tempFile);
					if (res) {
						tempFile[MAX_PATH] = 0;
						MoveFileExW((m_file + L".1").c_str(), tempFile, MOVEFILE_REPLACE_EXISTING);
						DeleteFileW(tempFile);
					}
				}
				MoveFileExW(m_file.c_str(), (m_file + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
				m_log_fd = CreateFileW(m_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
				if (m_log_fd == INVALID_HANDLE_VALUE) {
					// If this function would return bool, I'd return FILE_NOT_FOUND here.
					err = GetLastError();
				}
			}
			else {
				m_log_fd = hFile;
			}

			if (hMutex) {
				ReleaseMutex(hMutex);
				CloseHandle(hMutex);
			}

			if (err) {
				l.unlock(); // Avoid recursion
				LogMessage(MessageType::Error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}
		}
	}
	DWORD len = out.size();
	DWORD written;
	BOOL res = WriteFile(m_log_fd, out.c_str(), len, &written, 0);
	if (!res || written != len) {
		DWORD err = GetLastError();
		CloseHandle(m_log_fd);
		m_log_fd = INVALID_HANDLE_VALUE;
		l.unlock(); // Avoid recursion
		LogMessage(MessageType::Error, _("Could not write to log file: %s"), GetSystemErrorDescription(err));
	}
#else
	if (m_max_size) {
		struct stat buf;
		int rc = fstat(m_log_fd, &buf);
		while (!rc && buf.st_size > m_max_size) {
			struct flock lock = {};
			lock.l_type = F_WRLCK;
			lock.l_whence = SEEK_SET;
			lock.l_start = 0;
			lock.l_len = 1;

			// Retry through signals
			while ((rc = fcntl(m_log_fd, F_SETLKW, &lock)) == -1 && errno == EINTR);

			// Ignore any other failures
			int fd = open(m_file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
			if (fd == -1) {
				int err = errno;

				close(m_log_fd);
				m_log_fd = -1;

				l.unlock(); // Avoid recursion
				LogMessage(MessageType::Error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}
			struct stat buf2;
			rc = fstat(fd, &buf2);

			// Different files
			if (!rc && buf.st_ino != buf2.st_ino) {
				close(m_log_fd); // Releases the lock
				m_log_fd = fd;
				buf = buf2;
				continue;
			}

			// The file is indeed the log file and we are holding a lock on it.

			// Rename it
			rc = rename(m_file.c_str(), (m_file + ".1").c_str());
			close(m_log_fd);
			close(fd);

			// Get the new file
			m_log_fd = open(m_file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
			if (m_log_fd == -1) {
				int err = errno;
				l.unlock(); // Avoid recursion
				LogMessage(MessageType::Error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}

			if (!rc) {
				// Rename didn't fail
				rc = fstat(m_log_fd, &buf);
			}
		}
	}
	size_t written = write(m_log_fd, out.c_str(), out.size());
	if (written != out.size()) {
		int err = errno;
		close(m_log_fd);
		m_log_fd = -1;

		l.unlock(); // Avoid recursion
		LogMessage(MessageType::Error, _("Could not write to log file: %s"), GetSystemErrorDescription(err));
	}
#endif
}

void CLogging::UpdateLogLevel(COptionsBase & options)
{
	debug_level_ = options.GetOptionVal(OPTION_LOGGING_DEBUGLEVEL);
	raw_listing_ = options.GetOptionVal(OPTION_LOGGING_RAWLISTING);
}
