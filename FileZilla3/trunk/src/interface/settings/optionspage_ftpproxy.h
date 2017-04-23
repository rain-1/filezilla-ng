#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FTPPROXY_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FTPPROXY_HEADER

class COptionsPageFtpProxy final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_CONNECTION_FTP_PROXY"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnProxyTypeChanged(wxCommandEvent& event);
	void OnLoginSequenceChanged(wxCommandEvent& event);
};

#endif
