#ifndef FILEZILLA_ENGINE_FZSTRING_HEADER
#define FILEZILLA_ENGINE_FZSTRING_HEADER

#include "libfilezilla_engine.h"

#include <string>

typedef std::wstring fzstring;

inline fzstring to_fzstring(wxString const& s) { return s.ToStdWstring(); }
#ifdef __WXMSW__
inline int collate_fzstring(fzstring const& a, fzstring const& b) { return _wcsicmp(a.c_str(), b.c_str()); } // note: does not handle embedded null
#else
inline int collate_fzstring(fzstring const& a, fzstring const& b) { return wcscasecmp(a.c_str(), b.c_str()); } // note: does not handle embedded null
#endif

#endif
