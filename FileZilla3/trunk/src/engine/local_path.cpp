#include <filezilla.h>
#include "local_path.h"

#include <libfilezilla/format.hpp>

#ifndef FZ_WINDOWS
#include <errno.h>
#include <sys/stat.h>
#endif

#include <deque>

#include <assert.h>

#ifdef FZ_WINDOWS
wchar_t const CLocalPath::path_separator = '\\';
#else
wchar_t const CLocalPath::path_separator = '/';
#endif

CLocalPath::CLocalPath(std::wstring const& path, std::wstring* file)
{
	SetPath(path, file);
}

bool CLocalPath::SetPath(std::wstring const& path, std::wstring* file)
{
	// This function ensures that the path is in canonical form on success.
	if (path.empty()) {
		m_path.clear();
		return false;
	}

#ifdef FZ_WINDOWS
	if (path == L"\\") {
		m_path.get() = path;
		if (file) {
			file->clear();
		}
		return true;
	}
#endif

	std::deque<wchar_t*> segments; // List to store the beginnings of segments

	wchar_t const* in = path.c_str();

	{
		std::wstring & path_out = m_path.get();
		path_out.resize(path.size() + 1);
		wchar_t * const start = &(path_out[0]);
		wchar_t * out = start;

#ifdef FZ_WINDOWS

		if (*in == '\\') {
			// possibly UNC
			in++;
			if (*in++ != '\\') {
				path_out.clear();
				return false;
			}

			if (*in == '?') {
				// Could be \\?\c:\foo\bar
				// or \\?\UNC\server\share
				// or something else we do not support.
				if (*(++in) != '\\') {
					path_out.clear();
					return false;
				}
				in++;
				if (((*in >= 'a' && *in <= 'z') || (*in >= 'A' || *in <= 'Z')) && *(in+1) == ':') {
					// It's \\?\c:\foo\bar
					goto parse_regular;
				}
				wchar_t const* in_end = in + path.size();
				if (in_end - in < 5 || fz::stricmp(std::wstring(in, in_end), L"UNC\\")) {
					path_out.clear();
					return false;
				}
				in += 4;

				// It's \\?\UNC\server\share
				//              ^we are here
			}

			*out++ = '\\';
			*out++ = '\\';

			// UNC path
			while (*in) {
				if (*in == '/' || *in == '\\')
					break;
				*out++ = *in++;
			}
			*out++ = path_separator;

			if (out - start <= 3) {
				// not a valid UNC path
				path_out.clear();
				return false;
			}

			segments.push_back(out);
		}
		else if ((*in >= 'a' && *in <= 'z') || (*in >= 'A' || *in <= 'Z')) {
parse_regular:
			// Regular path
			*out++ = *in++;

			if (*in++ != ':') {
				path_out.clear();
				return false;
			}
			*out++ = ':';
			if (*in != '/' && *in != '\\' && *in) {
				path_out.clear();
				return false;
			}
			*out++ = path_separator;
			segments.push_back(out);
		}
		else {
			path_out.clear();
			return false;
		}
#else
		if (*in++ != '/') {
			// SetPath only accepts absolute paths
			path_out.clear();
			return false;
		}

		*out++ = '/';
		segments.push_back(out);
#endif

		enum _last
		{
			separator,
			dot,
			dotdot,
			segment
		};
		_last last = separator;

		while (*in) {
			if (*in == '/'
	#ifdef FZ_WINDOWS
				|| *in == '\\'
	#endif
				)
			{
				in++;
				if (last == separator) {
					// /foo//bar is equal to /foo/bar
					continue;
				}
				else if (last == dot) {
					// /foo/./bar is equal to /foo/bar
					last = separator;
					out = segments.back();
					continue;
				}
				else if (last == dotdot) {
					last = separator;

					// Go two segments back if possible
					if (segments.size() > 1) {
						segments.pop_back();
					}
					out = segments.back();
					continue;
				}

				// Ordinary segment just ended.
				*out++ = path_separator;
				segments.push_back(out);
				last = separator;
				continue;
			}
			else if (*in == '.') {
				if (last == separator) {
					last = dot;
				}
				else if (last == dot) {
					last = dotdot;
				}
				else if (last == dotdot) {
					last = segment;
				}
			}
			else {
				last = segment;
			}

			*out++ = *in++;
		}
		if (last == dot) {
			out = segments.back();
		}
		else if (last == dotdot) {
			if (segments.size() > 1) {
				segments.pop_back();
			}
			out = segments.back();
		}
		else if (last == segment) {
			if (file) {
				*file = std::wstring(segments.back(), out);
				out = segments.back();
			}
			else {
				*out++ = path_separator;
			}
		}

		path_out.resize(out - start);
	}

	return true;
}

bool CLocalPath::empty() const
{
	return m_path->empty();
}

void CLocalPath::clear()
{
	m_path.clear();
}

