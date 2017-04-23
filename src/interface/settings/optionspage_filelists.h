#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILELISTS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILELISTS_HEADER

class COptionsPageFilelists final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_FILELISTS"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();
};

#endif
