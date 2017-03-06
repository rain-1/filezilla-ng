#include <filezilla.h>
#include "buildinfo.h"

#include <libfilezilla/format.hpp>

#include <sqlite3.h>

std::wstring GetDependencyVersion(gui_lib_dependency d)
{
	switch (d) {
	case gui_lib_dependency::wxwidgets:
		return wxVERSION_NUM_DOT_STRING_T;
	case gui_lib_dependency::sqlite:
		return fz::to_wstring_from_utf8(sqlite3_libversion());
	default:
		return std::wstring();
	}
}

std::wstring GetDependencyName(gui_lib_dependency d)
{
	switch (d) {
	case gui_lib_dependency::wxwidgets:
		return L"wxWidgets";
	case gui_lib_dependency::sqlite:
		return L"SQLite";
	default:
		return std::wstring();
	}
}


std::wstring CBuildInfo::GetVersion()
{
	return fz::to_wstring(PACKAGE_VERSION);
}

std::wstring CBuildInfo::GetBuildDateString()
{
	// Get build date. Unfortunately it is in the ugly Mmm dd yyyy format.
	// Make a good yyyy-mm-dd out of it
	std::wstring date = fz::to_wstring(__DATE__);
	while (date.find(L"  ") != std::wstring::npos) {
		fz::replace_substrings(date, L"  ", L" ");
	}

	wchar_t const months[][4] = { L"Jan", L"Feb", L"Mar",
								L"Apr", L"May", L"Jun",
								L"Jul", L"Aug", L"Sep",
								L"Oct", L"Nov", L"Dec" };

	size_t pos = date.find(' ');
	if (pos == std::wstring::npos) {
		return date;
	}

	std::wstring month = date.substr(0, pos);
	size_t i = 0;
	for (i = 0; i < 12; ++i) {
		if (months[i] == month) {
			break;
		}
	}
	if (i == 12) {
		return date;
	}

	std::wstring tmp = date.substr(pos + 1);
	pos = tmp.find(' ');
	if (pos == std::wstring::npos) {
		return date;
	}

	auto day = fz::to_integral<unsigned int>(tmp.substr(0, pos));
	if (!day) {
		return date;
	}

	auto year = fz::to_integral<unsigned int>(tmp.substr(pos + 1));
	if (!year) {
		return date;
	}

	return fz::sprintf(L"%04d-%02d-%02d", year, i + 1, day);
}

std::wstring CBuildInfo::GetBuildTimeString()
{
	return fz::to_wstring(__TIME__);
}

fz::datetime CBuildInfo::GetBuildDate()
{
	fz::datetime date(GetBuildDateString(), fz::datetime::utc);
	return date;
}

std::wstring CBuildInfo::GetCompiler()
{
#ifdef USED_COMPILER
	return fz::to_wstring(USED_COMPILER);
#elif defined __VISUALC__
	int version = __VISUALC__;
	return fz::sprintf(L"Visual C++ %d", version);
#else
	return L"Unknown compiler";
#endif
}

std::wstring CBuildInfo::GetCompilerFlags()
{
#ifndef USED_CXXFLAGS
	return std::wstring();
#else
	return fz::to_wstring(USED_CXXFLAGS);
#endif
}

std::wstring CBuildInfo::GetBuildType()
{
#ifdef BUILDTYPE
	std::wstring buildtype = fz::to_wstring(BUILDTYPE);
	if (buildtype == L"official" || buildtype == L"nightly") {
		return buildtype;
	}
#endif
	return std::wstring();
}

int64_t CBuildInfo::ConvertToVersionNumber(wchar_t const* version)
{
	// Crude conversion from version string into number for easy comparison
	// Supported version formats:
	// 1.2.4
	// 11.22.33.44
	// 1.2.3-rc3
	// 1.2.3.4-beta5
	// All numbers can be as large as 1024, with the exception of the release candidate.

	// Only either rc or beta can exist at the same time)
	//
	// The version string A.B.C.D-rcE-betaF expands to the following binary representation:
	// 0000aaaaaaaaaabbbbbbbbbbccccccccccddddddddddxeeeeeeeeeffffffffff
	//
	// x will be set to 1 if neither rc nor beta are set. 0 otherwise.
	//
	// Example:
	// 2.2.26-beta3 will be converted into
	// 0000000010 0000000010 0000011010 0000000000 0000000000 0000000011
	// which in turn corresponds to the simple 64-bit number 2254026754228227
	// And these can be compared easily

	if (!version || *version < '0' || *version > '9') {
		return -1;
	}

	int64_t v{};
	int segment{};
	int shifts{};

	for (; *version; ++version) {
		if (*version == '.' || *version == '-' || *version == 'b') {
			v += segment;
			segment = 0;
			v <<= 10;
			shifts++;
		}
		if (*version == '-' && shifts < 4) {
			v <<= (4 - shifts) * 10;
			shifts = 4;
		}
		else if (*version >= '0' && *version <= '9') {
			segment *= 10;
			segment += *version - '0';
		}
	}
	v += segment;
	v <<= (5 - shifts) * 10;

	// Make sure final releases have a higher version number than rc or beta releases
	if ((v & 0x0FFFFF) == 0) {
		v |= 0x080000;
	}

	return v;
}

