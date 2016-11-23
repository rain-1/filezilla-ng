#ifndef __OPTIONSPAGE_THEMES_H__
#define __OPTIONSPAGE_THEMES_H__

class COptionsPageThemes : public COptionsPage
{
public:
	virtual ~COptionsPageThemes();
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_THEMES"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

	virtual bool CreatePage(COptions* pOptions, CSettingsDialog* pOwner, wxWindow* parent, wxSize& maxSize);

protected:
	bool DisplayTheme(std::wstring const& theme);

	virtual bool OnDisplayedFirstTime();

	DECLARE_EVENT_TABLE()
	void OnThemeChange(wxCommandEvent& event);
};

#endif //__OPTIONSPAGE_THEMES_H__
