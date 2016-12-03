#ifndef FILEZILLA_ENGINE_LOCAL_PATH_HEADER
#define FILEZILLA_ENGINE_LOCAL_PATH_HEADER

#include <libfilezilla/shared.hpp>

// This class encapsulates local paths.
// On Windows it uses the C:\foo\bar\ syntax and also supports
// UNC paths.
// On all other systems it uses /foo/bar/baz/

class CLocalPath final
{
public:
	CLocalPath() = default;
	CLocalPath(CLocalPath const& path) = default;
	CLocalPath(CLocalPath && path) noexcept = default;
	CLocalPath& operator=(CLocalPath const& path) = default;
	CLocalPath& operator=(CLocalPath && path) noexcept = default;

	// Creates path. If the path is not syntactically
	// correct, empty() will return true.
	// If file is given and path not terminated by a separator,
	// the filename portion is returned in file.
	explicit CLocalPath(std::wstring const& path, std::wstring* file = 0);
	bool SetPath(std::wstring const& path, std::wstring* file = 0);

	// Always terminated by a separator
	std::wstring const& GetPath() const { return *m_path; }

	bool empty() const;
	void clear();

	// On failure the path is undefined
	bool ChangePath(std::wstring const& new_path);

	// Do not call with separators in the segment
	void AddSegment(std::wstring const& segment);

	// HasParent() and HasLogicalParent() only return different values on
	// MSW: C:\ is the drive root but has \ as logical parent, the drive list.
	bool HasParent() const;
	bool HasLogicalParent() const;

	CLocalPath GetParent(std::wstring* last_segment = 0) const;

	// If it fails, the path is undefined
	bool MakeParent(std::wstring* last_segment = 0);

	/* Calling GetLastSegment() only returns non-empty string if
	 * HasParent() returns true
	 */
	std::wstring GetLastSegment() const;

	bool IsSubdirOf(const CLocalPath &path) const;
	bool IsParentOf(const CLocalPath &path) const;

	/* Checks if the directory is writeable purely on a syntactical level.
	 * Currently only works on MSW where some logical paths
	 * are not writeable, e.g. the drive list \ or a remote computer \\foo
	 */
	bool IsWriteable() const;

	// Checks if the directory exists.
	bool Exists(std::wstring *error = 0) const;

	// Craetes direcory if it doesn't yet exist
	bool Create(CLocalPath *last_successful = 0);

	static wchar_t const path_separator;

	bool operator==(CLocalPath const& op) const;
	bool operator!=(CLocalPath const& op) const;

	bool operator<(CLocalPath const& op) const;
protected:
	fz::shared_value<std::wstring> m_path;
};

#endif