std::wstring CBuildInfo::GetHostname()
{
#ifndef USED_HOST
	return std::wstring();
#else
	return fz::to_wstring(USED_HOST);
#endif
}

std::wstring CBuildInfo::GetBuildSystem()
{
#ifndef USED_BUILD
	return std::wstring();
#else
	return fz::to_wstring(USED_BUILD);
#endif
}

bool CBuildInfo::IsUnstable()
{
	if (GetVersion().find(L"beta") != std::wstring::npos) {
		return true;
	}

	if (GetVersion().find(L"rc") != std::wstring::npos) {
		return true;
	}

	return false;
}


#if defined(__i386__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86)
#define HAVE_CPUID 1
#endif

#if HAVE_CPUID

#ifdef _MSC_VER
namespace {
	void cpuid(int f, int sub, int reg[4])
	{
		__cpuidex(reg, f, sub);
	}
}
#else
#include <cpuid.h>
namespace {
	void cpuid(int f, int sub, int reg[4])
	{
		__cpuid_count(f, sub, reg[0], reg[1], reg[2], reg[3]);
	}
}
#endif
#endif

std::wstring CBuildInfo::GetCPUCaps(char separator)
{
	std::wstring ret;

#if HAVE_CPUID
	int reg[4];
	cpuid(0, 0, reg);

	int const max = reg[0];

	// function (aka leave), subfunction (subleave), register, bit, description
	std::tuple<int, int, int, int, std::wstring> const caps[] = {
		std::make_tuple(1, 0, 3, 25, L"sse"),
		std::make_tuple(1, 0, 3, 26, L"sse2"),
		std::make_tuple(1, 0, 2, 0,  L"sse3"),
		std::make_tuple(1, 0, 2, 9,  L"ssse3"),
		std::make_tuple(1, 0, 2, 19, L"sse4.1"),
		std::make_tuple(1, 0, 2, 20, L"sse4.2"),
		std::make_tuple(1, 0, 2, 28, L"avx"),
		std::make_tuple(7, 0, 1, 5,  L"avx2"),
		std::make_tuple(1, 0, 2, 25, L"aes"),
		std::make_tuple(1, 0, 2, 1,  L"pclmulqdq"),
		std::make_tuple(1, 0, 2, 30, L"rdrnd"),
		std::make_tuple(7, 0, 1, 3,  L"bmi2"),
		std::make_tuple(7, 0, 1, 8,  L"bmi2"),
		std::make_tuple(7, 0, 1, 19, L"adx")
	};

	for (auto const& cap : caps) {
		if (max >= std::get<0>(cap)) {
			cpuid(std::get<0>(cap), std::get<1>(cap), reg);
			if (reg[std::get<2>(cap)] & (1 << std::get<3>(cap))) {
				if (!ret.empty()) {
					ret += separator;
				}
				ret += std::get<4>(cap);
			}
		}
	}
#endif

	return ret;
}

#ifdef FZ_WINDOWS
namespace {
bool IsAtLeast(int major, int minor = 0)
{
	OSVERSIONINFOEX vi = { 0 };
	vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	vi.dwMajorVersion = major;
	vi.dwMinorVersion = minor;
	vi.dwPlatformId = VER_PLATFORM_WIN32_NT;

	DWORDLONG mask = 0;
	VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(mask, VER_MINORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(mask, VER_PLATFORMID, VER_EQUAL);
	return VerifyVersionInfo(&vi, VER_MAJORVERSION | VER_MINORVERSION | VER_PLATFORMID, mask) != 0;
}
}
#endif

bool GetRealOsVersion(int& major, int& minor)
{
#ifndef FZ_WINDOWS
	return wxGetOsVersion(&major, &minor) != wxOS_UNKNOWN;
#else
	major = 4;
	minor = 0;
	while (IsAtLeast(++major, minor))
	{
	}
	--major;
	while (IsAtLeast(major, ++minor))
	{
	}
	--minor;

	return true;
#endif
}
