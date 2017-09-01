#ifndef FILEZILLA_INTERFACE_SPEEDLIMITS_DIALOG_HEADER
#define FILEZILLA_INTERFACE_SPEEDLIMITS_DIALOG_HEADER

#include "dialogex.h"

class CSpeedLimitsDialog final : public wxDialogEx
{
public:
	CSpeedLimitsDialog() {}
	virtual ~CSpeedLimitsDialog() {}

	void Run(wxWindow* parent);

protected:
	DECLARE_EVENT_TABLE()
	void OnToggleEnable(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
};

#endif
