#ifndef FZ_RECURSIVE_OPERATION_STATUS_HEADER
#define FZ_RECURSIVE_OPERATION_STATUS_HEADER

#include "state.h"

class CRecursiveOperationStatus : public wxWindow, public CStateEventHandler
{
public:
	CRecursiveOperationStatus(wxWindow* pParent, CState* pState);

protected:
	virtual bool Show(bool show);

	virtual void OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString&, const void*);

	wxStaticText* m_pTextCtrl[2];

	wxDECLARE_EVENT_TABLE();
	void OnPaint(wxPaintEvent& ev);
	void OnCancel(wxCommandEvent& ev);
};

#endif
