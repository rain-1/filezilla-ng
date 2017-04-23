#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_DATEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_DATEFORMATTING_HEADER

class COptionsPageDateFormatting final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_DATEFORMATTING"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnRadioChanged(wxCommandEvent& event);
};

#endif
