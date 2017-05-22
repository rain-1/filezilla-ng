#ifndef FILEZILLA_INTERFACE_QUICKCONNECTBAR_HEADER
#define FILEZILLA_INTERFACE_QUICKCONNECTBAR_HEADER

#include "serverdata.h"

class CMainFrame;
class CQuickconnectBar final : public wxPanel
{
public:
	CQuickconnectBar();

	bool Create(CMainFrame* pParent);

	void ClearFields();

protected:
	// Only valid while menu is being displayed
	std::deque<ServerWithCredentials> m_recentServers;

	DECLARE_EVENT_TABLE()
	void OnQuickconnect(wxCommandEvent& event);
	void OnQuickconnectDropdown(wxCommandEvent& event);
	void OnMenu(wxCommandEvent& event);

	wxTextCtrl* m_pHost;
	wxTextCtrl* m_pUser;
	wxTextCtrl* m_pPass;
	wxTextCtrl* m_pPort;

	CMainFrame *m_pMainFrame;
};


#endif
