#include "fz_string.hpp"

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

}
