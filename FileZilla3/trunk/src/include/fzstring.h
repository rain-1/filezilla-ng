#ifndef FILEZILLA_ENGINE_STRING_HEADER
#define FILEZILLA_ENGINE_STRING_HEADER

#include "libfilezilla.h"

#include <string>

#ifdef __WXMSW__
typedef std::wstring fzstring;
#else
typedef std::string fzstring;
#endif

#endif
