#ifndef FILEZILLA_INTERFACE_CLEARPRIVATEDATA_HEADER
#define FILEZILLA_INTERFACE_CLEARPRIVATEDATA_HEADER

#include "dialogex.h"

#include <wx/timer.h>

class CMainFrame;
class CClearPrivateDataDialog final : public wxDialogEx
{
public:

	static CClearPrivateDataDialog* Create(CMainFrame* pMainFrame) { return new CClearPrivateDataDialog(pMainFrame); }
	void Run();

	void Delete();

protected:
	CClearPrivateDataDialog(CMainFrame* pMainFrame);
	virtual ~CClearPrivateDataDialog() = default;

	bool ClearReconnect();

	void RemoveXmlFile(const wxString& name);

	CMainFrame* const m_pMainFrame;

	wxTimer m_timer;

	DECLARE_EVENT_TABLE()
	void OnTimer(wxTimerEvent& event);
};

#endif
