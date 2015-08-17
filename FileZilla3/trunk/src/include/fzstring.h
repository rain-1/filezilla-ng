#ifndef FILEZILLA_ENGINE_FZSTRING_HEADER
#define FILEZILLA_ENGINE_FZSTRING_HEADER

#include "libfilezilla.h"

#include <string>

#ifdef __WXMSW__
typedef std::wstring fzstring;

inline fzstring to_fzstring(wxString const& s) { return s.ToStdWstring(); }
inline int collate_fzstring(fzstring const& a, fzstring const& b) { return _wcsicmp(a.c_str(), b.c_str()); } // note: does not handle embedded null
#else
typedef std::string fzstring;

inline fzstring to_fzstring(wxString const& s) { return s.ToStdString();  }
inline int collate_fzstring(fzstring const& a, fzstring const& b) { return strcasecmp(a.c_str(), b.c_str()); } // note: does not handle embedded null
#endif

#endif
