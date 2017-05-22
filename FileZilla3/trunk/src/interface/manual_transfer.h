#ifndef FILEZILLA_INTERFACE_MANUAL_TRANSFER_HEADER
#define FILEZILLA_INTERFACE_MANUAL_TRANSFER_HEADER

#include "dialogex.h"
#include "serverdata.h"

class CQueueView;
class CState;
class CManualTransfer final : public wxDialogEx
{
public:
	CManualTransfer(CQueueView* pQueueView);

	void Run(wxWindow* pParent, CState* pState);

protected:
	void DisplayServer();
	bool UpdateServer();
	bool VerifyServer();

	void SetControlState();
	void SetAutoAsciiState();
	void SetServerState();

	bool m_local_file_exists;

	ServerWithCredentials server_;
	ServerWithCredentials lastSite_;

	CState* m_pState;
	CQueueView* m_pQueueView;

	DECLARE_EVENT_TABLE()
	void OnLocalChanged(wxCommandEvent& event);
	void OnLocalBrowse(wxCommandEvent& event);
	void OnRemoteChanged(wxCommandEvent& event);
	void OnDirection(wxCommandEvent& event);
	void OnServerTypeChanged(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnSelectSite(wxCommandEvent& event);
	void OnSelectedSite(wxCommandEvent& event);
	void OnLogontypeSelChanged(wxCommandEvent& event);
};

#endif
