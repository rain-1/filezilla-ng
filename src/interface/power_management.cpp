#include <filezilla.h>
#include "power_management.h"
#include "Mainfrm.h"
#include "Options.h"
#include "queue.h"
#ifdef WITH_LIBDBUS
#include "../dbus/power_management_inhibitor.h"
#endif

CPowerManagement* CPowerManagement::m_pPowerManagement = 0;

#ifdef __WXMAC__
extern "C" {
	void const* PowerManagmentImpl_SetBusy();
	void PowerManagmentImpl_SetIdle(void const* activity);
}
#endif

void CPowerManagement::Create(CMainFrame* pMainFrame)
{
	if (!m_pPowerManagement) {
		m_pPowerManagement = new CPowerManagement(pMainFrame);
	}
}

void CPowerManagement::Destroy()
{
	delete m_pPowerManagement;
	m_pPowerManagement = 0;
}

CPowerManagement::CPowerManagement(CMainFrame* pMainFrame)
{
	m_pMainFrame = pMainFrame;

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_QUEUEPROCESSING, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_REMOTE_IDLE, false);

	m_busy = false;

#ifdef WITH_LIBDBUS
	m_inhibitor = new CPowerManagementInhibitor();
#endif
}

CPowerManagement::~CPowerManagement()
{
	DoSetIdle();
#ifdef WITH_LIBDBUS
	delete m_inhibitor;
#endif
}

void CPowerManagement::OnStateChange(CState*, t_statechange_notifications, wxString const&, void const*)
{
	auto const queue = m_pMainFrame->GetQueue();
	if (queue && queue->IsActive()) {
		DoSetBusy();
		return;
	}

	const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();
	for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter) {
		if (!(*iter)->IsRemoteIdle()) {
			DoSetBusy();
			return;
		}
	}

	DoSetIdle();
}

void CPowerManagement::DoSetBusy()
{
	if (m_busy) {
		return;
	}

	if (!COptions::Get()->GetOptionVal(OPTION_PREVENT_IDLESLEEP)) {
		return;
	}

	m_busy = true;

#ifdef __WXMSW__
	SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
#elif defined(WITH_LIBDBUS)
	m_inhibitor->RequestBusy();
#elif defined(__WXMAC__)
	activity_ = PowerManagmentImpl_SetBusy();
	m_busy = activity_ != 0;
#endif
}

void CPowerManagement::DoSetIdle()
{
	if (!m_busy) {
		return;
	}
	m_busy = false;

#ifdef __WXMSW__
	SetThreadExecutionState(ES_CONTINUOUS);
#elif defined(WITH_LIBDBUS)
	m_inhibitor->RequestIdle();
#elif defined(__WXMAC__)
	PowerManagmentImpl_SetIdle(activity_);
	activity_ = 0;
#endif
}

bool CPowerManagement::IsSupported()
{
#ifdef __WXMSW__
	return true;
#endif
#if defined(__WXMAC__)
	return true;
#endif
#ifdef WITH_LIBDBUS
	return true;
#endif

	return false;
}
