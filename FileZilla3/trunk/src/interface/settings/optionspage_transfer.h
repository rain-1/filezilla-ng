#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_TRANSFER_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_TRANSFER_HEADER

class COptionsPageTransfer final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_TRANSFER"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:
	DECLARE_EVENT_TABLE()
	void OnToggleSpeedLimitEnable(wxCommandEvent& event);
};

#endif
