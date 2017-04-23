#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILETYPE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILETYPE_HEADER

class COptionsPageFiletype final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_FILETYPE"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:
	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnRemove(wxCommandEvent& event);
	void OnAdd(wxCommandEvent& event);
	void OnSelChanged(wxListEvent& event);
	void OnTextChanged(wxCommandEvent& event);
};

#endif
