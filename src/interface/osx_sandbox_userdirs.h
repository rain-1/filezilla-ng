#ifndef FILEZILLA_OSX_SANDBOX_USERDIRS
#define FILEZILLA_OSX_SANDBOX_USERDIRS

#include "dialogex.h"

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

	bool AddFile(std::wstring const& file);

	void Remove(std::wstring const& dir);

	std::vector<std::wstring> GetDirs() const;

private:
	OSXSandboxUserdirs();
	~OSXSandboxUserdirs();

	bool Save();

	struct Data
	{
		bool dir{true};
		wxCFDataRef bookmark;
		wxCFRef<CFURLRef> url;
	};

	std::map<std::wstring, Data> userdirs_;
};

class OSXSandboxUserdirsDialog final : public wxDialogEx
{
public:
	void Run(wxWindow* parent);

private:
	void DisplayCurrentDirs();

	void OnAdd(wxCommandEvent&);
	void OnRemove(wxCommandEvent&);
};

#endif
