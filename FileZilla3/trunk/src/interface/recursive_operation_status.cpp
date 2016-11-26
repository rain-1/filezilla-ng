#include <filezilla.h>
#include "recursive_operation_status.h"

#include "local_recursive_operation.h"
#include "remote_recursive_operation.h"
#include "themeprovider.h"

#include <wx/bmpbuttn.h>
#include <wx/dcclient.h>

BEGIN_EVENT_TABLE(CRecursiveOperationStatus, wxWindow)
EVT_PAINT(CRecursiveOperationStatus::OnPaint)
EVT_BUTTON(wxID_ANY, CRecursiveOperationStatus::OnCancel)
EVT_TIMER(wxID_ANY, CRecursiveOperationStatus::OnTimer)
END_EVENT_TABLE()

CRecursiveOperationStatus::CRecursiveOperationStatus(wxWindow* parent, CState& state, bool local)
	: wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
	, CStateEventHandler(state)
	, m_local(local)
{
	Hide();
	SetBackgroundStyle(wxBG_STYLE_SYSTEM);

	m_state.RegisterHandler(this, local ? STATECHANGE_LOCAL_RECURSION_STATUS : STATECHANGE_REMOTE_RECURSION_STATUS);

	m_pTextCtrl[0] = new wxStaticText(this, wxID_ANY, _T("Recursive operation in progress"), wxPoint(10, 10));
	m_pTextCtrl[1] = new wxStaticText(this, wxID_ANY, _T("Recursive operation in progress"), wxPoint(10, 20));

	int const textHeight = m_pTextCtrl[0]->GetSize().GetHeight();

	// Set own size
	wxSize size = GetSize();
	size.SetHeight(textHeight * 3.5);
	SetSize(size);

	// Position stop button
	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(_T("ART_CANCEL"), wxART_OTHER, CThemeProvider::GetIconSize(iconSize24, false));
	wxSize s = bmp.GetScaledSize();
	s.IncBy(2);
	s.IncBy(s.GetHeight() / 2);
	auto button = new wxBitmapButton(this, wxID_ANY, bmp, wxPoint(10, (size.GetHeight() - s.GetHeight()) / 2), s);
	button->SetToolTip(_("Stop recursive operation"));

	// Position labels
	m_pTextCtrl[0]->SetPosition(wxPoint(20 + s.GetWidth(), textHeight /2));
	m_pTextCtrl[1]->SetPosition(wxPoint(20 + s.GetWidth(), textHeight * 2));

	m_timer.SetOwner(this);
}


bool CRecursiveOperationStatus::Show(bool show)
{
	bool ret = wxWindow::Show(show);

	wxSizeEvent evt;
	GetParent()->ProcessWindowEvent(evt);

	return ret;
}

void CRecursiveOperationStatus::OnStateChange(t_statechange_notifications, const wxString&, const void*)
{
	CRecursiveOperation* op = m_local ? static_cast<CRecursiveOperation*>(m_state.GetLocalRecursiveOperation()) : static_cast<CRecursiveOperation*>(m_state.GetRemoteRecursiveOperation());
	auto mode = CRecursiveOperation::recursive_none;
	if (op) {
		mode = op->GetOperationMode();
	}

	bool show = mode != CRecursiveOperation::recursive_none && mode != CRecursiveOperation::recursive_list;
	if (IsShown() != show) {
		Show(show);
	}

	if (show) {
		if (!m_timer.IsRunning()) {
			UpdateText();
			m_timer.Start(200, true);
		}
		else {
			m_changed = true;
		}
	}
}

void CRecursiveOperationStatus::UpdateText()
{
	CRecursiveOperation* operation = m_local ? static_cast<CRecursiveOperation*>(m_state.GetLocalRecursiveOperation()) : static_cast<CRecursiveOperation*>(m_state.GetRemoteRecursiveOperation());

	m_changed = false;
	wxString text;

	auto const mode = operation->GetOperationMode();
	bool show = mode != CRecursiveOperation::recursive_none && mode != CRecursiveOperation::recursive_list;
	if (show) {
		switch (mode) {
		case CRecursiveOperation::recursive_transfer:
		case CRecursiveOperation::recursive_transfer_flatten:
			text = _("Recursively adding files to queue.");
			break;
		case CRecursiveOperation::recursive_delete:
			text = _("Recursively deleting files and directories.");
			break;
		case CRecursiveOperation::recursive_chmod:
			text = _("Recursively changing permissions.");
			break;
		case CRecursiveOperation::recursive_synchronize_download:
		case CRecursiveOperation::recursive_synchronize_upload:
			text = _("Synchronizing directories.");
			break;
		default:
			break;
		}
		m_pTextCtrl[0]->SetLabel(text);

		unsigned long long const countFiles = static_cast<unsigned long long>(operation->GetProcessedFiles());
		unsigned long long const countDirs = static_cast<unsigned long long>(operation->GetProcessedDirectories());
		std::wstring const files = fz::sprintf(fztranslate("%llu file", "%llu files", countFiles), countFiles);
		std::wstring const dirs = fz::sprintf(fztranslate("%llu directory", "%llu directories", countDirs), countDirs);
		// @translator: Example: Processed 5 files in 1 directory
		m_pTextCtrl[1]->SetLabel(wxString::Format(_("Processed %s in %s."), files, dirs));
	}
}

void CRecursiveOperationStatus::OnPaint(wxPaintEvent&)
{
	wxPaintDC dc(this);

	wxSize const s = GetClientSize();

	dc.DrawLine(wxPoint(0, 0), wxPoint(s.GetWidth(), 0));
}

void CRecursiveOperationStatus::OnCancel(wxCommandEvent&)
{
	if (m_local) {
		m_state.GetLocalRecursiveOperation()->StopRecursiveOperation();
	}
	else {
		m_state.GetRemoteRecursiveOperation()->StopRecursiveOperation();
		m_state.RefreshRemote();
	}
}

void CRecursiveOperationStatus::OnTimer(wxTimerEvent&)
{
	if (m_changed) {
		UpdateText();
		m_timer.Start(200, true);
	}
}
