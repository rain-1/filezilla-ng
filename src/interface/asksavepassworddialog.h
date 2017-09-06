#ifndef FILEZILLA_INTERFACE_ASKSAVEPASSWORDDIALOG_HEADER
#define FILEZILLA_INTERFACE_ASKSAVEPASSWORDDIALOG_HEADER

#include "dialogex.h"

class CAskSavePasswordDialog final : public wxDialogEx
{
public:
	static bool Run(wxWindow* parent);
private:
	bool Create(wxWindow* parent);

	void OnOk(wxCommandEvent& event);
};

#endif
