#ifndef FILEZILLA_OSX_SANDBOX_USERDIRS
#define FILEZILLA_OSX_SANDBOX_USERDIRS

#include <map>
#include <string>

#include <wx/osx/core/cfdataref.h>

#include <CoreFoundation/CFUrl.h>

class OSXSandboxUserdirs final
{
public:
	static OSXSandboxUserdirs& Get();

	// Prints error message internally
	void Load();

	bool Add();

private:
	OSXSandboxUserdirs();
	~OSXSandboxUserdirs();

	bool Save();

	std::map<std::wstring, std::pair<wxCFDataRef, wxCFRef<CFURLRef>>> userdirs_;
};

#endif
