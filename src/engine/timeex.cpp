#include <filezilla.h>
#include "timeex.h"

CDateTime::CDateTime()
: a_(days)
{
}

CDateTime::CDateTime( wxDateTime const& t, Accuracy a )
: t_(t), a_(a)
{
	wxASSERT(IsClamped());
}

CDateTime CDateTime::Now()
{
	return CDateTime( wxDateTime::UNow(), milliseconds );
}

bool CDateTime::operator<( CDateTime const& op ) const
{
	if( t_ < op.t_ ) {
		return true;
	}
	if( t_ > op.t_ ) {
		return false;
	}

	return a_ < op.a_;
}

bool CDateTime::operator==( CDateTime const& op ) const
{
	return t_ == op.t_ && a_ == op.a_;
}

wxTimeSpan CDateTime::operator-( CDateTime const& op ) const
{
	return t_ - op.t_;
}

bool CDateTime::IsClamped()
{
	bool ret = true;
	wxDateTime::Tm t = t_.GetTm();
	if( a_ < milliseconds && t.msec ) {
		ret = false;
	}
	else if( a_ < seconds && t.sec ) {
		ret = false;
	}
	else if( a_ < minutes && t.min ) {
		ret = false;
	}
	else if( a_ < hours && t.hour ) {
		ret = false;
	}
	return ret;
}

int CDateTime::Compare( CDateTime const& op ) const
{
	if( a_ == op.a_ ) {
		// First fast path: Same accuracy
		if( t_ < op.t_ ) {
			return -1;
		}
		else if( t_ > op.t_ ) {
			return 1;
		}
		else {
			return 0;
		}
	}

	// Second fast path: Lots of difference, at least 2 days
	wxLongLong diff = t_.GetValue() - op.t_.GetValue();
	if( diff > 60 * 60 * 24 * 1000 * 2 ) {
		return 1;
	}
	else if( diff < -60 * 60 * 24 * 1000 * 2 ) {
		return -1;
	}

	wxDateTime::Tm t1 = t_.GetTm();
	wxDateTime::Tm t2 = op.t_.GetTm();
	if( t1.year < t2.year ) {
		return -1;
	}
	else if( t1.year > t2.year ) {
		return 1;
	}
	if( t1.mon < t2.mon ) {
		return -1;
	}
	else if( t1.mon > t2.mon ) {
		return 1;
	}
	if( t1.mday < t2.mday ) {
		return -1;
	}
	else if( t1.mday > t2.mday ) {
		return 1;
	}

	Accuracy a = (a_ < op.a_ ) ? a_ : op.a_;

	if( a < hours ) {
		return 0;
	}
	if( t1.hour < t2.hour ) {
		return -1;
	}
	else if( t1.hour > t2.hour ) {
		return 1;
	}

	if( a < minutes ) {
		return 0;
	}
	if( t1.min < t2.min ) {
		return -1;
	}
	else if( t1.min > t2.min ) {
		return 1;
	}

	if( a < seconds ) {
		return 0;
	}
	if( t1.sec < t2.sec ) {
		return -1;
	}
	else if( t1.sec > t2.sec ) {
		return 1;
	}

	wxASSERT( a < milliseconds );
	return 0;
}













CDateTime CMonotonicTime::m_lastTime = CDateTime::Now();
int CMonotonicTime::m_lastOffset = 0;

CMonotonicTime::CMonotonicTime()
{
	m_offset = 0;
}

CMonotonicTime::CMonotonicTime(const CDateTime& time)
{
	m_time = time;
	m_offset = 0;
}

CMonotonicTime CMonotonicTime::Now()
{
	CMonotonicTime time;
	time.m_time = CDateTime::Now();
	if (time.m_time == m_lastTime)
		time.m_offset = ++m_lastOffset;
	else
	{
		m_lastTime = time.m_time;
		time.m_offset = m_lastOffset = 0;
	}
	return time;
}

bool CMonotonicTime::operator < (const CMonotonicTime& op) const
{
	if (m_time < op.m_time)
		return true;
	if (m_time > op.m_time)
		return false;

	return m_offset < op.m_offset;
}

bool CMonotonicTime::operator <= (const CMonotonicTime& op) const
{
	if (m_time < op.m_time)
		return true;
	if (m_time > op.m_time)
		return false;

	return m_offset <= op.m_offset;
}

bool CMonotonicTime::operator > (const CMonotonicTime& op) const
{
	if (m_time > op.m_time)
		return true;
	if (m_time < op.m_time)
		return false;

	return m_offset > op.m_offset;
}

bool CMonotonicTime::operator >= (const CMonotonicTime& op) const
{
	if (m_time > op.m_time)
		return true;
	if (m_time < op.m_time)
		return false;

	return m_offset >= op.m_offset;
}

bool CMonotonicTime::operator == (const CMonotonicTime& op) const
{
	if (m_time != op.m_time)
		return false;

	return m_offset == op.m_offset;
}
