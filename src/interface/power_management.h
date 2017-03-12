#ifndef FILEZILLA_INTERFACE_POWER_MANAGEMENT_HEADER
#define FILEZILLA_INTERFACE_POWER_MANAGEMENT_HEADER

// This simple class prevents the system from entering idle sleep
// while FileZilla is busy

#include "state.h"

class CMainFrame;
#ifdef WITH_LIBDBUS
class CPowerManagementInhibitor;
#endif

class CPowerManagement final : protected CGlobalStateEventHandler
{
public:
	static void Create(CMainFrame* pMainFrame);
	static void Destroy();

	static bool IsSupported();
protected:
	CPowerManagement(CMainFrame* pMainFrame);
	virtual ~CPowerManagement();

	static CPowerManagement* m_pPowerManagement;

	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, wxString const& data, void const* data2);

	void DoSetBusy();
	void DoSetIdle();

	bool m_busy;

	CMainFrame* m_pMainFrame;

#ifdef WITH_LIBDBUS
	CPowerManagementInhibitor *m_inhibitor;
#elif defined(__WXMAC__)
	void const* activity_{};
#endif
};

#endif
