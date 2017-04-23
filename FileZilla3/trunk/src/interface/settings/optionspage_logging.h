#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_LOGGING_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_LOGGING_HEADER

class COptionsPageLogging final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_LOGGING"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:
	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnBrowse(wxCommandEvent& event);
	void OnCheck(wxCommandEvent& event);
};

#endif
