#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"

bool COptionsPage::CreatePage(COptions* pOptions, CSettingsDialog* pOwner, wxWindow* parent, wxSize& maxSize)
{
	m_pOwner = pOwner;
	m_pOptions = pOptions;

	if (!wxXmlResource::Get()->LoadPanel(this, parent, GetResourceName())) {
		return false;
	}

	m_was_selected = false;

	UpdateMaxPageSize(maxSize);

	return true;
}

void COptionsPage::UpdateMaxPageSize(wxSize& maxSize)
{
	wxSize size = GetSize();

#ifdef __WXGTK__
	// wxStaticBox draws its own border coords -1.
	// Adjust this window so that the left border is fully visible.
	Move(1, 0);
	size.x += 1;
#endif

	if (size.GetWidth() > maxSize.GetWidth()) {
		maxSize.SetWidth(size.GetWidth());
	}
	if (size.GetHeight() > maxSize.GetHeight()) {
		maxSize.SetHeight(size.GetHeight());
	}
}

void COptionsPage::SetCheck(int id, bool checked, bool& failure)
{
	auto pCheckBox = dynamic_cast<wxCheckBox*>(FindWindow(id));
	if (!pCheckBox) {
		failure = true;
		return;
	}

	pCheckBox->SetValue(checked);
}

void COptionsPage::SetCheckFromOption(int control_id, int option_id, bool& failure)
{
	SetCheck(control_id, m_pOptions->GetOptionVal(option_id) != 0, failure);
}

bool COptionsPage::GetCheck(int id) const
{
	auto pCheckBox = dynamic_cast<wxCheckBox*>(FindWindow(id));
	wxASSERT(pCheckBox);

	return pCheckBox ? pCheckBox->GetValue() : false;
}

void COptionsPage::SetOptionFromCheck(int control_id, int option_id)
{
	m_pOptions->SetOption(option_id, GetCheck(control_id) ? 1 : 0);
}

void COptionsPage::SetTextFromOption(int ctrlId, int optionId, bool& failure)
{
	if (ctrlId == -1) {
		failure = true;
		return;
	}

	auto pTextCtrl = dynamic_cast<wxTextCtrl*>(FindWindow(ctrlId));
	if (!pTextCtrl) {
		failure = true;
		return;
	}

	const wxString& text = m_pOptions->GetOption(optionId);
	pTextCtrl->ChangeValue(text);
}

wxString COptionsPage::GetText(int id) const
{
	auto pTextCtrl = dynamic_cast<wxTextCtrl*>(FindWindow(id));
	wxASSERT(pTextCtrl);

	return pTextCtrl ? pTextCtrl->GetValue() : wxString();
}

bool COptionsPage::SetText(int id, const wxString& text, bool& failure)
{
	auto pTextCtrl = dynamic_cast<wxTextCtrl*>(FindWindow(id));
	if (!pTextCtrl) {
		failure = true;
		return false;
	}

	pTextCtrl->ChangeValue(text);

	return true;
}

void COptionsPage::SetRCheck(int id, bool checked, bool& failure)
{
	auto pRadioButton = dynamic_cast<wxRadioButton*>(FindWindow(id));
	if (!pRadioButton) {
		failure = true;
		return;
	}

	pRadioButton->SetValue(checked);
}

bool COptionsPage::GetRCheck(int id) const
{
	auto pRadioButton = dynamic_cast<wxRadioButton*>(FindWindow(id));
	wxASSERT(pRadioButton);

	return pRadioButton ? pRadioButton->GetValue() : false;
}

void COptionsPage::SetStaticText(int id, const wxString& text, bool& failure)
{
	auto pStaticText = dynamic_cast<wxStaticText*>(FindWindow(id));
	if (!pStaticText) {
		failure = true;
		return;
	}

	pStaticText->SetLabel(text);
}

wxString COptionsPage::GetStaticText(int id) const
{
	auto pStaticText = dynamic_cast<wxStaticText*>(FindWindow(id));
	wxASSERT(pStaticText);

	return pStaticText ? pStaticText->GetLabel() : wxString();
}

void COptionsPage::ReloadSettings()
{
	m_pOwner->LoadSettings();
}

void COptionsPage::SetOptionFromText(int ctrlId, int optionId)
{
	const wxString& value = GetText(ctrlId);
	m_pOptions->SetOption(optionId, value.ToStdWstring());
}

void COptionsPage::SetIntOptionFromText(int ctrlId, int optionId)
{
	const wxString& value = GetText(ctrlId);

	long n;
	wxCHECK_RET(value.ToLong(&n), _T("Some options page did not validate user input!"));

	m_pOptions->SetOption(optionId, n);
}

void COptionsPage::SetChoice(int id, int selection, bool& failure)
{
	if (selection < -1) {
		failure = true;
		return;
	}

	auto pChoice = dynamic_cast<wxChoice*>(FindWindow(id));
	if (!pChoice) {
		failure = true;
		return;
	}

	if (selection >= (int)pChoice->GetCount()) {
		failure = true;
		return;
	}

	pChoice->SetSelection(selection);
}

int COptionsPage::GetChoice(int id) const
{
	auto pChoice = dynamic_cast<wxChoice*>(FindWindow(id));
	wxASSERT(pChoice);
	if (!pChoice)
		return 0;

	return pChoice->GetSelection();
}

bool COptionsPage::DisplayError(const wxString& controlToFocus, const wxString& error)
{
	int id = wxXmlResource::GetXRCID(controlToFocus);
	if (id == -1)
		DisplayError(0, error);
	else
		DisplayError(FindWindow(id), error);

	return false;
}

bool COptionsPage::DisplayError(wxWindow* pWnd, const wxString& error)
{
	if (pWnd)
		pWnd->SetFocus();

	wxMessageBoxEx(error, _("Failed to validate settings"), wxICON_EXCLAMATION, this);

	return false;
}

bool COptionsPage::Display()
{
	if (!m_was_selected)
	{
		if (!OnDisplayedFirstTime())
			return false;
		m_was_selected = true;
	}
	Show();

	return true;
}

bool COptionsPage::OnDisplayedFirstTime()
{
	return true;
}
