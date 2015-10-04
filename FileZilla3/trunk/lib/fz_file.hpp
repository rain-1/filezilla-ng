#ifndef LIBFILEZILLA_FILE_HEADER
#define LIBFILEZILLA_FILE_HEADER

#include "libfilezilla.hpp"

#ifdef FZ_WINDOWS
#include "private/windows.hpp"
#endif

namespace fz {

// Lean replacement of wxFile that is implemented in terms of CreateFile instead of _open on Windows.
class file final
{
public:
	typedef std::streamsize ssize_t;

	enum mode {
		reading,
		writing
	};

	// Only evaluated when opening existing files for writing
	// Non-existing files will always be created when writing.
	// Opening for reading never creates files
	enum disposition
	{
		existing, // Keep existing data
		empty // Truncate file if already existing, otherwise create new
	};

	file();
	file(native_string const& f, mode m, disposition d = existing);

	~file();

	file(file const&) = delete;
	file& operator=(file const&) = delete;

	bool opened() const;

	bool open(native_string const& f, mode m, disposition d = existing);
	void close();

	enum seek_mode {
		begin,
		current,
		end
	};

	static size_t const err{static_cast<size_t>(-1)};

	// Gets size of file
	// Returns err on error
	size_t size() const;

	// Relative seek based on seek mode
	// Returns -1 on error, otherwise new absolute offset in file
	// On failure, the new position in the file is undefined.
	ssize_t seek(ssize_t offset, seek_mode m);

	// Truncate the file to the current position of the file pointer.
	bool truncate();

	// Returns number of bytes read or err on error
	size_t read(void *buf, size_t count);

	// Returns number of bytes written or err on error
	size_t write(void const* buf, size_t count);

protected:
#ifdef FZ_WINDOWS
	HANDLE hFile_{INVALID_HANDLE_VALUE};
#else
	int fd_{-1};
#endif
};

}
#endif
