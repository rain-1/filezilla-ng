#ifndef __LOCAL_FILESYS_H__
#define __LOCAL_FILESYS_H__

namespace fz {

// This class adds an abstraction layer for the local filesystem.
// Although wxWidgets provides functions for this, they are in
// general too slow.
// This class offers exactly what's needed by FileZilla and
// exploits some platform-specific features.
class local_filesys final
{
public:
	local_filesys() = default;
	~local_filesys();

	local_filesys(local_filesys const&) = delete;
	local_filesys& operator=(local_filesys const&) = delete;

	enum type {
		unknown = -1,
		file,
		dir,
		link
	};

	static const wxChar path_separator;

	// GetFileType return the type of the passed path. Does not follow symbolic links
	static type GetFileType(wxString const& path);

	// Gets the info for the passed arguments. Follows symbolic links and stats the target, sets isLink to true if path was
	// a link.
	static type GetFileInfo(wxString const& path, bool &isLink, int64_t* size, datetime* modificationTime, int* mode);

	// Shortcut, returns -1 on error.
	static int64_t GetSize(wxString const& path, bool *isLink = 0);

	// If parent window is given, display confirmation dialog
	// Returns false iff there's an encoding error, e.g. program
	// started without UTF-8 locale.
	static bool RecursiveDelete(wxString const& path, wxWindow* parent);
	static bool RecursiveDelete(std::list<wxString> dirsToVisit, wxWindow* parent);

	bool begin_find_files(wxString path, bool dirs_only);
	bool get_next_file(wxString& name);
	bool get_next_file(wxString& name, bool &isLink, bool &is_dir, int64_t* size, datetime* modificationTime, int* mode);
	void end_find_files();

	static datetime get_modification_time(wxString const& path);
	static bool set_modification_time(wxString const& path, const datetime& t);

	/// Get the target path of a symbolic link
	static wxString get_link_target(wxString const& path);

private:
#ifndef __WXMSW__
	static type GetFileInfo(const char* path, bool &isLink, int64_t* size, datetime* modificationTime, int* mode);
	void alloc_path_buffer(const char* file); // Ensures m_raw_path is large enough to hold path and filename
#endif

	// State for directory enumeration
	bool m_dirs_only{};
#ifdef __WXMSW__
	WIN32_FIND_DATA m_find_data{};
	HANDLE m_hFind{INVALID_HANDLE_VALUE};
	bool m_found{};
	wxString m_find_path;
#else
	char* m_raw_path{};
	char* m_file_part{}; // Points into m_raw_path past the trailing slash of the path part
	int m_buffer_length{};
	DIR* m_dir{};
#endif
};

}

#endif
