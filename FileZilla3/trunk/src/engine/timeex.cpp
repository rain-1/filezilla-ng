#include <filezilla.h>
#include "timeex.h"

#define TIME_ASSERT(x) //wxASSERT(x)

CDateTime::CDateTime()
: a_(days)
{
}

CDateTime::CDateTime(CDateTime const& op)
: t_(op.t_)
, a_(op.a_)
{
}

CDateTime::CDateTime(Zone z, int year, int month, int day, int hour, int minute, int second, int millisecond)
	: a_()
{
	if( !Set(z, year, month, day, hour, minute, second, millisecond) ) {
		t_ = -1;
	}
}

CDateTime::CDateTime(time_t t, Accuracy a)
	: t_(t)
	, a_(a)
{
	TIME_ASSERT(IsClamped());
	TIME_ASSERT(a != milliseconds);
}

namespace {
void skip(wxChar const*& it, const wxChar* const end)
{
	while (it != end && (*it < '0' || *it > '9')) {
		++it;
	}
}

template<typename T>
bool parse(wxChar const*& it, wxChar const* end, int count, T & v, int offset)
{
	skip(it, end);

	if (end - it < count) {
		return false;
	}

	T w = 0;

	wxChar const* const stop = it + count;
	while (it != stop) {
		if (*it < '0' || *it > '9') {
			return false;
		}
		w *= 10;
		w += *it - '0';
		++it;
	}

	w += offset;

	v = w;
	return true;
}
}

CDateTime::CDateTime(wxString const& str, Zone z)
{
	Set(str, z);
}

CDateTime& CDateTime::operator=(CDateTime const& op)
{
	a_ = op.a_;
	t_ = op.t_;
	return *this;
}

CDateTime CDateTime::Now()
{
#ifdef __WXMSW__
	FILETIME ft{};
	GetSystemTimeAsFileTime(&ft);
	return CDateTime(ft, milliseconds);
#else
	CDateTime ret;
	timeval tv = { 0, 0 };
	if (gettimeofday(&tv, 0) == 0) {
		ret.t_ = tv.tv_sec * 1000 + tv.tv_nsec / 1000;
		ret.a_ = milliseconds;
	}
	return ret;
#endif
}

bool CDateTime::operator<(CDateTime const& op) const
{
	if (t_ < 0) {
		return op.t_ >= 0;
	}
	else if (op.t_ < 0) {
		return false;
	}

	if (t_ < op.t_) {
		return true;
	}
	if (t_ > op.t_) {
		return false;
	}

	return a_ < op.a_;
}

bool CDateTime::operator==(CDateTime const& op) const
{
	return t_ == op.t_ && a_ == op.a_;
}

bool CDateTime::IsClamped()
{
	bool ret = true;
	tm t = GetTm(utc);
	if (a_ < milliseconds && GetMilliseconds() != 0) {
		ret = false;
	}
	else if (a_ < seconds && t.tm_sec) {
		ret = false;
	}
	else if (a_ < minutes && t.tm_min) {
		ret = false;
	}
	else if (a_ < hours && t.tm_hour) {
		ret = false;
	}
	return ret;
}

int CDateTime::Compare( CDateTime const& op ) const
{
	if (t_ < 0) {
		return (op.t_ < 0) ? 0 : -1;
	}
	else if (op.t_ < 0) {
		return 1;
	}

	if (a_ == op.a_) {
		// First fast path: Same accuracy
		int ret = 0;
		if (t_ < op.t_) {
			ret = -1;
		}
		else if (t_ > op.t_) {
			ret = 1;
		}
		TIME_ASSERT(CompareSlow(op) == ret);
		return ret;
	}

	// Second fast path: Lots of difference, at least 2 days
	int64_t diff = t_ - op.t_;
	if (diff > 60 * 60 * 24 * 1000 * 2) {
		TIME_ASSERT(CompareSlow(op) == 1);
		return 1;
	}
	else if (diff < -60 * 60 * 24 * 1000 * 2) {
		TIME_ASSERT(CompareSlow(op) == -1);
		return -1;
	}

	return CompareSlow(op);
}

