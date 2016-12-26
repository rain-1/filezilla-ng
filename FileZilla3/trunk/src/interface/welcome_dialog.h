#ifndef __WELCOME_DIALOG_H__
#define __WELCOME_DIALOG_H__

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

#endif //__WELCOME_DIALOG_H__
