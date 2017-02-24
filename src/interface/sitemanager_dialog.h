#ifndef FILEZILLA_INTERFACE_SITEMANAGER_DIALOG_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_DIALOG_HEADER

#include "dialogex.h"
#include "sitemanager.h"

class CInterProcessMutex;
class CWindowStateManager;
class CSiteManagerDropTarget;
class CSiteManagerDialog final : public wxDialogEx
{
	friend class CSiteManagerDropTarget;

	DECLARE_EVENT_TABLE()

public:
	struct _connected_site
	{
		CServer server;
		std::wstring old_path;
		std::wstring new_path;
	};

	/// Constructors
	CSiteManagerDialog();
	virtual ~CSiteManagerDialog();

	// Creation. If pServer is set, it will cause a new item to be created.
	bool Create(wxWindow* parent, std::vector<_connected_site> *connected_sites, const CServer* pServer = 0);

	bool GetServer(Site& data, Bookmark& bookmark);

protected:
	// Creates the controls and sizers
	void CreateControls(wxWindow* parent);

	bool Verify();
	bool UpdateItem();
	bool UpdateServer(Site &server, const wxString& name);
	bool UpdateBookmark(Bookmark &bookmark, const CServer& server);
	bool Load();
	bool Save(pugi::xml_node element = pugi::xml_node(), wxTreeItemId treeId = wxTreeItemId());
	bool SaveChild(pugi::xml_node element, wxTreeItemId child);
	void SetCtrlState();
	bool LoadDefaultSites();

	void SetProtocol(ServerProtocol protocol);
	ServerProtocol GetProtocol() const;
	LogonType GetLogonType() const;

	bool IsPredefinedItem(wxTreeItemId item);

	wxString FindFirstFreeName(const wxTreeItemId &parent, const wxString& name);

	void AddNewSite(wxTreeItemId parent, const CServer& server, bool connected = false);
	void CopyAddServer(const CServer& server);

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
	void OnLogontypeSelChanged(wxCommandEvent& event);
	void OnRemoteDirBrowse(wxCommandEvent& event);
	void OnItemActivated(wxTreeEvent& event);
	void OnLimitMultipleConnectionsChanged(wxCommandEvent& event);
	void OnCharsetChange(wxCommandEvent& event);
	void OnProtocolSelChanged(wxCommandEvent& event);
	void OnBeginDrag(wxTreeEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnCopySite(wxCommandEvent& event);
	void OnContextMenu(wxTreeEvent& event);
	void OnExportSelected(wxCommandEvent&);
	void OnNewBookmark(wxCommandEvent&);
	void OnBookmarkBrowse(wxCommandEvent&);
	void OnKeyFileBrowse(wxCommandEvent&);

	void SetControlVisibility(ServerProtocol protocol, LogonType type);

	CInterProcessMutex* m_pSiteManagerMutex{};

	wxTreeItemId m_predefinedSites;
	wxTreeItemId m_ownSites;

	wxTreeItemId m_dropSource;

	wxTreeItemId m_contextMenuItem;

	bool MoveItems(wxTreeItemId source, wxTreeItemId target, bool copy);

protected:
	CWindowStateManager* m_pWindowStateManager{};

	wxNotebook *m_pNotebook_Site{};
	wxNotebook *m_pNotebook_Bookmark{};

	std::vector<_connected_site> *m_connected_sites{};

	bool m_is_deleting{};

	std::map<ServerProtocol, int> mainProtocolListIndex_;
};

#endif