int CDateTime::CompareSlow( CDateTime const& op ) const
{
	tm t1 = GetTm(utc);
	tm t2 = op.GetTm(utc);
	if (t1.tm_year < t2.tm_year) {
		return -1;
	}
	else if (t1.tm_year > t2.tm_year) {
		return 1;
	}
	if (t1.tm_mon < t2.tm_mon) {
		return -1;
	}
	else if (t1.tm_mon > t2.tm_mon) {
		return 1;
	}
	if (t1.tm_mday < t2.tm_mday) {
		return -1;
	}
	else if (t1.tm_mday > t2.tm_mday) {
		return 1;
	}

	Accuracy a = (a_ < op.a_ ) ? a_ : op.a_;

	if (a < hours) {
		return 0;
	}
	if (t1.tm_hour < t2.tm_hour) {
		return -1;
	}
	else if (t1.tm_hour > t2.tm_hour) {
		return 1;
	}

	if (a < minutes) {
		return 0;
	}
	if (t1.tm_min < t2.tm_min) {
		return -1;
	}
	else if (t1.tm_min > t2.tm_min) {
		return 1;
	}

	if (a < seconds) {
		return 0;
	}
	if (t1.tm_sec < t2.tm_sec) {
		return -1;
	}
	else if(t1.tm_sec > t2.tm_sec) {
		return 1;
	}

	if (a < milliseconds) {
		return 0;
	}
	auto ms1 = GetMilliseconds();
	auto ms2 = op.GetMilliseconds();
	if (ms1 < ms2) {
		return -1;
	}
	else if (ms1 > ms2) {
		return 1;
	}

	return 0;
}

CDateTime& CDateTime::operator+=(wxTimeSpan const& op)
{
	if (IsValid()) {
		if( a_ < hours ) {
			t_ += static_cast<int64_t>(op.GetDays()) * 24 * 3600 * 1000;
		}
		else if( a_ < minutes ) {
			t_ += static_cast<int64_t>(op.GetHours()) * 3600 * 1000;
		}
		else if( a_ < seconds ) {
			t_ += static_cast<int64_t>(op.GetMinutes()) * 60 * 1000;
		}
		else if( a_ < milliseconds ) {
			t_ += static_cast<int64_t>(op.GetSeconds().GetValue()) * 1000;
		}
		else {
			t_ += op.GetMilliseconds().GetValue();
		}
	}
	return *this;
}

CDateTime& CDateTime::operator-=(wxTimeSpan const& op)
{
	*this += op.Negate();
	return *this;
}

bool CDateTime::Set(Zone z, int year, int month, int day, int hour, int minute, int second, int millisecond)
{
	clear();

	Accuracy a;
	if (hour == -1) {
		a = days;
		TIME_ASSERT(minute == -1);
		TIME_ASSERT(second == -1);
		TIME_ASSERT(millisecond == -1);
		hour = minute = second = millisecond = 0;
	}
	else if (minute == -1) {
		a = hours;
		TIME_ASSERT(second == -1);
		TIME_ASSERT(millisecond == -1);
		minute = second = millisecond = 0;
	}
	else if (second == -1) {
		a = minutes;
		TIME_ASSERT(millisecond == -1);
		second = millisecond = 0;
	}
	else if (millisecond == -1) {
		a = seconds;
		millisecond = 0;
	}
	else {
		a = milliseconds;
	}

#ifdef __WXMSW__
	SYSTEMTIME st{};
	st.wYear = year;
	st.wMonth = month;
	st.wDay = day;
	st.wHour = hour;
	st.wMinute = minute;
	st.wSecond = second;
	st.wMilliseconds = millisecond;

	return Set(st, a, z);
#else
	// fixme
	if (year < 1970 || year > 3000)
		return false;

	if (month < 1 || month > 12)
		return false;

	int maxDays = wxDateTime::GetNumberOfDays(static_cast<wxDateTime::Month>(month - 1), year);
	if (day < 1 || day > maxDays)
		return false;

	if( hour < 0 || hour >= 24 ) {
		return false;
	}
	if( minute < 0 || minute >= 60 ) {
		return false;
	}
	if( second < 0 || second >= 60 ) {
		return false;
	}
	if( millisecond < 0 || millisecond >= 1000 ) {
		return false;
	}

	//fixme
	//t_.Set( day, static_cast<wxDateTime::Month>(month - 1), year, hour, minute, second, millisecond );
	return IsValid();
#endif
}

