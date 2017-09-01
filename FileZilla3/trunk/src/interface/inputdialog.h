#ifndef FILEZILLA_INTERFACE_INPUTDIALOG_HEADER
#define FILEZILLA_INTERFACE_INPUTDIALOG_HEADER

#include "dialogex.h"

class CInputDialog final : public wxDialogEx
{
public:
	CInputDialog();
	virtual ~CInputDialog() {}

	bool Create(wxWindow* parent, const wxString& title, wxString text);

	bool SetPasswordMode(bool password);
	void AllowEmpty(bool allowEmpty);

	void SetValue(const wxString& value);
	wxString GetValue() const;

	bool SelectText(int start, int end);

protected:
	DECLARE_EVENT_TABLE()
	void OnValueChanged(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);

	bool m_allowEmpty;
	wxTextCtrl* m_pTextCtrl;
};

#endif
