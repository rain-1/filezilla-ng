#ifndef FILEZILLA_INTERFACE_SITEMANAGER_DIALOG_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_DIALOG_HEADER

#include "dialogex.h"
#include "sitemanager.h"

class CInterProcessMutex;
class CWindowStateManager;
class CSiteManagerDropTarget;
class CSiteManagerSite;

class CSiteManagerDialog final : public wxDialogEx
{
	friend class CSiteManagerDropTarget;

	DECLARE_EVENT_TABLE()

public:
	struct _connected_site
	{
		ServerWithCredentials server;
		std::wstring old_path;
		std::wstring new_path;
	};

	/// Constructors
	CSiteManagerDialog();
	virtual ~CSiteManagerDialog();

	// Creation. If pServer is set, it will cause a new item to be created.
	bool Create(wxWindow* parent, std::vector<_connected_site> *connected_sites, ServerWithCredentials const* pServer = 0);

	bool GetServer(Site& data, Bookmark& bookmark);

protected:

	bool Verify();
	bool UpdateItem();
	bool UpdateBookmark(Bookmark &bookmark, ServerWithCredentials const& server);
	bool Load();
	bool Save(pugi::xml_node element = pugi::xml_node(), wxTreeItemId treeId = wxTreeItemId());
	bool SaveChild(pugi::xml_node element, wxTreeItemId child);
	void UpdateServer(Site &server, wxString const& name);
	void SetCtrlState();
	bool LoadDefaultSites();

	bool IsPredefinedItem(wxTreeItemId item);

	wxString FindFirstFreeName(const wxTreeItemId &parent, const wxString& name);

	void AddNewSite(wxTreeItemId parent, ServerWithCredentials const& server, bool connected = false);
	void CopyAddServer(ServerWithCredentials const& server);

	void AddNewBookmark(wxTreeItemId parent);

	void RememberLastSelected();

	std::wstring GetSitePath(wxTreeItemId item, bool stripBookmark = true);

	void MarkConnectedSites();
	void MarkConnectedSite(int connected_site);

	void OnOK(wxCommandEvent&);
	void OnCancel(wxCommandEvent&);
	void OnConnect(wxCommandEvent& event);
	void OnNewSite(wxCommandEvent& event);
	void OnNewFolder(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
	void OnSelChanging(wxTreeEvent& event);
	void OnSelChanged(wxTreeEvent& event);
	void OnItemActivated(wxTreeEvent& event);
	void OnBeginDrag(wxTreeEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnCopySite(wxCommandEvent& event);
	void OnContextMenu(wxTreeEvent& event);
	void OnExportSelected(wxCommandEvent&);
	void OnNewBookmark(wxCommandEvent&);
	void OnBookmarkBrowse(wxCommandEvent&);

	CInterProcessMutex* m_pSiteManagerMutex{};

	wxTreeItemId m_predefinedSites;
	wxTreeItemId m_ownSites;

	wxTreeItemId m_dropSource;

	wxTreeItemId m_contextMenuItem;

	bool MoveItems(wxTreeItemId source, wxTreeItemId target, bool copy);

protected:
	CWindowStateManager* m_pWindowStateManager{};

	CSiteManagerSite *m_pNotebook_Site{};
	wxNotebook *m_pNotebook_Bookmark{};

	std::vector<_connected_site> *m_connected_sites{};

	bool m_is_deleting{};
};

#endif
