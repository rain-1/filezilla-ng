#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_UPDATECHECK_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_UPDATECHECK_HEADER

#if FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK

class COptionsPageUpdateCheck final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_UPDATECHECK"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:
	void OnRunUpdateCheck(wxCommandEvent&);

	DECLARE_EVENT_TABLE()
};

#endif

#endif
