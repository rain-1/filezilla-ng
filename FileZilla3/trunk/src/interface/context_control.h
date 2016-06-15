#ifndef __CONTEXT_CONTROL_H__
#define __CONTEXT_CONTROL_H__

#include <wx/aui/auibook.h>
#include "state.h"

#include <memory>

class wxAuiNotebookEx;
class CLocalListView;
class CLocalTreeView;
class CMainFrame;
class CRemoteListView;
class CRemoteTreeView;
class CView;
class CViewHeader;
class CSplitterWindowEx;
class CState;

class CContextControl final : public wxSplitterWindow, public CGlobalStateEventHandler
{
public:
	struct _context_controls
	{
		bool used() const { return pViewSplitter != 0; }

		// List of all windows and controls assorted with a context
		CView* pLocalTreeViewPanel{};
		CView* pLocalListViewPanel{};
		CLocalTreeView* pLocalTreeView{};
		CLocalListView* pLocalListView{};
		CView* pRemoteTreeViewPanel{};
		CView* pRemoteListViewPanel{};
		CRemoteTreeView* pRemoteTreeView{};
		CRemoteListView* pRemoteListView{};
		CViewHeader* pLocalViewHeader{};
		CViewHeader* pRemoteViewHeader{};

		CSplitterWindowEx* pViewSplitter{}; // Contains local and remote splitters
		CSplitterWindowEx* pLocalSplitter{};
		CSplitterWindowEx* pRemoteSplitter{};

		CState* pState{};
	};

	CContextControl(CMainFrame& mainFrame);
	virtual ~CContextControl();

	void Create(wxWindow* parent);

	void CreateTab();
	bool CloseTab(int tab);

	_context_controls* GetCurrentControls();
	_context_controls* GetControlsFromState(CState* pState);

	int GetCurrentTab() const;
	int GetTabCount() const;
	_context_controls* GetControlsFromTabIndex(int i);

	bool SelectTab(int i);
	void AdvanceTab(bool forward);

protected:

	void CreateContextControls(CState& state);

	std::vector<_context_controls> m_context_controls;
	int m_current_context_controls{-1};

	wxAuiNotebookEx* m_tabs{};
	int m_right_clicked_tab{-1};
	CMainFrame& m_mainFrame;

protected:
	DECLARE_EVENT_TABLE()
	void OnTabRefresh(wxCommandEvent&);
	void OnTabChanged(wxAuiNotebookEvent& event);
	void OnTabClosing(wxAuiNotebookEvent& event);
	void OnTabClosing_Deferred(wxCommandEvent& event);
	void OnTabBgDoubleclick(wxAuiNotebookEvent&);
	void OnTabRightclick(wxAuiNotebookEvent& event);
	void OnTabContextClose(wxCommandEvent& event);
	void OnTabContextCloseOthers(wxCommandEvent& event);
	void OnTabContextNew(wxCommandEvent&);

	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, const wxString&, const void*);
};

#endif //__CONTEX_CONTROL_H__
