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
	CDateTime( int year, int month, int day, int hour = -1, int minute = -1, int second = -1, int millisecond = -1 );
	CDateTime( wxDateTime const& t, Accuracy a );

	CDateTime( CDateTime const& op );
	CDateTime& operator=( CDateTime const& op );

	wxDateTime Degenerate() const { return t_; }

	bool IsValid() const { return t_.IsValid(); }

	Accuracy GetAccuracy() const { return a_; }

	static CDateTime Now();

	bool operator==( CDateTime const& op ) const;
	bool operator!=( CDateTime const& op ) const { return !(*this == op); }
	bool operator<( CDateTime const& op ) const;
	bool operator>( CDateTime const& op ) const { return op < *this; }

	wxTimeSpan operator-( CDateTime const& op ) const;

	int Compare( CDateTime const& op ) const;
	bool IsEarlierThan( CDateTime const& op ) const { return Compare(op) < 0; };
	bool IsLaterThan( CDateTime const& op ) const { return Compare(op) > 0; };

	CDateTime& operator+=( wxTimeSpan const& op );
	CDateTime operator+( wxTimeSpan const& op ) const { CDateTime t(*this); t += op; return t; }

	// Beware: month and day are 1-indexed!
	bool Set( int year, int month, int day, int hour = -1, int minute = -1, int second = -1, int millisecond = -1 );
	bool ImbueTime( int hour, int minute, int second = -1, int millisecond = -1 );

private:
	int CompareSlow( CDateTime const& op ) const;

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
