#ifndef __TIMEFORMATTING_H__
#define __TIMEFORMATTING_H__

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

#endif //__TIMEFORMATTING_H__
