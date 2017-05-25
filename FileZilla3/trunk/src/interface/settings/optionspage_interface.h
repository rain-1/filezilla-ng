#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_INTERFACE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_INTERFACE_HEADER

class COptionsPageInterface final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_INTERFACE"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

private:
	void SavePasswordOption();

	DECLARE_EVENT_TABLE()
	void OnLayoutChange(wxCommandEvent& event);
};

#endif
