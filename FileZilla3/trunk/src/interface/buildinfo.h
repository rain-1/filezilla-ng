#ifndef FILEZILLA_BUILDINFO_HEADER
#define FILEZILLA_BUILDINFO_HEADER

#include <string>

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

#endif
