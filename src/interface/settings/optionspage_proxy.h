#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_PROXY_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_PROXY_HEADER

class COptionsPageProxy final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_CONNECTION_PROXY"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnProxyTypeChanged(wxCommandEvent& event);
};

#endif
