#include <filezilla.h>
#include "recursive_operation_status.h"

#include "recursive_operation.h"
#include "themeprovider.h"

#include <wx/bmpbuttn.h>
#include <wx/dcclient.h>

BEGIN_EVENT_TABLE(CRecursiveOperationStatus, wxWindow)
EVT_PAINT(CRecursiveOperationStatus::OnPaint)
EVT_BUTTON(wxID_ANY, CRecursiveOperationStatus::OnCancel)
END_EVENT_TABLE()

CRecursiveOperationStatus::CRecursiveOperationStatus(wxWindow* parent, CState* pState)
	: wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
	, CStateEventHandler(pState)
{
	Hide();
	SetBackgroundStyle(wxBG_STYLE_SYSTEM);

	m_pState->RegisterHandler(this, STATECHANGE_RECURSION_STATUS);

	m_pTextCtrl[0] = new wxStaticText(this, wxID_ANY, _T("Recursive operation in progress"), wxPoint(10, 10));
	m_pTextCtrl[1] = new wxStaticText(this, wxID_ANY, _T("Recursive operation in progress"), wxPoint(10, 20));

	int const textHeight = m_pTextCtrl[0]->GetSize().GetHeight();

	// Set own size
	wxSize size = GetSize();
	size.SetHeight(textHeight * 3.5);
	SetSize(size);

	// Position stop button
	wxBitmap bmp = CThemeProvider::Get()->GetBitmap(_T("ART_CANCEL"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall));
	wxSize s = bmp.GetSize();
	s.IncBy(s.GetHeight() / 2);
	auto button = new wxBitmapButton(this, wxID_ANY, bmp, wxPoint(10, (size.GetHeight() - s.GetHeight()) / 2), s);
	button->SetToolTip(_("Stop recursive operation"));

	// Position labels
	m_pTextCtrl[0]->SetPosition(wxPoint(20 + s.GetWidth(), textHeight /2));
	m_pTextCtrl[1]->SetPosition(wxPoint(20 + s.GetWidth(), textHeight * 2));
}


bool CRecursiveOperationStatus::Show(bool show)
{
	bool ret = wxWindow::Show(show);

	wxSizeEvent evt;
	GetParent()->ProcessWindowEvent(evt);

	return ret;
}

void CRecursiveOperationStatus::OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString&, const void*)
{
	auto const mode = m_pState->GetRecursiveOperationHandler()->GetOperationMode();
	bool show = mode != CRecursiveOperation::recursive_none && mode != CRecursiveOperation::recursive_list;
	if (IsShown() != show) {
		Show(show);
	}

	wxString text;
	if (show) {
		switch (mode) {
		case CRecursiveOperation::recursive_addtoqueue:
		case CRecursiveOperation::recursive_addtoqueue_flatten:
		case CRecursiveOperation::recursive_download:
		case CRecursiveOperation::recursive_download_flatten:
			text = _("Recursively adding files to queue.");
			break;
		case CRecursiveOperation::recursive_delete:
			text = _("Recursively deleting files and directories.");
			break;
		case CRecursiveOperation::recursive_chmod:
			text = _("Recursively changing permissions.");
			break;
		default:
			break;
		}
		m_pTextCtrl[0]->SetLabel(text);

		unsigned long long const countFiles = static_cast<unsigned long long>(m_pState->GetRecursiveOperationHandler()->GetProcessedFiles());
		unsigned long long const countDirs = static_cast<unsigned long long>(m_pState->GetRecursiveOperationHandler()->GetProcessedDirectories());
		const wxString files = wxString::Format(wxPLURAL_LL("%llu file", "%llu files", countFiles), countFiles);
		const wxString dirs = wxString::Format(wxPLURAL_LL("%llu directory", "%llu directories", countDirs), countDirs);
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

void CRecursiveOperationStatus::OnCancel(wxCommandEvent& ev)
{
	m_pState->GetRecursiveOperationHandler()->StopRecursiveOperation();
	m_pState->RefreshRemote();
}