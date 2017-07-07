#ifndef FILEZILLA_INCLUDE_HTTPHEADERS_HEADER
#define FILEZILLA_INCLUDE_HTTPHEADERS_HEADER

#include <map>
#include <libfilezilla/string.hpp>

struct HeaderCmp
{
	template<typename T>
	bool operator()(T const& lhs, T const& rhs) const {
		return fz::str_tolower_ascii(lhs) < fz::str_tolower_ascii(rhs);
	}
};

typedef std::map<std::string, std::string, HeaderCmp> HttpHeaders;
#endif // ndef FILEZILLA_INCLUDE_HTTPHEADERS_HEADER
