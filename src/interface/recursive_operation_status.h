#ifndef FZ_RECURSIVE_OPERATION_STATUS_HEADER
#define FZ_RECURSIVE_OPERATION_STATUS_HEADER

#include "state.h"

#include <wx/timer.h>

class CRecursiveOperationStatus final : public wxWindow, public CStateEventHandler
{
public:
	CRecursiveOperationStatus(wxWindow* pParent, CState& state, bool local);

	CRecursiveOperationStatus(CRecursiveOperationStatus const&) = delete;
private:
	virtual bool Show(bool show);

	virtual void OnStateChange(t_statechange_notifications notification, const wxString&, const void*);

	void UpdateText();

	wxStaticText* m_pTextCtrl[2];

	wxTimer m_timer;
	bool m_changed{};

	wxDECLARE_EVENT_TABLE();
	void OnPaint(wxPaintEvent& ev);
	void OnCancel(wxCommandEvent& ev);
	void OnTimer(wxTimerEvent&);

	bool const m_local;
};

#endif
