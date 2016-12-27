#ifndef __SEARCH_H__
#define __SEARCH_H__

#include "filter_conditions_dialog.h"
#include "local_recursive_operation.h"
#include "state.h"
#include <set>

class CWindowStateManager;
class CSearchDialogFileList;
class CQueueView;
class CFilelistStatusBar;
class CSearchDialog final : protected CFilterConditionsDialog, public CStateEventHandler
{
	friend class CSearchDialogFileList;
public:
	enum class search_mode
	{
		none,
		local,
		remote
	};

	CSearchDialog(wxWindow* parent, CState& state, CQueueView* pQueue);
	virtual ~CSearchDialog();

	bool Load();
	void Run();

protected:
	void ProcessDirectoryListing(std::shared_ptr<CDirectoryListing> const& listing);
	void ProcessDirectoryListing(CLocalRecursiveOperation::listing const& listing);

	void SetCtrlState();

	void SaveConditions();
	void LoadConditions();

	wxWindow* m_parent;
	CSearchDialogFileList *m_results{};
	CQueueView* m_pQueue;

	virtual void OnStateChange(t_statechange_notifications notification, const wxString& data, const void* data2);

	CWindowStateManager* m_pWindowStateManager{};

	CFilter m_search_filter;

	search_mode m_searching{};

	CServerPath m_original_dir;

	DECLARE_EVENT_TABLE()
	void OnSearch(wxCommandEvent& event);
	void OnStop(wxCommandEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);
	void OnDownload(wxCommandEvent&);
	void OnUpload(wxCommandEvent&);
	void OnEdit(wxCommandEvent&);
	void OnDelete(wxCommandEvent&);
	void OnCharHook(wxKeyEvent& event);
	void OnChangeSearchMode(wxCommandEvent&);
	void OnGetUrl(wxCommandEvent& event);

	std::set<CServerPath> m_visited;

	CLocalPath m_local_search_root;
	CServerPath m_remote_search_root;
};

#endif //__SEARCH_H__
