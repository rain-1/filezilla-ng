#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_PASSIVE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_PASSIVE_HEADER

class COptionsPageConnectionPassive final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_CONNECTION_PASSIVE"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();
};

#endif
