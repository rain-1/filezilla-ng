#include <filezilla.h>
#include "recursive_operation_status.h"

#include "recursive_operation.h"

CRecursiveOperationStatus::CRecursiveOperationStatus(wxWindow* parent, CState* pState)
	: wxWindow(parent, wxID_ANY)
	, CStateEventHandler(pState)
{
	Hide();
	SetBackgroundStyle(wxBG_STYLE_SYSTEM);

	m_pState->RegisterHandler(this, STATECHANGE_RECURSION_STATUS);

	new wxStaticText(this, wxID_ANY, _T("Recursive operation in progress"), wxPoint(5, 5));
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
	bool show = m_pState->GetRecursiveOperationHandler()->GetOperationMode() != CRecursiveOperation::recursive_none;
	if (IsShown() != show) {
		Show(show);
	}
}
