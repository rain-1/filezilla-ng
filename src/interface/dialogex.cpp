#include <filezilla.h>
#include "dialogex.h"
#include "msgbox.h"

#ifdef __WXMAC__
#include "filezillaapp.h"
#endif

BEGIN_EVENT_TABLE(wxDialogEx, wxDialog)
EVT_CHAR_HOOK(wxDialogEx::OnChar)
END_EVENT_TABLE()

int wxDialogEx::m_shown_dialogs = 0;

#ifdef __WXMAC__
static int const pasteId = wxNewId();
static int const selectAllId = wxNewId();

extern wxTextEntry* GetSpecialTextEntry(wxWindow*, wxChar);

bool wxDialogEx::ProcessEvent(wxEvent& event)
{
	if(event.GetEventType() != wxEVT_MENU) {
		return wxDialog::ProcessEvent(event);
	}

	wxTextEntry* e = GetSpecialTextEntry(FindFocus(), 'V');
	if( e && event.GetId() == pasteId ) {
		e->Paste();
		return true;
	}
	else if( e && event.GetId() == selectAllId ) {
		e->SelectAll();
		return true;
	}
	else {
		return wxDialog::ProcessEvent(event);
	}
}
#endif

void wxDialogEx::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		wxCommandEvent cmdEvent(wxEVT_COMMAND_BUTTON_CLICKED, wxID_CANCEL);
		ProcessEvent(cmdEvent);
	}
	else
		event.Skip();
}

bool wxDialogEx::Load(wxWindow* pParent, const wxString& name)
{
	SetParent(pParent);
	if (!wxXmlResource::Get()->LoadDialog(this, pParent, name))
		return false;

	//GetSizer()->Fit(this);
	//GetSizer()->SetSizeHints(this);

#ifdef __WXMAC__
	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CMD, 'V', pasteId);
	entries[1].Set(wxACCEL_CMD, 'A', selectAllId);
	wxAcceleratorTable accel(sizeof(entries) / sizeof(wxAcceleratorEntry), entries);
	SetAcceleratorTable(accel);
#endif

	return true;
}

bool wxDialogEx::SetChildLabel(int id, const wxString& label, unsigned long maxLength)
{
	wxWindow* pText = FindWindow(id);
	if (!pText)
		return false;

	if (!maxLength)
		pText->SetLabel(label);
	else {
		wxString wrapped = label;
		WrapText(this, wrapped, maxLength);
		pText->SetLabel(wrapped);
	}

	return true;
}

bool wxDialogEx::SetChildLabel(char const* id, const wxString& label, unsigned long maxLength)
{
	return SetChildLabel(XRCID(id), label, maxLength);
}

wxString wxDialogEx::GetChildLabel(int id)
{
	auto pText = dynamic_cast<wxStaticText*>(FindWindow(id));
	if (!pText)
		return wxString();

	return pText->GetLabel();
}

int wxDialogEx::ShowModal()
{
	CenterOnParent();

#ifdef __WXMSW__
	// All open menus need to be closed or app will become unresponsive.
	::EndMenu();

	// For same reason release mouse capture.
	// Could happen during drag&drop with notification dialogs.
	::ReleaseCapture();
#endif

	m_shown_dialogs++;

	int ret = wxDialog::ShowModal();

	m_shown_dialogs--;

	return ret;
}

bool wxDialogEx::ReplaceControl(wxWindow* old, wxWindow* wnd)
{
	if( !GetSizer()->Replace(old, wnd, true) ) {
		return false;
	}
	old->Destroy();

	return true;
}

bool wxDialogEx::CanShowPopupDialog()
{
	if (m_shown_dialogs != 0 || IsShowingMessageBox()) {
		// There already is a dialog or message box showing
		return false;
	}

	wxMouseState mouseState = wxGetMouseState();
	if (mouseState.LeftIsDown() || mouseState.MiddleIsDown() || mouseState.RightIsDown()) {
		// Displaying a dialog while the user is clicking is extremely confusing, don't do it.
		return false;
	}
#ifdef __WXMSW__
	// During a drag & drop we cannot show a dialog. Doing so can render the program unresponsive
	if (GetCapture()) {
		return false;
	}
#endif

#ifdef __WXMAC__
	if (wxGetApp().MacGetCurrentEvent()) {
		// We're inside an event handler for a native mac event, such as a popup menu
		return false;
	}
#endif

	return true;
}

void wxDialogEx::InitDialog()
{
#ifdef __WXGTK__
	// Some controls only report proper size after the call to Show(), e.g.
	// wxStaticBox::GetBordersForSizer is affected by this.
	// Re-fit window to compensate.
	wxSizer* s = GetSizer();
	if( s ) {
		wxSize min = GetMinClientSize();
		wxSize smin = s->GetMinSize();
		if( min.x < smin.x || min.y < smin.y ) {
			s->Fit(this);
			SetMinSize(GetSize());
		}
	}
#endif

	wxDialog::InitDialog();
}
