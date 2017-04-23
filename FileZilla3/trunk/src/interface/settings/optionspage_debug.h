#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_DEBUG_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_DEBUG_HEADER

class COptionsPageDebug final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_DEBUG"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();
};

#endif
