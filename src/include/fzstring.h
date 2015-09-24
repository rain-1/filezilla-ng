#ifndef FILEZILLA_ENGINE_FZSTRING_HEADER
#define FILEZILLA_ENGINE_FZSTRING_HEADER

#include "libfilezilla_engine.h"

inline std::wstring to_wstring(wxString const& s) { return s.ToStdWstring(); }

namespace fz {
template<>
inline wxString str_tolower_ascii(wxString const& s)
{
	wxString ret = s;
	// wxString is just broken, can't even use range-based for loops with it.
	for (auto it = ret.begin(); it != ret.end(); ++it) {
		*it = tolower_ascii(static_cast<wxChar>(*it));
	}
	return ret;
}
}

#endif
