#include "fz_string.hpp"

#ifdef FZ_WINDOWS
#include <string.h>

#include "private/windows.hpp"
#else
#include <iconv.h>
#include <strings.h>
#endif

#include <cstdlib>

static_assert('a' + 25 == 'z', "We only support systems running with an ASCII-based character set. Sorry, no EBCDIC.");

// char may be unsigned, yielding stange results if subtracting characters. To work around it, expect a particular order of characters.
static_assert('A' < 'a', "We only support systems running with an ASCII-based character set. Sorry, no EBCDIC.");

namespace fz {

#ifdef FZ_WINDOWS
native_string to_native(std::string const& in)
{
	return to_wstring(in);
}

native_string to_native(std::wstring const& in)
{
	return in;
}
#else
native_string to_native(std::string const& in)
{
	return in;
}

native_string to_native(std::wstring const& in)
{
	return to_string(in);
}
#endif

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
std::wstring to_wstring_from_utf8(std::string const& in)
{
	std::wstring ret;

	if (!in.empty()) {
#if FZ_WINDOWS
		char const* const in_p = in.c_str();
		int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in_p, in.size(), 0, 0);
		if (len > 0) {
			ret.resize(len);
			wchar_t* out_p = &ret[0];
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in_p, len, out_p, len);
		}
#else
		iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
		if (cd != reinterpret_cast<iconv_t>(-1)) {
			char * in_p = const_cast<char*>(in.c_str());
			size_t in_len = in.size();

			size_t out_len = in_len * sizeof(wchar_t) * 2;
			char* out_buf = new char[out_len];
			char* out_p = out_buf;

			size_t r = iconv(cd, &in_p, &in_len, &out_p, &out_len);

			if (r != static_cast<size_t>(-1)) {
				ret.assign(reinterpret_cast<wchar_t*>(out_buf), reinterpret_cast<wchar_t*>(out_p));
			}

			// Our buffer should big enough as well, so we can ignore errors such as E2BIG.

			delete [] out_buf;

			iconv_close(cd);
		}
#endif
	}

	return ret;
}

std::string to_string(std::wstring const& in)
{
	std::mbstate_t ps{};
	std::string ret;

	wchar_t const* in_p = in.c_str();
	size_t len = std::wcsrtombs(0, &in_p, 0, &ps);
	if (len != static_cast<size_t>(-1)) {
		ret.resize(len);
		char* out_p = &ret[0];

		std::wcsrtombs(out_p, &in_p, len + 1, &ps);
	}

	return ret;
}

}