bool CDateTime::Set(wxString const& str, Zone z)
{
	clear();

	wxChar const* it = str;
	wxChar const* end = it + str.size();

#ifdef __WXMSW__
	SYSTEMTIME st{};
	if (!parse(it, end, 4, st.wYear, 0) ||
		!parse(it, end, 2, st.wMonth, 0) ||
		!parse(it, end, 2, st.wDay, 0))
	{
		return false;
	}

	Accuracy a = days;
	if (parse(it, end, 2, st.wHour, 0)) {
		a = hours;
		if (parse(it, end, 2, st.wMinute, 0)) {
			a = minutes;
			if (parse(it, end, 2, st.wSecond, 0)) {
				a = seconds;
				if (parse(it, end, 3, st.wMilliseconds, 0)) {
					a = milliseconds;
				}
			}
		}
	}
	return Set(st, a, z);
#else
	// fixme
#endif
}

#ifdef __WXMSW__

bool CDateTime::Set(SYSTEMTIME const& st, Accuracy a, Zone z)
{
	clear();

	FILETIME ft{};
	if (a >= hours && z == local) {
		SYSTEMTIME st2{};
		if (!TzSpecificLocalTimeToSystemTime(0, &st, &st2)) {
			return false;
		}
		if (!SystemTimeToFileTime(&st2, &ft)) {
			return false;
		}
	}
	else if (!SystemTimeToFileTime(&st, &ft)) {
		return false;
	}
	return Set(ft, a);
}

namespace {
template<typename T>
int64_t make_int64_t(T hi, T lo)
{
	return (static_cast<int64_t>(hi) << 32) + static_cast<int64_t>(lo);
}

// This is the offset between FILETIME epoch and the Unix/wxDateTime Epoch.
int64_t const EPOCH_OFFSET_IN_MSEC = 11644473600000ll;
}

bool CDateTime::Set(FILETIME const& ft, Accuracy a)
{
	clear();
	if (ft.dwHighDateTime || ft.dwLowDateTime) {
		// See http://trac.wxwidgets.org/changeset/74423 and http://trac.wxwidgets.org/ticket/13098
		// Directly converting to time_t

		int64_t t = make_int64_t(ft.dwHighDateTime, ft.dwLowDateTime);
		t /= 10000; // Convert hundreds of nanoseconds to milliseconds.
		t -= EPOCH_OFFSET_IN_MSEC;
		if (t >= 0) {
			t_ = t;
			a_ = a;
			return true;
		}
	}
	return false;
}
#endif

bool CDateTime::ImbueTime(int hour, int minute, int second, int millisecond)
{
	if (!IsValid() || a_ > days) {
		return false;
	}

	if (second == -1) {
		a_ = minutes;
		TIME_ASSERT(millisecond == -1);
		second = millisecond = 0;
	}
	else if (millisecond == -1) {
		a_ = seconds;
		millisecond = 0;
	}
	else {
		a_ = milliseconds;
	}

	if (hour < 0 || hour >= 24) {
		return false;
	}
	if (minute < 0 || minute >= 60) {
		return false;
	}
	if (second < 0 || second >= 60) {
		return false;
	}
	if (millisecond < 0 || millisecond >= 1000) {
		return false;
	}

	t_ += (hour * 3600 + minute * 60 + second) * 1000 + millisecond;
	return true;
}

bool CDateTime::IsValid() const
{
	return t_ >= 0;
}

void CDateTime::clear()
{
	a_ = days;
	t_ = -1;
}

#ifdef __VISUALC__

#include <mutex.h>

