#ifndef FILEZILLA_INTERFACE_TIMEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_TIMEFORMATTING_HEADER

namespace fz {
class datetime;
}

class CTimeFormat
{
public:
	static wxString Format(fz::datetime const& time);
	static wxString FormatDateTime(fz::datetime const& time);
	static wxString FormatDate(fz::datetime const& time);
};

#endif
