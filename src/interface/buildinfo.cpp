#include <filezilla.h>
#include "buildinfo.h"

wxString CBuildInfo::GetVersion()
{
	return wxString(PACKAGE_VERSION, wxConvLocal);
}

wxString CBuildInfo::GetBuildDateString()
{
	// Get build date. Unfortunately it is in the ugly Mmm dd yyyy format.
	// Make a good yyyy-mm-dd out of it
	wxString date(__DATE__, wxConvLocal);
	while (date.Replace(_T("  "), _T(" ")));

	const wxChar months[][4] = { _T("Jan"), _T("Feb"), _T("Mar"),
								_T("Apr"), _T("May"), _T("Jun"),
								_T("Jul"), _T("Aug"), _T("Sep"),
								_T("Oct"), _T("Nov"), _T("Dec") };

	int pos = date.Find(_T(" "));
	if (pos == -1)
		return date;

	wxString month = date.Left(pos);
	int i = 0;
	for (i = 0; i < 12; ++i) {
		if (months[i] == month)
			break;
	}
	if (i == 12)
		return date;

	wxString tmp = date.Mid(pos + 1);
	pos = tmp.Find(_T(" "));
	if (pos == -1)
		return date;

	long day;
	if (!tmp.Left(pos).ToLong(&day))
		return date;

	long year;
	if (!tmp.Mid(pos + 1).ToLong(&year))
		return date;

	return wxString::Format(_T("%04d-%02d-%02d"), (int)year, i + 1, (int)day);
}

wxString CBuildInfo::GetBuildTimeString()
{
	return wxString(__TIME__, wxConvLocal);
}

fz::datetime CBuildInfo::GetBuildDate()
{
	fz::datetime date(GetBuildDateString(), fz::datetime::utc);
	return date;
}

wxString CBuildInfo::GetCompiler()
{
#ifdef USED_COMPILER
	return wxString(USED_COMPILER, wxConvLocal);
#elif defined __VISUALC__
	int version = __VISUALC__;
	return wxString::Format(_T("Visual C++ %d"), version);
#else
	return _T("Unknown compiler");
#endif
}

wxString CBuildInfo::GetCompilerFlags()
{
#ifndef USED_CXXFLAGS
	return wxString();
#else
	wxString flags(USED_CXXFLAGS, wxConvLocal);
	return flags;
#endif
}

wxString CBuildInfo::GetBuildType()
{
#ifdef BUILDTYPE
	wxString buildtype(BUILDTYPE, wxConvLocal);
	if (buildtype == _T("official") || buildtype == _T("nightly"))
		return buildtype;
#endif
	return wxString();
}

int64_t CBuildInfo::ConvertToVersionNumber(const wxChar* version)
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

	if (*version < '0' || *version > '9')
		return -1;

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
	if ((v & 0x0FFFFF) == 0)
		v |= 0x080000;

	return v;
}

wxString CBuildInfo::GetHostname()
{
#ifndef USED_HOST
	return wxString();
#else
	wxString flags(USED_HOST, wxConvLocal);
	return flags;
#endif
}

wxString CBuildInfo::GetBuildSystem()
{
#ifndef USED_BUILD
	return wxString();
#else
	wxString flags(USED_BUILD, wxConvLocal);
	return flags;
#endif
}

bool CBuildInfo::IsUnstable()
{
	if (GetVersion().Find(_T("beta")) != -1)
		return true;

	if (GetVersion().Find(_T("rc")) != -1)
		return true;

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

wxString CBuildInfo::GetCPUCaps(char separator)
{
	wxString ret;

#if HAVE_CPUID
	int reg[4];
	cpuid(0, 0, reg);

	int const max = reg[0];

	// function (aka leave), subfunction (subleave), register, bit, description
	std::tuple<int, int, int, int, wxString> const caps[] = {
		std::make_tuple(1, 0, 3, 25, _T("sse")),
		std::make_tuple(1, 0, 3, 26, _T("sse2")),
		std::make_tuple(1, 0, 2, 0,  _T("sse3")),
		std::make_tuple(1, 0, 2, 9,  _T("ssse3")),
		std::make_tuple(1, 0, 2, 19, _T("sse4.1")),
		std::make_tuple(1, 0, 2, 20, _T("sse4.2")),
		std::make_tuple(1, 0, 2, 28, _T("avx")),
		std::make_tuple(7, 0, 1, 5,  _T("avx2")),
		std::make_tuple(1, 0, 2, 25, _T("aes")),
		std::make_tuple(1, 0, 2, 1,  _T("pclmulqdq")),
		std::make_tuple(1, 0, 2, 30, _T("rdrnd")),
		std::make_tuple(7, 0, 1, 3,  _T("bmi2")),
		std::make_tuple(7, 0, 1, 8,  _T("bmi2")),
		std::make_tuple(7, 0, 1, 19, _T("adx"))
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
