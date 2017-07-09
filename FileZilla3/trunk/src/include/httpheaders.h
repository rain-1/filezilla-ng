#ifndef FILEZILLA_INCLUDE_HTTPHEADERS_HEADER
#define FILEZILLA_INCLUDE_HTTPHEADERS_HEADER

#include <map>
#include <libfilezilla/string.hpp>

typedef std::map<std::string, std::string, fz::less_insensitive_ascii> HttpHeaders;

#endif
