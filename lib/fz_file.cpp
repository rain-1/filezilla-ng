#include "libfilezilla.hpp"
#include "fz_file.hpp"

#ifndef FZ_WINDOWS
#include <errno.h>
#include <sys/stat.h>
#endif

namespace fz {

static_assert(sizeof(file::ssize_t) >= 8, "Need 64bit support.");

file::file()
{
}

file::file(native_string const& f, mode m, disposition d)
{
	open(f, m, d);
}

file::~file()
{
	close();
}

#ifdef FZ_WINDOWS
bool file::open(native_string const& f, mode m, disposition d)
{
	close();

	DWORD dispositionFlags;
	if (m == writing) {
		if (d == empty) {
			dispositionFlags = CREATE_ALWAYS;
		}
		else {
			dispositionFlags = OPEN_ALWAYS;
		}
	}
	else {
		dispositionFlags = OPEN_EXISTING;
	}

	DWORD shareMode = FILE_SHARE_READ;
	if (m == reading) {
		shareMode |= FILE_SHARE_WRITE;
	}

	hFile_ = CreateFile(f.c_str(), (m == reading) ? GENERIC_READ : GENERIC_WRITE, shareMode, 0, dispositionFlags, FILE_FLAG_SEQUENTIAL_SCAN, 0);

	return hFile_ != INVALID_HANDLE_VALUE;
}

void file::close()
{
	if (hFile_ != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile_);
		hFile_ = INVALID_HANDLE_VALUE;
	}
}

size_t file::size() const
{
	size_t ret = err;

	LARGE_INTEGER size{};
	if (GetFileSizeEx(hFile_, &size)) {
		ret = static_cast<size_t>(size.QuadPart);
	}
	return ret;
}

file::ssize_t file::seek(ssize_t offset, seek_mode m)
{
	file::ssize_t ret = -1;

	LARGE_INTEGER dist{};
	dist.QuadPart = offset;

	DWORD method = FILE_BEGIN;
	if (m == current) {
		method = FILE_CURRENT;
	}
	else if (m == end) {
		method = FILE_END;
	}

	LARGE_INTEGER newPos{};
	if (SetFilePointerEx(hFile_, dist, &newPos, method)) {
		ret = newPos.QuadPart;
	}
	return ret;
}

bool file::truncate()
{
	return !!SetEndOfFile(hFile_);
}

size_t file::read(void *buf, size_t count)
{
	size_t ret = -1;

	DWORD read = 0;
	if (ReadFile(hFile_, buf, count, &read, 0)) {
		ret = static_cast<size_t>(read);
	}

	return ret;
}

size_t file::write(void const* buf, size_t count)
{
	size_t ret = err;

	DWORD written = 0;
	if (WriteFile(hFile_, buf, count, &written, 0)) {
		ret = static_cast<size_t>(written);
	}

	return ret;
}

bool file::opened() const
{
	return hFile_ != INVALID_HANDLE_VALUE;
}

#else

bool file::open(native_string const& f, mode m, disposition d)
{
	close();

	int flags = O_CLOEXEC;
	if (m == reading) {
		flags |= O_RDONLY;
	}
	else {
		flags |= O_WRONLY | O_CREAT;
		if (d == empty) {
			flags |= O_TRUNC;
		}
	}
	fd_ = open(f.fn_str(), flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

#if HAVE_POSIX_FADVISE
	if (fd_ != -1) {
		(void)posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);
	}
#endif

	return fd_ != -1;
}

void file::close()
{
	if (fd_ != -1) {
		close(fd_);
		fd_ = -1;
	}
}

size_t file::size() const
{
	size_t ret = err;

	struct stat buf;
	if (!fstat(fd_, &buf)) {
		ret = buf.st_size;
	}

	return ret;
}

file::ssize_t file::seek(ssize_t offset, seek_mode m)
{
	ssize_t ret = -1;

	int whence = SEEK_SET;
	if (m == current) {
		whence = SEEK_CUR;
	}
	else if (m == end) {
		whence = SEEK_END;
	}

	auto newPos = lseek(fd_, offset, whence);
	if (newPos != static_cast<off_t>(-1)) {
		ret = newPos;
	}

	return ret;
}

bool file::truncate()
{
	bool ret = false;

	auto length = lseek(fd_, 0, SEEK_CUR);
	if (length != static_cast<off_t>(-1)) {
		do {
			ret = !ftruncate(fd_, length);
		} while (!ret && (errno == EAGAIN || errno == EINTR));
	}

	return ret;
}

size_t file::read(void *buf, size_t count)
{
	size_t ret;
	do {
		ret = ::read(fd_, buf, count);
	} while (ret == -1 && (errno == EAGAIN || errno == EINTR));

	return ret;
}

size_t file::write(void const* buf, size_t count)
{
	size_t ret;
	do {
		ret = ::write(fd_, buf, count);
	} while (ret == -1 && (errno == EAGAIN || errno == EINTR));

	return ret;
}

bool file::opened() const
{
	return fd_ != -1;
}

#endif

}