bool CLocalPath::IsWriteable() const
{
	if (m_path->empty()) {
		return false;
	}

#ifdef FZ_WINDOWS
	if (m_path == L"\\") {
		// List of drives not writeable
		return false;
	}

	if (m_path->substr(0, 2) == L"\\\\") {
		auto pos = m_path->find('\\', 2);
		if (pos == std::wstring::npos || pos + 3 >= m_path->size()) {
			// List of shares on a computer not writeable
			return false;
		}
	}
#endif

	return true;
}

bool CLocalPath::HasParent() const
{
#ifdef FZ_WINDOWS
	// C:\f\ has parent
	// C:\ does not
	// \\x\y\ shortest UNC
	//   ^ min
	const int min = 2;
#else
	const int min = 0;
#endif
	for (int i = static_cast<int>(m_path->size()) - 2; i >= min; --i) {
		if ((*m_path)[i] == path_separator)
			return true;
	}

	return false;
}

bool CLocalPath::HasLogicalParent() const
{
#ifdef FZ_WINDOWS
	if (m_path->size() == 3 && (*m_path)[0] != '\\') {
		// Drive root
		return true;
	}
#endif
	return HasParent();
}

CLocalPath CLocalPath::GetParent(std::wstring* last_segment) const
{
	CLocalPath parent;

#ifdef FZ_WINDOWS
	if (m_path->size() == 3 && (*m_path)[0] != '\\') {
		// Drive root
		if (last_segment) {
			last_segment->clear();
		}
		return CLocalPath(L"\\");
	}

	// C:\f\ has parent
	// C:\ does not
	// \\x\y\ shortest UNC
	//   ^ min
	const int min = 2;
#else
	const int min = 0;
#endif
	for (int i = (int)m_path->size() - 2; i >= min; --i) {
		if ((*m_path)[i] == path_separator) {
			if (last_segment) {
				*last_segment = m_path->substr(i + 1, m_path->size() - i - 2);
			}
			return CLocalPath(m_path->substr(0, i + 1));
		}
	}

	return CLocalPath();
}

bool CLocalPath::MakeParent(std::wstring* last_segment)
{
	std::wstring& path = m_path.get();

#ifdef FZ_WINDOWS
	if (path.size() == 3 && path[0] != '\\') {
		// Drive root
		path = L"\\";
		return true;
	}

	// C:\f\ has parent
	// C:\ does not
	// \\x\y\ shortest UNC
	//   ^ min
	const int min = 2;
#else
	const int min = 0;
#endif
	for (int i = (int)path.size() - 2; i >= min; --i) {
		if (path[i] == path_separator) {
			if (last_segment) {
				*last_segment = path.substr(i + 1, path.size() - i - 2);
			}
			path = path.substr(0, i + 1);
			return true;
		}
	}

	return false;
}

void CLocalPath::AddSegment(std::wstring const& segment)
{
	std::wstring& path = m_path.get();

	assert(!path.empty());
	assert(segment.find(L"/") == std::wstring::npos);
#ifdef FZ_WINDOWS
	assert(segment.find(L"\\") == std::wstring::npos);
#endif

	if (!segment.empty()) {
		path += segment;
		path += path_separator;
	}
}

bool CLocalPath::ChangePath(std::wstring const& new_path)
{
	if (new_path.empty()) {
		return false;
	}

#ifdef FZ_WINDOWS
	if (new_path == L"\\" || new_path == L"/") {
		m_path.get() = L"\\";
		return true;
	}

	if (new_path.size() >= 2 && new_path[0] == '\\' && new_path[1] == '\\') {
		// Absolute UNC
		return SetPath(new_path);
	}
	if (new_path.size() >= 2 && new_path[0] && new_path[1] == ':') {
		// Absolute new_path
		return SetPath(new_path);
	}

	// Relative new_path
	if (m_path->empty()) {
		return false;
	}

	if (new_path.size() >= 2 && (new_path[0] == '\\' || new_path[0] == '/') && m_path->size() > 2 && (*m_path)[1] == ':') {
		// Relative to drive root
		return SetPath(m_path->substr(0, 2) + new_path);
	}
	else {
		// Relative to current directory
		return SetPath(*m_path + new_path);
	}
#else
	if (!new_path.empty() && new_path[0] == path_separator) {
		// Absolute new_path
		return SetPath(new_path);
	}
	else {
		// Relative new_path
		if (m_path->empty()) {
			return false;
		}

		return SetPath(*m_path + new_path);
	}
#endif
}

