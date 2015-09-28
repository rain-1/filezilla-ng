#include "fz_string.hpp"

#ifdef FZ_WINDOWS
#include <string.h>

#include "private/windows.hpp"
#else
#include <strings.h>
#endif

#include <cstdlib>

static_assert('a' + 25 == 'z', "We only support systems running with an ASCII-based character set. Sorry, no EBCDIC.");

// char may be unsigned, yielding stange results if subtracting characters. To work around it, expect a particular order of characters.
static_assert('A' < 'a', "We only support systems running with an ASCII-based character set. Sorry, no EBCDIC.");

namespace fz {

int stricmp(std::string const& a, std::string const& b)
{
#ifdef FZ_WINDOWS
	return _stricmp(a.c_str(), b.c_str());
#else
	return strcasecmp(a.c_str(), b.c_str());
#endif
}

int stricmp(std::wstring const& a, std::wstring const& b)
{
#ifdef FZ_WINDOWS
	return _wcsicmp(a.c_str(), b.c_str());
#else
	return wcscasecmp(a.c_str(), b.c_str());
#endif
}

template<>
std::wstring::value_type tolower_ascii(std::wstring::value_type c)
{
	if (c >= 'A' && c <= 'Z') {
		return c + ('a' - 'A');
	}
	else if (c == 0x130 || c == 0x131) {
		c = 'i';
	}
	return c;
}

std::wstring to_wstring(std::string const& in)
{
	std::mbstate_t ps{};
	std::wstring ret;

	char const* in_p = in.c_str();
	size_t len = std::mbsrtowcs(0, &in_p, 0, &ps);
	if (len != static_cast<size_t>(-1)) {
		ret.resize(len);
		wchar_t* out_p = &ret[0];

		std::mbsrtowcs(out_p, &in_p, len + 1, &ps);
	}

	return ret;
}

// Converts from UTF-8 into wstring
// Undefined behavior if input string is not valid UTF-8.
// Does not handle embedded nulls
std::wstring to_wstring_from_utf8(std::string const& in)
{
	std::wstring ret;

#if FZ_WINDOWS
	char const* const in_p = in.c_str();
	int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in_p, in.size(), 0, 0);
	if (len > 0) {
		ret.resize(len);
		wchar_t* out_p = &ret[0];
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in_p, len, out_p, len);
	}
#else
#error "Not implemented"
#endif

	return ret;
}

}
