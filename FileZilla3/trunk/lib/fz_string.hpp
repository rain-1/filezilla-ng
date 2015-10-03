#ifndef LIBFILEZILLA_STRING_HEADER
#define LIBFILEZILLA_STRING_HEADER

#include "libfilezilla.hpp"

#include <string>

namespace fz {

// Declare a string class with the native character encoding.
// Note: On non-Windows builds a UTF-8 locale _MUST_ be used.
//       Use op any locale results in undefined behavior.
#ifdef FZ_WINDOWS
	typedef std::wstring native_string;
#else
	typedef std::string native_string;
#endif

// Locale-sensitive stricmp
// Note: does not handle embedded null
int stricmp(std::string const& a, std::string const& b);
int stricmp(std::wstring const& a, std::wstring const& b);

// Under some locales there is a different case-relationship 
// between the letters a-z and A-Z as one expects from ASCII.
// In Turkish for example there are different variations of the letter i,
// namely dotted and dotless. What we see as 'i' is the lowercase dotted i and 
// 'I' is the  uppercase dotless i. Since std::tolower is locale-aware, I would 
// become the dotless lowercase i.
//
// This is not always what we want. FTP commands for example are case-insensitive
// ASCII strings, LIST and list are the same.
//
// tolower_ascii instead converts all types of 'i's to the ASCII i as well.
template<typename Char>
Char tolower_ascii(Char c) {
	if (c >= 'A' && c <= 'Z') {
		return c + ('a' - 'A');
	}
	return c;
}

template<>
std::wstring::value_type tolower_ascii(std::wstring::value_type c);

// str_tolower_ascii does for strings what tolower_ascii does for indivudial characters
// Note: For UTF-8 strings it works on individual octets!
template<typename String>
String str_tolower_ascii(String const& s)
{
	String ret = s;
	for (auto& c : ret) {
		c = tolower_ascii(c);
	}
	return ret;
}

// Converts from system encoding into wstring
// Does not handle embedded nulls
std::wstring to_wstring(std::string const& in);

// Converts from UTF-8 into wstring
// Undefined behavior if input string is not valid UTF-8.
std::wstring to_wstring_from_utf8(std::string const& in);

}

#ifndef fzT
#ifdef FZ_WINDOWS
#define fzT(x) L ## x
#else
#define fzT(X) x
#endif
#endif

#endif
