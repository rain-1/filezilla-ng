#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_LANGUAGE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_LANGUAGE_HEADER

class COptionsPageLanguage final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_LANGUAGE"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

	virtual bool OnDisplayedFirstTime();

	struct _locale_info { wxString name; wxString code; };

protected:
	void GetLocales();

	DECLARE_EVENT_TABLE()

	std::vector<_locale_info> m_locale;
};

#endif
