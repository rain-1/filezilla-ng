#include <filezilla.h>
#include "serverpath.h"

#define FTP_MVS_DOUBLE_QUOTE (wchar_t)0xDC

struct CServerTypeTraits
{
	wchar_t const* separators;
	bool has_root; // Root = simply separator nothing else
	wchar_t left_enclosure; // Example: VMS paths: [FOO.BAR]
	wchar_t right_enclosure;
	bool filename_inside_enclosure; // MVS
	int prefixmode; //0 = normal prefix, 1 = suffix
	wchar_t separatorEscape;
	bool has_dots; // Special meaning for .. (parent) and . (self)
	bool separator_after_prefix;
};

static const CServerTypeTraits traits[SERVERTYPE_MAX] = {
	{ L"/",   true,     0,    0,    false, 0, 0,   true,  false }, // Failsafe
	{ L"/",   true,     0,    0,    false, 0, 0,   true,  false },
	{ L".",   false,  '[',  ']',    false, 0, '^', false, false },
	{ L"\\/", false,    0,    0,    false, 0, 0,   true,  false }, // DOS with backslashes
	{ L".",   false, '\'', '\'',     true, 1, 0,   false, false },
	{ L"/",   true,     0,    0,    false, 0, 0,   true,  false },
	{ L"/",   true,     0,    0,    false, 0, 0,   true,  false }, // Same as Unix
	{ L".",   false,    0,    0,    false, 0, 0,   false, false },
	{ L"\\",  true,     0,    0,    false, 0, 0,   true,  false },
	{ L"/",   true,     0,    0,    false, 0, 0,   true,  true  },  // Cygwin is like Unix but has optional prefix of form "//server"
	{ L"/\\", false,    0,    0,    false, 0, 0,   true,  false } // DOS with forwardslashes
};

bool CServerPathData::operator==(CServerPathData const& cmp) const
{
	if (m_prefix != cmp.m_prefix) {
		return false;
	}

	if (m_segments != cmp.m_segments) {
		return false;
	}

	return true;
}

CServerPath::CServerPath()
	: m_type(DEFAULT)
{
}

CServerPath::CServerPath(CServerPath const& path, std::wstring subdir)
	: m_type(path.m_type)
	, m_data(path.m_data)
{
	if (subdir.empty()) {
		return;
	}

	if (!ChangePath(subdir)) {
		clear();
	}
}

CServerPath::CServerPath(std::wstring const& path, ServerType type)
	: m_type(type)
{
	SetPath(path);
}

void CServerPath::clear()
{
	m_data.clear();
}

bool CServerPath::SetPath(std::wstring newPath)
{
	return SetPath(newPath, false);
}

