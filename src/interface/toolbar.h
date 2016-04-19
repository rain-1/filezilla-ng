#ifndef __TOOLBAR_H__
#define __TOOLBAR_H__

#include <option_change_event_handler.h>
#include "state.h"

class CMainFrame;

class CToolBar : public wxToolBar, public CGlobalStateEventHandler, public COptionChangeEventHandler
{
public:
	CToolBar() = default;
	virtual ~CToolBar();

	void UpdateToolbarState();

	static CToolBar* Load(CMainFrame* pMainFrame);

	bool ShowTool(int id);
	bool HideTool(int id);

protected:
	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, const wxString& data, const void* data2);
	virtual void OnOptionsChanged(changed_options_t const& options);

	CMainFrame* m_pMainFrame{};

	std::map<int, wxToolBarToolBase*> m_hidden_tools;

	DECLARE_DYNAMIC_CLASS(CToolBar)
};

#endif //__TOOLBAR_H__
