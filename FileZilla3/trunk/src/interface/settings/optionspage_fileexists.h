#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILEEXISTS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILEEXISTS_HEADER

class COptionsPageFileExists final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_FILEEXISTS"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();
};

#endif