bool CServerPath::SetPath(std::wstring &newPath, bool isFile)
{
	std::wstring path = newPath;

	if (path.empty()) {
		return false;
	}

	if (m_type == DEFAULT) {
		size_t pos1 = path.find(L":[");
		if (pos1 != std::wstring::npos) {
			size_t pos2 = path.rfind(']');
			if (pos2 != std::string::npos && pos2 == (path.size() - 1) && !isFile) {
				m_type = VMS;
			}
			else if (isFile && pos2 > pos1) {
				m_type = VMS;
			}
		}
		else if (path.size() >= 3 &&
			((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
			path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
		{
			m_type = DOS;
		}
		else if (path[0] == FTP_MVS_DOUBLE_QUOTE && path.back() == FTP_MVS_DOUBLE_QUOTE) {
			m_type = MVS;
		}
		else if (path[0] == ':' && (pos1 = path.find(':'), 2) != std::wstring::npos) {
			size_t slash = path.find('/');
			if (slash == std::wstring::npos || slash > pos1) {
				m_type = VXWORKS;
			}
		}
		else if (path[0] == '\\') {
			m_type = DOS_VIRTUAL;
		}

		if (m_type == DEFAULT) {
			m_type = UNIX;
		}
	}

	m_data.clear();

	if (!ChangePath(path, isFile)) {
		return false;
	}

	if (isFile) {
		newPath = path;
	}
	return true;
}

std::wstring CServerPath::GetPath() const
{
	if (empty()) {
		return std::wstring();
	}

	std::wstring path;

	if (!traits[m_type].prefixmode && m_data->m_prefix) {
		path = *m_data->m_prefix;
	}

	if (traits[m_type].left_enclosure != 0) {
		path += traits[m_type].left_enclosure;
	}
	if (m_data->m_segments.empty() && (!traits[m_type].has_root || !m_data->m_prefix || traits[m_type].separator_after_prefix)) {
		path += traits[m_type].separators[0];
	}

	for (auto iter = m_data->m_segments.cbegin(); iter != m_data->m_segments.cend(); ++iter) {
		std::wstring const& segment = *iter;
		if (iter != m_data->m_segments.cbegin()) {
			path += traits[m_type].separators[0];
		}
		else if (traits[m_type].has_root) {
			if (!m_data->m_prefix || traits[m_type].separator_after_prefix) {
				path += traits[m_type].separators[0];
			}
		}

		if (traits[m_type].separatorEscape) {
			std::wstring tmp = segment;
			EscapeSeparators(m_type, tmp);
			path += tmp;
		}
		else {
			path += segment;
		}
	}

	if (traits[m_type].prefixmode && m_data->m_prefix) {
		path += *m_data->m_prefix;
	}

	if (traits[m_type].right_enclosure != 0) {
		path += traits[m_type].right_enclosure;
	}

	// DOS is strange.
	// C: is current working dir on drive C, C:\ the drive root.
	if ((m_type == DOS || m_type == DOS_FWD_SLASHES) && m_data->m_segments.size() == 1) {
		path += traits[m_type].separators[0];
	}

	return path;
}

bool CServerPath::HasParent() const
{
	if (empty()) {
		return false;
	}

	if (!traits[m_type].has_root) {
		return m_data->m_segments.size() > 1;
	}

	return !m_data->m_segments.empty();
}

CServerPath CServerPath::GetParent() const
{
	if (empty() || !HasParent()) {
		return CServerPath();
	}

	CServerPath parent(*this);
	CServerPathData& parent_data = parent.m_data.get();

	parent_data.m_segments.pop_back();

	if (m_type == MVS) {
		parent_data.m_prefix = fz::sparse_optional<std::wstring>(L".");
	}

	return parent;
}

std::wstring CServerPath::GetLastSegment() const
{
	if (empty() || !HasParent()) {
		return std::wstring();
	}

	if (!m_data->m_segments.empty()) {
		return m_data->m_segments.back();
	}
	else {
		return std::wstring();
	}
}

// libc sprintf can be so slow at times...
wchar_t* fast_sprint_number(wchar_t* s, size_t n)
{
	wchar_t tmp[20]; // Long enough for 2^64-1

	wchar_t* c = tmp;
	do {
		*(c++) = n % 10 + '0';
		n /= 10;
	} while( n > 0 );

	do {
		*(s++) = *(--c);
	} while (c != tmp);

	return s;
}

#define tstrcpy wcscpy

std::wstring CServerPath::GetSafePath() const
{
	if (empty()) {
		return std::wstring();
	}

	#define INTLENGTH 20 // 2^64 - 1

	int len = 5 // Type and 2x' ' and terminating 0
		+ INTLENGTH; // Max length of prefix

	len += m_data->m_prefix ? m_data->m_prefix->size() : 0;
	for (auto const& segment : m_data->m_segments) {
		len += segment.size() + 2 + INTLENGTH;
	}

	std::wstring safepath;
	safepath.resize(len);
	wchar_t * const start = &safepath[0];
	wchar_t * t = &safepath[0];

	t = fast_sprint_number(t, m_type);
	*(t++) = ' ';
	t = fast_sprint_number(t, m_data->m_prefix ? m_data->m_prefix->size() : 0);

	if (m_data->m_prefix) {
		*(t++) = ' ';
		tstrcpy(t, m_data->m_prefix->c_str());
		t += m_data->m_prefix->size();
	}

	for (auto const& segment : m_data->m_segments) {
		*(t++) = ' ';
		t = fast_sprint_number(t, segment.size());
		*(t++) = ' ';
		tstrcpy(t, segment.c_str());
		t += segment.size();
	}
	safepath.resize(t - start);
	safepath.shrink_to_fit();

	return safepath;
}

bool CServerPath::SetSafePath(std::wstring const& path)
{
	bool const ret = DoSetSafePath(path);
	if (!ret) {
		clear();
	}

	return ret;
}

bool CServerPath::DoSetSafePath(std::wstring const& path)
{
	CServerPathData& data = m_data.get();
	data.m_prefix.clear();
	data.m_segments.clear();

	// Optimized for speed, avoid expensive wxString functions
	// Before the optimization this function was responsible for
	// most CPU cycles used during loading of transfer queues
	// from file
	wchar_t const* const begin = path.c_str();
	wchar_t const* const end = begin + path.size();

	wchar_t const* p = begin;

	int type = 0;
	do {
		if (*p < '0' || *p > '9') {
			return false;
		}
		type *= 10;
		type += *p - '0';

		if (type >= SERVERTYPE_MAX) {
			return false;
		}
		++p;
	} while (*p != ' ');

	m_type = static_cast<ServerType>(type);
	++p;

	int prefix_len = 0;
	do {
		if (*p < '0' || *p > '9') {
			return false;
		}
		prefix_len *= 10;
		prefix_len += *p - '0';

		if (prefix_len > 32767) // Should be sane enough
			return false;
		++p;
	}
	while (*p && *p != ' ');

	if (!*p) {
		if (prefix_len != 0) {
			return false;
		}
		else {
			// Is root directory, like / on unix like systems.
			return true;
		}
	}

	++p;

	if (prefix_len > end - p) {
		return false;
	}
	if (prefix_len) {
		data.m_prefix = fz::sparse_optional<std::wstring>(new std::wstring(p, p + prefix_len));
		p += prefix_len + 1;
	}

	while (p < end) {
		int segment_len = 0;
		do {
			if (*p < '0' || *p > '9') {
				return false;
			}
			segment_len *= 10;
			segment_len += *p - '0';

			if (segment_len > 32767) { // Should be sane enough
				return false;
			}
			++p;
		}
		while (*p != ' ');

		if (!segment_len) {
			return false;
		}
		++p;

		if (segment_len > end - p) {
			return false;
		}
		data.m_segments.emplace_back(p, p + segment_len);

		p += segment_len + 1;
	}

	return true;
}

bool CServerPath::SetType(ServerType type)
{
	if (!empty() && m_type != DEFAULT) {
		return false;
	}

	m_type = type;

	return true;
}

ServerType CServerPath::GetType() const
{
	return m_type;
}

bool CServerPath::IsSubdirOf(const CServerPath &path, bool cmpNoCase) const
{
	if (empty() || path.empty()) {
		return false;
	}

	if (m_type != path.m_type) {
		return false;
	}

	if (!HasParent()) {
		return false;
	}

	if (traits[m_type].prefixmode != 1) {
		if (cmpNoCase ) {
			if( m_data->m_prefix && !path.m_data->m_prefix ) {
				return false;
			}
			else if( !m_data->m_prefix && path.m_data->m_prefix ) {
				return false;
			}
			else if( m_data->m_prefix && path.m_data->m_prefix && fz::stricmp(*m_data->m_prefix, *path.m_data->m_prefix) ) {
				return false;
			}
		}
		if (!cmpNoCase && m_data->m_prefix != path.m_data->m_prefix)
			return false;
	}

	// On MVS, dirs like 'FOO.BAR' without trailing dot cannot have
	// subdirectories
	if (traits[m_type].prefixmode == 1 && !path.m_data->m_prefix)
		return false;

	tConstSegmentIter iter1 = m_data->m_segments.begin();
	tConstSegmentIter iter2 = path.m_data->m_segments.begin();
	while (iter1 != m_data->m_segments.end()) {
		if (iter2 == path.m_data->m_segments.end()) {
			return true;
		}
		if (cmpNoCase) {
			if (fz::stricmp(*iter1, *iter2)) {
				return false;
			}
		}
		else if (*iter1 != *iter2) {
			return false;
		}

		++iter1;
		++iter2;
	}

	return false;
}

bool CServerPath::IsParentOf(const CServerPath &path, bool cmpNoCase) const
{
	return path.IsSubdirOf(*this, cmpNoCase);
}

bool CServerPath::ChangePath(std::wstring const& subdir)
{
	std::wstring subdir2 = subdir;
	return ChangePath(subdir2, false);
}

bool CServerPath::ChangePath(std::wstring &subdir, bool isFile)
{
	bool ret = DoChangePath(subdir, isFile);
	if (!ret) {
		clear();
	}

	return ret;
}

bool CServerPath::DoChangePath(std::wstring &subdir, bool isFile)
{
	std::wstring dir = subdir;
	std::wstring file;

	if (dir.empty()) {
		if (empty() || isFile) {
			return false;
		}
		else {
			return true;
		}
	}

	bool const was_empty = empty();
	CServerPathData& data = m_data.get();

	switch (m_type)
	{
	case VMS:
		{
			size_t pos1 = dir.find(traits[m_type].left_enclosure);
			if (pos1 == std::wstring::npos) {
				size_t pos2 = dir.rfind(traits[m_type].right_enclosure);
				if (pos2 != std::wstring::npos) {
					return false;
				}

				if (isFile) {
					if (was_empty) {
						return false;
					}

					file = dir;
					break;
				}
			}
			else {
				size_t pos2 = dir.rfind(traits[m_type].right_enclosure);
				if (pos2 == std::wstring::npos || pos2 <= pos1 + 1) {
					return false;
				}

				bool enclosure_is_last = pos2 == (dir.size() - 1);
				if (isFile == enclosure_is_last) {
					return false;
				}

				if (isFile) {
					file = dir.substr(pos2 + 1);
				}
				dir = dir.substr(0, pos2);

				if (pos1) {
					data.m_prefix = fz::sparse_optional<std::wstring>(dir.substr(0, pos1));
				}
				dir = dir.substr(pos1 + 1);

				data.m_segments.clear();
			}

			if (!Segmentize(dir, data.m_segments)) {
				return false;
			}
			if (data.m_segments.empty() && was_empty) {
				return false;
			}
		}
		break;
	case DOS:
	case DOS_FWD_SLASHES:
		{
			bool is_absolute = false;
			size_t sep = dir.find_first_of(traits[m_type].separators);
			if (sep == std::wstring::npos) {
				sep = dir.size();
			}
			size_t colon = dir.find(':');
			if (colon != std::wstring::npos && colon > 0 && colon == sep - 1) {
				is_absolute = true;
			}

			if (is_absolute) {
				data.m_segments.clear();
			}
			else if (IsSeparator(dir[0])) {
				// Drive-relative path
				if (data.m_segments.empty()) {
					return false;
				}
				std::wstring first = data.m_segments.front();
				data.m_segments.clear();
				data.m_segments.push_back(first);
				dir = dir.substr(1);
			}
			// else: Any other relative path

			if (isFile && !ExtractFile(dir, file)) {
				return false;
			}

			if (!Segmentize(dir, data.m_segments)) {
				return false;
			}
			if (data.m_segments.empty() && was_empty) {
				return false;
			}
		}
		break;
	case MVS:
		{
			// Remove the double quoation some servers send in PWD reply
			size_t i = 0;
			wchar_t c = dir[i];
			while (c == FTP_MVS_DOUBLE_QUOTE) {
				c = dir[++i];
			}
			dir.erase(0, i);

			while (!dir.empty()) {
				c = dir.back();
				if (c != FTP_MVS_DOUBLE_QUOTE) {
					break;
				}
				else {
					dir.pop_back();
				}
			}
		}
		if (dir.empty()) {
			return false;
		}

		if (dir[0] == traits[m_type].left_enclosure) {
			if (dir.back() != traits[m_type].right_enclosure) {
				return false;
			}

			dir = dir.substr(1, dir.size() - 2);

			data.m_segments.clear();
		}
		else if (dir.back() == traits[m_type].right_enclosure) {
			return false;
		}
		else if (was_empty) {
			return false;
		}

		if (!dir.empty() && dir.back() == ')') {
			// Partitioned dataset member
			if (!isFile) {
				return false;
			}

			size_t pos = dir.find('(');
			if (pos == std::wstring::npos) {
				return false;
			}
			dir.pop_back();
			file = dir.substr(pos + 1);
			dir = dir.substr(0, pos);

			if (!was_empty && !data.m_prefix && !dir.empty()) {
				return false;
			}

			data.m_prefix.clear();
		}
		else {
			if (!was_empty && !data.m_prefix) {
				if (dir.find('.') != std::wstring::npos || !isFile) {
					return false;
				}
			}

			if (isFile) {
				if (!ExtractFile(dir, file)) {
					return false;
				}
				data.m_prefix = fz::sparse_optional<std::wstring>(L".");
			}
			else if (!dir.empty() && dir.back() == '.') {
				data.m_prefix = fz::sparse_optional<std::wstring>(L".");
			}
			else {
				data.m_prefix.clear();
			}
		}

		if (!Segmentize(dir, data.m_segments)) {
			return false;
		}
		break;
	case HPNONSTOP:
		if (dir[0] == '\\') {
			data.m_segments.clear();
		}

		if (isFile && !ExtractFile(dir, file)) {
			return false;
		}

		if (!Segmentize(dir, data.m_segments)) {
			return false;
		}
		if (data.m_segments.empty() && was_empty) {
			return false;
		}

		break;
	case VXWORKS:
		{
			if (dir[0] != ':') {
				if (was_empty) {
					return false;
				}
			}
			else {
				size_t colon2 =  dir.find(':', 1);
				if (colon2 == std::wstring::npos || colon2 == 1) {
					return false;
				}
				data.m_prefix = fz::sparse_optional<std::wstring>(dir.substr(0, colon2 + 1));
				dir = dir.substr(colon2 + 1);

				data.m_segments.clear();
			}

			if (isFile && !ExtractFile(dir, file)) {
				return false;
			}

			if (!Segmentize(dir, data.m_segments)) {
				return false;
			}
		}
		break;
	case CYGWIN:
		{
			if (IsSeparator(dir[0])) {
				data.m_segments.clear();
				data.m_prefix.clear();
			}
			else if (was_empty) {
				return false;
			}
			if (dir[0] == '/' && dir[1] == '/') {
				data.m_prefix = fz::sparse_optional<std::wstring>(std::wstring(1, traits[m_type].separators[0]));
				dir = dir.substr(1);
			}

			if (isFile && !ExtractFile(dir, file)) {
				return false;
			}

			if (!Segmentize(dir, data.m_segments)) {
				return false;
			}
		}
		break;
	default:
		{
			if (IsSeparator(dir[0])) {
				data.m_segments.clear();
			}
			else if (was_empty) {
				return false;
			}

			if (isFile && !ExtractFile(dir, file)) {
				return false;
			}

			if (!Segmentize(dir, data.m_segments)) {
				return false;
			}
		}
		break;
	}

	if (!traits[m_type].has_root && data.m_segments.empty()) {
		return false;
	}

	if (isFile) {
		if (traits[m_type].has_dots) {
			if (file == L".." || file == L".") {
				return false;
			}
		}
		subdir = file;
	}

	return true;
}

bool CServerPath::operator==(const CServerPath &op) const
{
	if (empty() != op.empty()) {
		return false;
	}
	else if (m_type != op.m_type) {
		return false;
	}
	else if (m_data != op.m_data) {
		return false;
	}

	return true;
}

bool CServerPath::operator!=(const CServerPath &op) const
{
	return !(*this == op);
}

bool CServerPath::operator<(const CServerPath &op) const
{
	if (empty()) {
		return !op.empty();
	}
	else if (op.empty()) {
		return false;
	}

	if (m_data->m_prefix || op.m_data->m_prefix) {
		if (m_data->m_prefix < op.m_data->m_prefix) {
			return true;
		}
		else if (op.m_data->m_prefix < m_data->m_prefix) {
			return false;
		}
	}

	if (m_type > op.m_type) {
		return false;
	}
	else if (m_type < op.m_type) {
		return true;
	}

	tConstSegmentIter iter1, iter2;
	for (iter1 = m_data->m_segments.begin(), iter2 = op.m_data->m_segments.begin(); iter1 != m_data->m_segments.end(); ++iter1, ++iter2) {
		if (iter2 == op.m_data->m_segments.end()) {
			return false;
		}

		const int cmp = std::wcscmp(iter1->c_str(), iter2->c_str());
		if (cmp < 0) {
			return true;
		}
		if (cmp > 0) {
			return false;
		}
	}

	return iter2 != op.m_data->m_segments.end();
}

std::wstring CServerPath::FormatFilename(std::wstring const& filename, bool omitPath) const
{
	if (empty() || filename.empty()) {
		return filename;
	}

	if (omitPath && (!traits[m_type].prefixmode || (m_data->m_prefix && *m_data->m_prefix == L"."))) {
		return filename;
	}

	std::wstring result = GetPath();
	if (traits[m_type].left_enclosure && traits[m_type].filename_inside_enclosure) {
		result.pop_back();
	}

	switch (m_type) {
		case VXWORKS:
			if (!result.empty() && !IsSeparator(result.back()) && !m_data->m_segments.empty()) {
				result += traits[m_type].separators[0];
			}
			break;
		case VMS:
		case MVS:
			break;
		default:
			if (!result.empty() && !IsSeparator(result.back())) {
				result += traits[m_type].separators[0];
			}
			break;
	}

	if (traits[m_type].prefixmode == 1 && !m_data->m_prefix) {
		result += L"(" + filename + L")";
	}
	else {
		result += filename;
	}

	if (traits[m_type].left_enclosure && traits[m_type].filename_inside_enclosure) {
		result += traits[m_type].right_enclosure;
	}

	return result;
}

int CServerPath::CmpNoCase(const CServerPath &op) const
{
	if (empty() != op.empty())
		return 1;
	else if (m_data->m_prefix != op.m_data->m_prefix)
		return 1;
	else if (m_type != op.m_type)
		return 1;

	if (m_data->m_segments.size() > op.m_data->m_segments.size())
		return 1;
	else if (m_data->m_segments.size() < op.m_data->m_segments.size())
		return -1;

	tConstSegmentIter iter = m_data->m_segments.begin();
	tConstSegmentIter iter2 = op.m_data->m_segments.begin();
	while (iter != m_data->m_segments.end()) {
		int res = fz::stricmp(*(iter++), *(iter2++));
		if (res) {
			return res;
		}
	}

	return 0;
}

bool CServerPath::AddSegment(std::wstring const& segment)
{
	if (empty()) {
		return false;
	}

	// TODO: Check for invalid characters
	m_data.get().m_segments.push_back(segment);

	return true;
}

CServerPath CServerPath::GetCommonParent(const CServerPath& path) const
{
	if (*this == path) {
		return *this;
	}

	if (empty() || path.empty()) {
		return CServerPath();
	}

	if (m_type != path.m_type ||
		(!traits[m_type].prefixmode && m_data->m_prefix != path.m_data->m_prefix))
	{
		return CServerPath();
	}

	if (!HasParent()) {
		if (path.IsSubdirOf(*this, false)) {
			return *this;
		}
		else {
			return CServerPath();
		}
	}
	else if (!path.HasParent()) {
		if (IsSubdirOf(path, false)) {
			return path;
		}
		else {
			return CServerPath();
		}
	}

	CServerPath parent;
	parent.m_type = m_type;

	CServerPathData& parentData = parent.m_data.get();

	tConstSegmentIter last = m_data->m_segments.end();
	tConstSegmentIter last2 = path.m_data->m_segments.end();
	if (traits[m_type].prefixmode == 1) {
		if (!m_data->m_prefix) {
			--last;
		}
		if (!path.m_data->m_prefix) {
			--last2;
		}
		parentData.m_prefix = GetParent().m_data->m_prefix;
	}
	else
		parentData.m_prefix = m_data->m_prefix;

	tConstSegmentIter iter = m_data->m_segments.begin();
	tConstSegmentIter iter2 = path.m_data->m_segments.begin();
	while (iter != last && iter2 != last2) {
		if (*iter != *iter2) {
			if (!traits[m_type].has_root && parentData.m_segments.empty()) {
				return CServerPath();
			}
			else {
				return parent;
			}
		}

		parentData.m_segments.push_back(*iter);

		++iter;
		++iter2;
	}

	return parent;
}

std::wstring CServerPath::FormatSubdir(std::wstring const& subdir) const
{
	if (!traits[m_type].separatorEscape) {
		return subdir;
	}

	std::wstring res = subdir;
	EscapeSeparators(m_type, res);

	return res;
}

bool CServerPath::SegmentizeAddSegment(std::wstring & segment, tSegmentList& segments, bool& append)
{
	if (traits[m_type].has_dots) {
		if (segment == L".") {
			return true;
		}
		else if (segment == L"..") {
			if (segments.empty()) {
				return false;
			}
			else {
				segments.pop_back();
				return true;
			}
		}
	}

	bool append_next = false;
	if (!segment.empty() && traits[m_type].separatorEscape && segment.back() == traits[m_type].separatorEscape) {
		append_next = true;
		segment[segment.size() - 1] = traits[m_type].separators[0];
	}

	if (append) {
		segments.back() += segment;
	}
	else {
		segments.push_back(std::move(segment));
	}

	append = append_next;

	return true;
}

bool CServerPath::Segmentize(std::wstring const& str, tSegmentList& segments)
{
	bool append = false;
	size_t start = 0;

	size_t pos;
	while (true) {
		pos = str.find_first_of(traits[m_type].separators, start);
		if (pos == std::string::npos) {
			break;
		}
		if (start == pos) {
			++start;
			continue;
		}

		std::wstring segment = str.substr(start, pos - start);
		start = pos + 1;

		if (!SegmentizeAddSegment(segment, segments, append)) {
			return false;
		}
	}

	if (start < str.size()) {
		std::wstring segment = str.substr(start);
		if (!SegmentizeAddSegment(segment, segments, append)) {
			return false;
		}
	}

	if (append)
		return false;

	return true;
}

bool CServerPath::ExtractFile(std::wstring& dir, std::wstring& file)
{
	size_t pos = dir.find_last_of(traits[m_type].separators);
	if (pos != std::wstring::npos && pos == dir.size() - 1) {
		return false;
	}

	if (pos == std::wstring::npos) {
		file = dir;
		dir.clear();
		return true;
	}

	file = dir.substr(pos + 1);
	dir = dir.substr(0, pos + 1);

	return true;
}

void CServerPath::EscapeSeparators(ServerType type, std::wstring& subdir)
{
	if (traits[type].separatorEscape) {
		for (wchar_t const* p = traits[type].separators; *p; ++p) {
			fz::replace_substrings(subdir, std::wstring(1, *p), std::wstring(1, traits[type].separatorEscape) + traits[type].separators[0]);
		}
	}
}

size_t CServerPath::SegmentCount() const
{
	return empty() ? 0 : m_data->m_segments.size();
}

bool CServerPath::IsSeparator(wchar_t c) const
{
	for (wchar_t const* ref = traits[m_type].separators; *ref; ++ref) {
		if (*ref == c) {
			return true;
		}
	}

	return false;
}
