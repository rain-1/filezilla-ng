#ifndef FILEZILLA_UPDATE_DIALOG_HEADER
#define FILEZILLA_UPDATE_DIALOG_HEADER

#include "dialogex.h"
#include "updater.h"

class wxPanel;
class wxWindow;

class CUpdateDialog : public wxDialogEx, protected CUpdateHandler
{
public:
	CUpdateDialog(wxWindow* parent, CUpdater& updater);
	virtual ~CUpdateDialog();

	virtual int ShowModal();

protected:
	virtual void UpdaterStateChanged( UpdaterState s, build const& v );

	void LoadPanel(wxString const& name);
	void Wrap();

	wxWindow* parent_;
	CUpdater& updater_;

	std::vector<wxPanel*> panels_;

	wxTimer timer_;
	
	DECLARE_EVENT_TABLE()
	void OnInstall(wxCommandEvent& ev);
	void OnTimer(wxTimerEvent& ev);
};

#endif