namespace {

// Sadly wcsftime has shitty error handling, instead of returning 0 and setting errrno, it invokes some crt debug machinary.
// Fortunately we don't build the official FZ binaries with Visual Studio.
extern "C" void NullInvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
}

struct CrtAssertSuppressor
{
	CrtAssertSuppressor()
	{
		scoped_lock l(m_);

		if (!refs_++) {
			oldError = _CrtSetReportMode(_CRT_ERROR, 0);
			oldAssert = _CrtSetReportMode(_CRT_ASSERT, 0);
			oldHandler = _set_invalid_parameter_handler(NullInvalidParameterHandler);
		}
	}

	~CrtAssertSuppressor()
	{
		scoped_lock l(m_);

		if (!--refs_) {
			_set_invalid_parameter_handler(oldHandler);
			_CrtSetReportMode(_CRT_ASSERT, oldAssert);
			_CrtSetReportMode(_CRT_ERROR, oldError);
		}
	}

	static int oldError;
	static int oldAssert;
	static _invalid_parameter_handler oldHandler;

	static mutex m_;
	static int refs_;
};

int CrtAssertSuppressor::oldError{};
int CrtAssertSuppressor::oldAssert{};
_invalid_parameter_handler CrtAssertSuppressor::oldHandler{};

mutex CrtAssertSuppressor::m_{};
int CrtAssertSuppressor::refs_{};

}
#endif

bool CDateTime::VerifyFormat(wxString const& fmt)
{
	wxChar buf[4096];
	tm t = CDateTime::Now().GetTm(utc);

#ifdef __VISUALC__
	CrtAssertSuppressor suppressor;
#endif

	return wxStrftime(buf, sizeof(buf)/sizeof(wxChar), fmt, &t) != 0;
}

duration operator-(CDateTime const& a, CDateTime const& b)
{
	TIME_ASSERT(a.IsValid());
	TIME_ASSERT(b.IsValid());

	return duration(a.t_ - b.t_);
}

wxString CDateTime::Format(wxString const& fmt, Zone z) const
{
	tm t = GetTm(z);

#ifdef __WXMSW__
	int const count = 1000;
	wxChar buf[count];

	CrtAssertSuppressor suppressor;
	wcsftime(buf, count - 1, fmt, &t);
	buf[count - 1] = 0;
	return buf;
#else
	// fixme
	return wxString();
#endif
}

time_t CDateTime::GetTimeT() const
{
	return t_ / 1000;
}

tm CDateTime::GetTm(Zone z) const
{
	tm ret{};
	time_t t = GetTimeT();
#ifdef __WXMSW__
	// Special case: If having only days, don't perform conversion
	if (z == utc || a_ == days) {
		gmtime_s(&ret, &t);
	}
	else {
		localtime_s(&ret, &t);
	}
#else
	if (z == utc || a_ == days) {
		gmtime_r(&ret, &t);
	}
	else {
		localtime_r(&ret, &t);
	}
#endif
	return ret;
}

#ifdef __WXMSW__

CDateTime::CDateTime(FILETIME const& ft, Accuracy a)
{
	Set(ft, a);
	TIME_ASSERT(IsClamped());
}

FILETIME CDateTime::GetFileTime() const
{
	FILETIME ret{};
	if (IsValid()) {
		int64_t t = t_;

		t += EPOCH_OFFSET_IN_MSEC;
		t *= 10000;

		ret.dwHighDateTime = t >> 32;
		ret.dwLowDateTime = t & 0xffffffffll;
	}

	return ret;
}

#endif
















CDateTime CMonotonicTime::m_lastTime = CDateTime::Now();
int CMonotonicTime::m_lastOffset = 0;

CMonotonicTime::CMonotonicTime(const CDateTime& time)
	: m_time(time)
{
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

#if defined(_MSC_VER) && _MSC_VER < 1900
namespace {
int64_t GetFreq() {
	LARGE_INTEGER f;
	(void)QueryPerformanceFrequency(&f); // Cannot fail on XP or later according to MSDN
	return f.QuadPart;
}
}

int64_t const CMonotonicClock::freq_ = GetFreq();
#endif