bool CLocalPath::Exists(std::wstring *error) const
{
	assert(!m_path->empty());
	if (m_path->empty()) {
		return false;
	}

#ifdef FZ_WINDOWS
	if (m_path == L"\\") {
		// List of drives always exists
		return true;
	}

	if ((*m_path)[0] == '\\') {
		// \\server\share\ UNC path

		size_t pos;

		// Search for backslash separating server from share
		for (pos = 3; pos < m_path->size(); ++pos) {
			if ((*m_path)[pos] == '\\') {
				break;
			}
		}
		++pos;
		if (pos >= m_path->size()) {
			// Partial UNC path
			return true;
		}
	}

	std::wstring path = *m_path;
	if (path.size() > 3) {
		path.resize(path.size() - 1);
	}
	DWORD ret = ::GetFileAttributes(path.c_str());
	if (ret == INVALID_FILE_ATTRIBUTES) {
		if (!error) {
			return false;
		}

		*error = fz::sprintf(_("'%s' does not exist or cannot be accessed."), path);

		if ((*m_path)[0] == '\\') {
			return false;
		}

		// Check for removable drive, display a more specific error message in that case
		if (::GetLastError() != ERROR_NOT_READY) {
			return false;
		}
		int type = GetDriveType(m_path->substr(0, 3).c_str());
		if (type == DRIVE_REMOVABLE || type == DRIVE_CDROM) {
			*error = fz::sprintf(_("Cannot access '%s', no media inserted or drive not ready."), path);
		}
		return false;
	}
	else if (!(ret & FILE_ATTRIBUTE_DIRECTORY)) {
		if (error) {
			*error = fz::sprintf(_("'%s' is not a directory."), path);
		}
		return false;
	}

	return true;
#else
	std::string path = fz::to_string(*m_path);
	if (path.size() > 1) {
		path.pop_back();
	}

	struct stat buf;
	int result = stat(path.c_str(), &buf);

	if (!result) {
		if (S_ISDIR(buf.st_mode)) {
			return true;
		}

		if (error) {
			*error = fz::sprintf(_("'%s' is not a directory."), *m_path);
		}

		return false;
	}
	else if (result == ENOTDIR) {
		if (error) {
			*error = fz::sprintf(_("'%s' is not a directory."), *m_path);
		}
		return false;
	}
	else {
		if (error) {
			*error = fz::sprintf(_("'%s' does not exist or cannot be accessed."), *m_path);
		}
		return false;
	}
#endif
}

bool CLocalPath::operator==(const CLocalPath& op) const
{
#ifdef FZ_WINDOWS
	return m_path.is_same(op.m_path) || fz::stricmp(*m_path, *op.m_path) == 0;
#else
	return m_path == op.m_path;
#endif
}

bool CLocalPath::operator!=(const CLocalPath& op) const
{
#ifdef FZ_WINDOWS
	return !m_path.is_same(op.m_path) && fz::stricmp(*m_path, *op.m_path) != 0;
#else
	return m_path != op.m_path;
#endif
}

bool CLocalPath::operator<(CLocalPath const& op) const
{
#ifdef FZ_WINDOWS
	return !m_path.is_same(op.m_path) && fz::stricmp(*m_path, *op.m_path) < 0;
#else
	return m_path < op.m_path;
#endif
}

bool CLocalPath::IsParentOf(const CLocalPath &path) const
{
	if (empty() || path.empty()) {
		return false;
	}

	if (path.m_path->size() < m_path->size()) {
		return false;
	}

#ifdef FZ_WINDOWS
	if (fz::stricmp(*m_path, path.m_path->substr(0, m_path->size())))
		return false;
#else
	if (*m_path != path.m_path->substr(0, m_path->size()))
		return false;
#endif

	return true;
}

bool CLocalPath::IsSubdirOf(const CLocalPath &path) const
{
	if (empty() || path.empty())
		return false;

	if (path.m_path->size() > m_path->size())
		return false;

#ifdef FZ_WINDOWS
	if (fz::stricmp(*path.m_path, m_path->substr(0, path.m_path->size())))
		return false;
#else
	if (*path.m_path != m_path->substr(0, path.m_path->size()))
		return false;
#endif

	return true;
}

std::wstring CLocalPath::GetLastSegment() const
{
	assert(HasParent());

#ifdef FZ_WINDOWS
	// C:\f\ has parent
	// C:\ does not
	// \\x\y\ shortest UNC
	//   ^ min
	const int min = 2;
#else
	const int min = 0;
#endif
	for (int i = (int)m_path->size() - 2; i >= min; i--) {
		if ((*m_path)[i] == path_separator) {
			return m_path->substr(i + 1, m_path->size() - i - 2);
		}
	}

	return std::wstring();
}

bool CLocalPath::Create(CLocalPath *last_successful)
{
	CLocalPath local_path(*this);

	// Only go back as far as needed.
	std::vector<std::wstring> segments;
	while (!local_path.Exists() && local_path.HasParent()) {
		std::wstring segment;
		local_path.MakeParent(&segment);
		segments.emplace_back(std::move(segment));
	}

	bool ret = true;
	for (auto iter = segments.rbegin(); iter != segments.rend(); ++iter) {
		local_path.AddSegment(*iter);

	#ifdef FZ_WINDOWS
		BOOL res = CreateDirectory(local_path.GetPath().c_str(), 0);
		if (!res && GetLastError() != ERROR_ALREADY_EXISTS) {
			ret = false;
			break;
		}
	#else
		fz::native_string s = fz::to_native(local_path.GetPath());
		if (s.empty()) {
			ret = false;
			break;
		}

		int res = mkdir(s.c_str(), 0777);
		if (res && errno != EEXIST) {
			ret = false;
			break;
		}
	#endif
		if (last_successful) {
			*last_successful = local_path;
		}
	}

	return ret;
}
