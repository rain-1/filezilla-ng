#ifndef FILEZILLA_ENGINE_FZSTRING_HEADER
#define FILEZILLA_ENGINE_FZSTRING_HEADER

#include "libfilezilla_engine.h"

inline std::wstring to_wstring(wxString const& s) { return s.ToStdWstring(); }

namespace fz {
template<>
inline wxString str_tolower_ascii(wxString const& s)
{
	wxString ret = s;
	for (auto& c : ret) {
		c = tolower_ascii(static_cast<wxChar>(c));
	}
	return ret;
}
}

#endif
