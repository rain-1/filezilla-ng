#ifndef __TIMEEX_H__
#define __TIMEEX_H__

#include <wx/timer.h>

class CDateTime
{
public:
	enum Accuracy {
		days,
		hours,
		minutes,
		seconds,
		milliseconds
	};

	CDateTime();
	CDateTime( wxDateTime const& t, Accuracy a );

	wxDateTime Degenerate();

	bool IsValid() const { return t_.IsValid(); }

	static CDateTime Now();

	bool operator==( CDateTime const& op ) const;
	bool operator!=( CDateTime const& op ) const { return !(*this == op); }
	bool operator<( CDateTime const& op ) const;
	bool operator>( CDateTime const& op ) const { return op < *this; }

	wxTimeSpan operator-( CDateTime const& op ) const;

	int Compare( CDateTime const& op ) const;

private:
	bool IsClamped();

	Accuracy a_;
	wxDateTime t_;
};


/* If called multiple times in a row, wxDateTime::Now may return the same
 * time. This causes problems with the cache logic. This class implements
 * an extended time class in wich Now() never returns the same value.
 */

class CMonotonicTime
{
public:
	CMonotonicTime(const CDateTime& time);
	CMonotonicTime();

	static CMonotonicTime Now();

	CDateTime GetTime() const { return m_time; }

	bool IsValid() const { return m_time.IsValid(); }

	bool operator < (const CMonotonicTime& op) const;
	bool operator <= (const CMonotonicTime& op) const;
	bool operator > (const CMonotonicTime& op) const;
	bool operator >= (const CMonotonicTime& op) const;
	bool operator == (const CMonotonicTime& op) const;

protected:
	static CDateTime m_lastTime;
	static int m_lastOffset;

	CDateTime m_time;
	int m_offset;
};

#endif //__TIMEEX_H__
