#ifndef FILEZILLA_BUILDINFO_HEADER
#define FILEZILLA_BUILDINFO_HEADER

#include <string>

enum class gui_lib_dependency
{
	wxwidgets,
	sqlite,
	count
};

class CBuildInfo final
{
public:
	CBuildInfo() = delete;

public:

	static std::wstring GetVersion();
	static int64_t ConvertToVersionNumber(wchar_t const* version);
	static std::wstring GetBuildDateString();
	static std::wstring GetBuildTimeString();
	static fz::datetime GetBuildDate();
	static std::wstring GetBuildType();
	static std::wstring GetCompiler();
	static std::wstring GetCompilerFlags();
	static std::wstring GetHostname();
	static std::wstring GetBuildSystem();
	static bool IsUnstable(); // Returns true on beta or rc releases.
	static std::wstring GetCPUCaps(char separator = ',');
};

std::wstring GetDependencyName(gui_lib_dependency d);
std::wstring GetDependencyVersion(gui_lib_dependency d);

// Microsoft, in its insane stupidity, has decided to make GetVersion(Ex) useless, starting with Windows 8.1,
// this function no longer returns the operating system version but instead some arbitrary and random value depending
// on the phase of the moon.
// This function instead returns the actual Windows version. On non-Windows systems, it's equivalent to
// wxGetOsVersion
bool GetRealOsVersion(int& major, int& minor);

#endif
