#ifndef FILEZILLA_INTERFACE_WELCOME_DIALOG_HEADER
#define FILEZILLA_INTERFACE_WELCOME_DIALOG_HEADER

#include "dialogex.h"

#include <wx/timer.h>

class CWelcomeDialog final : public wxDialogEx
{
public:
	CWelcomeDialog() {}
	virtual ~CWelcomeDialog() {}

	bool Run(wxWindow* parent, bool force = false, bool delay = false);

protected:

	void InitFooter(wxString const& resources);

	wxTimer m_delayedShowTimer;

	DECLARE_EVENT_TABLE()
	void OnTimer(wxTimerEvent& event);
};

#endif
