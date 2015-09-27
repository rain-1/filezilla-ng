#include "fz_time.hpp"

#ifndef FZ_WINDOWS
#include <sys/time.h>
#endif

#include <wchar.h>

#define TIME_ASSERT(x) //assert(x)

namespace fz {

datetime::datetime(zone z, int year, int month, int day, int hour, int minute, int second, int millisecond)
{
	set(z, year, month, day, hour, minute, second, millisecond);
}

datetime::datetime(time_t t, accuracy a)
	: t_(static_cast<int64_t>(t) * 1000)
	, a_(a)
{
	TIME_ASSERT(clamped());
	TIME_ASSERT(a != milliseconds);
}

namespace {
template<typename C>
void skip(C const*& it, C const* const end)
{
	while (it != end && (*it < '0' || *it > '9')) {
		++it;
	}
}

template<typename T, typename C>
bool parse(C const*& it, C const* end, int count, T & v, int offset)
{
	skip(it, end);

	if (end - it < count) {
		return false;
	}

	T w = 0;

	C const* const stop = it + count;
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

template<typename String>
bool do_set(datetime& dt, String const& str, datetime::zone z)
{
	auto const* it = str.c_str();
	auto const* end = it + str.size();

#ifdef FZ_WINDOWS
	SYSTEMTIME st{};
	if (!parse(it, end, 4, st.wYear, 0) ||
		!parse(it, end, 2, st.wMonth, 0) ||
		!parse(it, end, 2, st.wDay, 0))
	{
		dt.clear();
		return false;
	}

	datetime::accuracy a = datetime::days;
	if (parse(it, end, 2, st.wHour, 0)) {
		a = datetime::hours;
		if (parse(it, end, 2, st.wMinute, 0)) {
			a = datetime::minutes;
			if (parse(it, end, 2, st.wSecond, 0)) {
				a = datetime::seconds;
				if (parse(it, end, 3, st.wMilliseconds, 0)) {
					a = datetime::milliseconds;
				}
			}
		}
	}
	return dt.set(st, a, z);
#else
	tm t{};
	if (!parse(it, end, 4, t.tm_year, -1900) ||
		!parse(it, end, 2, t.tm_mon, -1) ||
		!parse(it, end, 2, t.tm_mday, 0))
	{
		dt.clear();
		return false;
	}

	datetime::accuracy a = datetime::days;
	int64_t ms{};
	if (parse(it, end, 2, t.tm_hour, 0)) {
		a = datetime::hours;
		if (parse(it, end, 2, t.tm_min, 0)) {
			a = datetime::minutes;
			if (parse(it, end, 2, t.tm_sec, 0)) {
				a = datetime::seconds;
				if (parse(it, end, 3, ms, 0)) {
					a = datetime::milliseconds;
				}
			}
		}
	}
	bool success = dt.set(t, a, z);
	if (success) {
		dt += duration::from_milliseconds(ms);
	}
	return success;
#endif
}
}

datetime::datetime(std::string const& str, zone z)
{
	do_set(*this, str, z);
}

datetime::datetime(std::wstring const& str, zone z)
{
	do_set(*this, str, z);
}

datetime datetime::now()
{
#ifdef FZ_WINDOWS
	FILETIME ft{};
	GetSystemTimeAsFileTime(&ft);
	return datetime(ft, milliseconds);
#else
	datetime ret;
	timeval tv = { 0, 0 };
	if (gettimeofday(&tv, 0) == 0) {
		ret.t_ = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
		ret.a_ = milliseconds;
	}
	return ret;
#endif
}

bool datetime::operator<(datetime const& op) const
{
	if (t_ == invalid) {
		return op.t_ != invalid;
	}
	else if (op.t_ == invalid) {
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

bool datetime::operator<=(datetime const& op) const
{
	if (t_ == invalid) {
		return true;
	}
	else if (op.t_ == invalid) {
		return false;
	}

	if (t_ < op.t_) {
		return true;
	}
	if (t_ > op.t_) {
		return false;
	}

	return a_ <= op.a_;
}

bool datetime::operator==(datetime const& op) const
{
	return t_ == op.t_ && a_ == op.a_;
}

bool datetime::clamped()
{
	bool ret = true;
	tm t = get_tm(utc);
	if (a_ < milliseconds && get_milliseconds() != 0) {
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

int datetime::Compare(datetime const& op) const
{
	if (t_ == invalid) {
		return (op.t_ == invalid) ? 0 : -1;
	}
	else if (op.t_ == invalid) {
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
		TIME_ASSERT(compare_slow(op) == ret);
		return ret;
	}

	// Second fast path: Lots of difference, at least 2 days
	int64_t diff = t_ - op.t_;
	if (diff > 60 * 60 * 24 * 1000 * 2) {
		TIME_ASSERT(compare_slow(op) == 1);
		return 1;
	}
	else if (diff < -60 * 60 * 24 * 1000 * 2) {
		TIME_ASSERT(compare_slow(op) == -1);
		return -1;
	}

	return compare_slow(op);
}

int datetime::compare_slow(datetime const& op) const
{
	tm const t1 = get_tm(utc);
	tm const t2 = op.get_tm(utc);
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

	accuracy a = (a_ < op.a_) ? a_ : op.a_;

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
	else if (t1.tm_sec > t2.tm_sec) {
		return 1;
	}

	if (a < milliseconds) {
		return 0;
	}
	auto ms1 = get_milliseconds();
	auto ms2 = op.get_milliseconds();
	if (ms1 < ms2) {
		return -1;
	}
	else if (ms1 > ms2) {
		return 1;
	}

	return 0;
}

datetime& datetime::operator+=(duration const& op)
{
	if (empty()) {
		if (a_ < hours) {
			t_ += op.get_days() * 24 * 3600 * 1000;
		}
		else if (a_ < minutes) {
			t_ += op.get_hours() * 3600 * 1000;
		}
		else if (a_ < seconds) {
			t_ += op.get_minutes() * 60 * 1000;
		}
		else if (a_ < milliseconds) {
			t_ += op.get_seconds() * 1000;
		}
		else {
			t_ += op.get_milliseconds();
		}
	}
	return *this;
}

datetime& datetime::operator-=(duration const& op)
{
	*this += -op;
	return *this;
}

bool datetime::set(zone z, int year, int month, int day, int hour, int minute, int second, int millisecond)
{
	accuracy a;
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

#ifdef FZ_WINDOWS
	SYSTEMTIME st{};
	st.wYear = year;
	st.wMonth = month;
	st.wDay = day;
	st.wHour = hour;
	st.wMinute = minute;
	st.wSecond = second;
	st.wMilliseconds = millisecond;

	return set(st, a, z);
#else

	tm t{};
	t.tm_isdst = -1;
	t.tm_year = year - 1900;
	t.tm_mon = month - 1;
	t.tm_mday = day;
	t.tm_hour = hour;
	t.tm_min = minute;
	t.tm_sec = second;

	bool const success = set(t, a, z);

	if (success) {
		t_ += millisecond;
	}

	return success;
#endif
}

bool datetime::set(std::string const& str, zone z)
{
	return do_set(*this, str, z);
}

bool datetime::set(std::wstring const& str, zone z)
{
	return do_set(*this, str, z);
}

#ifdef FZ_WINDOWS

bool datetime::set(SYSTEMTIME const& st, accuracy a, zone z)
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
	return set(ft, a);
}

namespace {
template<typename T>
int64_t make_int64_t(T hi, T lo)
{
	return (static_cast<int64_t>(hi) << 32) + static_cast<int64_t>(lo);
}

// This is the offset between FILETIME epoch in 100ns and the Unix epoch in ms.
int64_t const EPOCH_OFFSET_IN_MSEC = 11644473600000ll;
}

bool datetime::set(FILETIME const& ft, accuracy a)
{
	if (ft.dwHighDateTime || ft.dwLowDateTime) {
		// See http://trac.wxwidgets.org/changeset/74423 and http://trac.wxwidgets.org/ticket/13098
		// Directly converting to time_t

		int64_t t = make_int64_t(ft.dwHighDateTime, ft.dwLowDateTime);
		t /= 10000; // Convert hundreds of nanoseconds to milliseconds.
		t -= EPOCH_OFFSET_IN_MSEC;
		if (t != invalid) {
			t_ = t;
			a_ = a;
			TIME_ASSERT(clamped());
			return true;
		}
	}
	clear();
	return false;
}

#else

bool datetime::set(tm& t, accuracy a, zone z)
{
	time_t tt;

	errno = 0;

	if (a >= hours && z == local) {
		tt = mktime(&t);
	}
	else {
		tt = timegm(&t);
	}

	if (tt != time_t(-1) || !errno) {
		t_ = static_cast<int64_t>(tt) * 1000;
		a_ = a;

		TIME_ASSERT(clamped());

		return true;
	}

	clear();
	return false;
}

#endif

bool datetime::imbue_time(int hour, int minute, int second, int millisecond)
{
	if (!empty() || a_ > days) {
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

bool datetime::empty() const
{
	return t_ != invalid;
}

void datetime::clear()
{
	a_ = days;
	t_ = invalid;
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

bool datetime::verify_format(std::string const& fmt)
{
	tm const t = datetime::now().get_tm(utc);
	char buf[4096];

#ifdef __VISUALC__
	CrtAssertSuppressor suppressor;
#endif

	return strftime(buf, sizeof(buf) / sizeof(char), fmt.c_str(), &t) != 0;
}

bool datetime::verify_format(std::wstring const& fmt)
{
	tm const t = datetime::now().get_tm(utc);
	wchar_t buf[4096];

#ifdef __VISUALC__
	CrtAssertSuppressor suppressor;
#endif

	return wcsftime(buf, sizeof(buf) / sizeof(wchar_t), fmt.c_str(), &t) != 0;
}

duration operator-(datetime const& a, datetime const& b)
{
	TIME_ASSERT(a.IsValid());
	TIME_ASSERT(b.IsValid());

	return duration::from_milliseconds(a.t_ - b.t_);
}

std::string datetime::format(std::string const& fmt, zone z) const
{
	tm t = get_tm(z);

	int const count = 1000;
	char buf[count];

#ifdef __VISUALC__
	CrtAssertSuppressor suppressor;
#endif
	strftime(buf, count - 1, fmt.c_str(), &t);
	buf[count - 1] = 0;

	return buf;
}

std::wstring datetime::format(std::wstring const& fmt, zone z) const
{
	tm t = get_tm(z);

	int const count = 1000;
	wchar_t buf[count];

#ifdef __VISUALC__
	CrtAssertSuppressor suppressor;
#endif
	wcsftime(buf, count - 1, fmt.c_str(), &t);
	buf[count - 1] = 0;

	return buf;
}

time_t datetime::get_time_t() const
{
	return t_ / 1000;
}

tm datetime::get_tm(zone z) const
{
	tm ret{};
	time_t t = get_time_t();
#ifdef FZ_WINDOWS
	// gmtime_s/localtime_s don't work with negative times
	if (t < 86400) {
		FILETIME ft = get_filetime();
		SYSTEMTIME st;
		if (FileTimeToSystemTime(&ft, &st)) {

			if (a_ >= hours && z == local) {
				SYSTEMTIME st2;
				if (SystemTimeToTzSpecificLocalTime(0, &st, &st2)) {
					st = st2;
				}
			}

			ret.tm_year = st.wYear - 1900;
			ret.tm_mon = st.wMonth - 1;
			ret.tm_mday = st.wDay;
			ret.tm_wday = st.wDayOfWeek;
			ret.tm_hour = st.wHour;
			ret.tm_min = st.wMinute;
			ret.tm_sec = st.wSecond;
			ret.tm_yday = -1;
		}
	}
	else {
		// Special case: If having only days, don't perform conversion
		if (z == utc || a_ == days) {
			gmtime_s(&ret, &t);
		}
		else {
			localtime_s(&ret, &t);
		}
	}
#else
	if (z == utc || a_ == days) {
		gmtime_r(&t, &ret);
	}
	else {
		localtime_r(&t, &ret);
	}
#endif
	return ret;
}

#ifdef FZ_WINDOWS

datetime::datetime(FILETIME const& ft, accuracy a)
{
	set(ft, a);
}

FILETIME datetime::get_filetime() const
{
	FILETIME ret{};
	if (empty()) {
		int64_t t = t_;

		t += EPOCH_OFFSET_IN_MSEC;
		t *= 10000;

		ret.dwHighDateTime = t >> 32;
		ret.dwLowDateTime = t & 0xffffffffll;
	}

	return ret;
}

#endif

}
