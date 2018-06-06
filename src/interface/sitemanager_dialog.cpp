#include <filezilla.h>
#include "sitemanager_dialog.h"

#include "asksavepassworddialog.h"
#include "buildinfo.h"
#include "conditionaldialog.h"
#include "drop_target_ex.h"
#include "filezillaapp.h"
#include "ipcmutex.h"
#include "Options.h"
#include "sitemanager_site.h"
#include "themeprovider.h"
#include "treectrlex.h"
#include "window_state_manager.h"
#include "wrapengine.h"
#include "xmlfunctions.h"
#include "xrc_helper.h"

#include <wx/dnd.h>
#include <wx/file.h>

#include <algorithm>
#include <array>

BEGIN_EVENT_TABLE(CSiteManagerDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CSiteManagerDialog::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CSiteManagerDialog::OnCancel)
EVT_BUTTON(XRCID("ID_CONNECT"), CSiteManagerDialog::OnConnect)
EVT_BUTTON(XRCID("ID_NEWSITE"), CSiteManagerDialog::OnNewSite)
EVT_BUTTON(XRCID("ID_NEWFOLDER"), CSiteManagerDialog::OnNewFolder)
EVT_BUTTON(XRCID("ID_RENAME"), CSiteManagerDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETE"), CSiteManagerDialog::OnDelete)
EVT_TREE_BEGIN_LABEL_EDIT(XRCID("ID_SITETREE"), CSiteManagerDialog::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(XRCID("ID_SITETREE"), CSiteManagerDialog::OnEndLabelEdit)
EVT_TREE_SEL_CHANGING(XRCID("ID_SITETREE"), CSiteManagerDialog::OnSelChanging)
EVT_TREE_SEL_CHANGED(XRCID("ID_SITETREE"), CSiteManagerDialog::OnSelChanged)
EVT_TREE_ITEM_ACTIVATED(XRCID("ID_SITETREE"), CSiteManagerDialog::OnItemActivated)
EVT_BUTTON(XRCID("ID_COPY"), CSiteManagerDialog::OnCopySite)
EVT_TREE_BEGIN_DRAG(XRCID("ID_SITETREE"), CSiteManagerDialog::OnBeginDrag)
EVT_CHAR(CSiteManagerDialog::OnChar)
EVT_TREE_ITEM_MENU(XRCID("ID_SITETREE"), CSiteManagerDialog::OnContextMenu)
EVT_MENU(XRCID("ID_EXPORT"), CSiteManagerDialog::OnExportSelected)
EVT_BUTTON(XRCID("ID_NEWBOOKMARK"), CSiteManagerDialog::OnNewBookmark)
EVT_BUTTON(XRCID("ID_BOOKMARK_BROWSE"), CSiteManagerDialog::OnBookmarkBrowse)
END_EVENT_TABLE()

class CSiteManagerItemData : public wxTreeItemData
{
public:
	CSiteManagerItemData() = default;

	CSiteManagerItemData(std::unique_ptr<Site> && site)
		: m_site(std::move(site))
	{}

	// While inside tree, the contents of the path and bookmarks members are inconsistent
	std::unique_ptr<Site> m_site;

	// While inside tree, the nam member is inconsistent
	std::unique_ptr<Bookmark> m_bookmark;

	// Needed to keep track of currently connected sites so that
	// bookmarks and bookmark path can be updated in response to
	// changes done here
	int connected_item{-1};
};

class CSiteManagerDialogDataObject : public wxDataObjectSimple
{
public:
	CSiteManagerDialogDataObject()
		: wxDataObjectSimple(wxDataFormat(_T("FileZilla3SiteManagerObject")))
	{
	}

	// GTK doesn't like data size of 0
	virtual size_t GetDataSize() const { return 1; }

	virtual bool GetDataHere(void *buf) const { memset(buf, 0, 1); return true; }

	virtual bool SetData(size_t, const void *) { return true; }
};

class CSiteManagerDropTarget final : public CScrollableDropTarget<wxTreeCtrlEx>
{
public:
	CSiteManagerDropTarget(CSiteManagerDialog* pSiteManager)
		: CScrollableDropTarget<wxTreeCtrlEx>(XRCCTRL(*pSiteManager, "ID_SITETREE", wxTreeCtrlEx))
	{
		SetDataObject(new CSiteManagerDialogDataObject());
		m_pSiteManager = pSiteManager;
	}

	bool IsValidDropLocation(wxTreeItemId const& hit, wxDragResult const& def)
	{
		if (!hit) {
			return false;
		}
		if (hit == m_pSiteManager->m_dropSource) {
			return false;
		}

		const bool predefined = m_pSiteManager->IsPredefinedItem(hit);
		if (predefined) {
			return false;
		}

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		if (!pTree) {
			return false;
		}

		CSiteManagerItemData *pData = (CSiteManagerItemData *)pTree->GetItemData(hit);
		CSiteManagerItemData *pSourceData = (CSiteManagerItemData *)pTree->GetItemData(m_pSiteManager->m_dropSource);
		if (pData) {
			// Cannot drop on bookmark
			if (!pData->m_site) {
				return false;
			}

			// If dropping on a site, source needs to be a bookmark
			if (!pSourceData || pSourceData->m_site) {
				return false;
			}
		}
		// Target is a directory, so source must not be a bookmark
		else if (pSourceData && !pSourceData->m_site) {
			return false;
		}

		// Disallow dragging into own child
		wxTreeItemId item = hit;
		while (item != pTree->GetRootItem()) {
			if (item == m_pSiteManager->m_dropSource) {
				ClearDropHighlight();
				return wxDragNone;
			}
			item = pTree->GetItemParent(item);
		}

		// If moving, disallow moving to direct parent
		if (def == wxDragMove && pTree->GetItemParent(m_pSiteManager->m_dropSource) == hit) {
			return false;
		}

		return true;
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		ClearDropHighlight();
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			return def;
		}

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!IsValidDropLocation(hit, def)) {
			return wxDragNone;
		}

		if (!m_pSiteManager->MoveItems(m_pSiteManager->m_dropSource, hit, def == wxDragCopy)) {
			return wxDragNone;
		}

		return def;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		CScrollableDropTarget<wxTreeCtrlEx>::OnDrop(x, y);
		ClearDropHighlight();

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!IsValidDropLocation(hit, wxDragCopy)) {
			return wxDragNone;
		}

		return true;
	}

	virtual void OnLeave()
	{
		CScrollableDropTarget<wxTreeCtrlEx>::OnLeave();
		ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxTreeCtrlEx>::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

	wxTreeItemId GetHit(const wxPoint& point)
	{
		int flags = 0;

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		wxTreeItemId hit = pTree->HitTest(point, flags);

		if (flags & (wxTREE_HITTEST_ABOVE | wxTREE_HITTEST_BELOW | wxTREE_HITTEST_NOWHERE | wxTREE_HITTEST_TOLEFT | wxTREE_HITTEST_TORIGHT)) {
			return wxTreeItemId();
		}

		return hit;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxTreeCtrlEx>::OnDragOver(x, y, def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			ClearDropHighlight();
			return def;
		}

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!IsValidDropLocation(hit, def)) {
			ClearDropHighlight();
		}

		DisplayDropHighlight(wxPoint(x, y));

		return def;
	}

	void ClearDropHighlight()
	{
		if (m_dropHighlight == wxTreeItemId())
			return;

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		pTree->SetItemDropHighlight(m_dropHighlight, false);
		m_dropHighlight = wxTreeItemId();
	}

	wxTreeItemId DisplayDropHighlight(wxPoint p)
	{
		ClearDropHighlight();

		wxTreeItemId hit = GetHit(p);
		if (hit.IsOk()) {
			wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
			pTree->SetItemDropHighlight(hit, true);
			m_dropHighlight = hit;
		}

		return hit;
	}

protected:
	CSiteManagerDialog* m_pSiteManager;
	wxTreeItemId m_dropHighlight;
};

CSiteManagerDialog::CSiteManagerDialog()
{
}

CSiteManagerDialog::~CSiteManagerDialog()
{
	delete m_pSiteManagerMutex;

	if (m_pWindowStateManager) {
		m_pWindowStateManager->Remember(OPTION_SITEMANAGER_POSITION);
		delete m_pWindowStateManager;
	}
}

bool CSiteManagerDialog::Create(wxWindow* parent, std::vector<_connected_site> *connected_sites, ServerWithCredentials const* pServer)
{
	m_pSiteManagerMutex = new CInterProcessMutex(MUTEX_SITEMANAGERGLOBAL, false);
	if (m_pSiteManagerMutex->TryLock() == 0) {
		int answer = wxMessageBoxEx(_("The Site Manager is opened in another instance of FileZilla 3.\nDo you want to continue? Any changes made in the Site Manager won't be saved then."),
								  _("Site Manager already open"), wxYES_NO | wxICON_QUESTION);
		if (answer != wxYES) {
			return false;
		}

		delete m_pSiteManagerMutex;
		m_pSiteManagerMutex = 0;
	}
	if (!wxDialogEx::Load(parent, _T("ID_SITEMANAGER"))) {
		return false;
	}

	// Now create the imagelist for the site tree
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree) {
		return false;
	}

	wxSize s = CThemeProvider::GetIconSize(iconSizeSmall);
	wxImageList* pImageList = new wxImageList(s.x, s.y);

	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_FOLDERCLOSED"), wxART_OTHER, s));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_FOLDER"), wxART_OTHER, s));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_SERVER"), wxART_OTHER, s));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_BOOKMARK"), wxART_OTHER, s));

	pTree->AssignImageList(pImageList);

	wxWindow* placeholder = XRCCTRL(*this, "ID_NOTEBOOK_PLACEHOLDER", wxWindow);
	if (placeholder) {
		wxSizer *pSizer = placeholder->GetContainingSizer();

		m_pNotebook_Site = new CSiteManagerSite(*this);
		if (!m_pNotebook_Site->Load(placeholder->GetParent())) {
			return false;
		}

		pSizer->Add(m_pNotebook_Site, 1, wxGROW);

		Layout();


		wxSize minSize = GetSizer()->GetMinSize();

		wxSize size = GetSize();
		wxSize clientSize = GetClientSize();
		SetMinSize(GetSizer()->GetMinSize() + size - clientSize);
		SetClientSize(minSize);

		// Load bookmark notebook
		m_pNotebook_Bookmark = new wxNotebook(placeholder->GetParent(), -1);
		wxPanel* pPanel = new wxPanel;
		wxXmlResource::Get()->LoadPanel(pPanel, m_pNotebook_Bookmark, _T("ID_SITEMANAGER_BOOKMARK_PANEL"));
		m_pNotebook_Bookmark->Hide();
		m_pNotebook_Bookmark->AddPage(pPanel, _("Bookmark"));
		pSizer->Add(m_pNotebook_Bookmark, 2, wxGROW);
		pSizer->SetItemMinSize(2, pSizer->GetItem((size_t)0)->GetMinSize().GetWidth(), -1);
	}

	// Set min size of tree to actual size of tree.
	// Otherwise some platforms automatically calculate a min size fitting all items,
	// resulting in a huge dialog if there are many sites.
	wxSize const treeSize = pTree->GetSize();
	if (treeSize.IsFullySpecified()) {
		pTree->SetMinSize(treeSize);
	}

	if (!Load()) {
		return false;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		pTree->SafeSelectItem(m_ownSites);
	}
	SetCtrlState();

	m_pWindowStateManager = new CWindowStateManager(this);
	m_pWindowStateManager->Restore(OPTION_SITEMANAGER_POSITION);

	pTree->SetDropTarget(new CSiteManagerDropTarget(this));

#ifdef __WXGTK__
	{
		CSiteManagerItemData* data = 0;
		wxTreeItemId selected = pTree->GetSelection();
		if (selected.IsOk()) {
			data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(selected));
		}
		if (!data) {
			XRCCTRL(*this, "wxID_OK", wxButton)->SetFocus();
		}
	}
#endif

	m_connected_sites = connected_sites;
	MarkConnectedSites();

	if (pServer && *pServer) {
		CopyAddServer(*pServer);
	}

	return true;
}

void CSiteManagerDialog::MarkConnectedSites()
{
	for (int i = 0; i < (int)m_connected_sites->size(); ++i) {
		MarkConnectedSite(i);
	}
}

void CSiteManagerDialog::MarkConnectedSite(int connected_site)
{
	std::wstring const& connected_site_path = (*m_connected_sites)[connected_site].old_path;
	if (connected_site_path.empty()) {
		return;
	}

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	if (connected_site_path[0] == '1') {
		// Default sites never change
		(*m_connected_sites)[connected_site].new_path = (*m_connected_sites)[connected_site].old_path;
		return;
	}

	if (connected_site_path[0] != '0') {
		return;
	}

	std::vector<std::wstring> segments;
	if (!CSiteManager::UnescapeSitePath(connected_site_path.substr(1), segments)) {
		return;
	}

	wxTreeItemId current = m_ownSites;
	for (auto const& segment : segments) {
		wxTreeItemIdValue c;
		wxTreeItemId child = pTree->GetFirstChild(current, c);
		while (child) {
			if (pTree->GetItemText(child) == segment) {
				break;
			}

			child = pTree->GetNextChild(current, c);
		}
		if (!child) {
			return;
		}

		current = child;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(current));
	if (!data || !data->m_site) {
		return;
	}

	wxASSERT(data->connected_item == -1);
	data->connected_item = connected_site;
}

void CSiteManagerDialog::OnOK(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}

	UpdateItem();

	if (!CAskSavePasswordDialog::Run(this)) {
		return;
	}

	Save();

	RememberLastSelected();

	EndModal(wxID_OK);
}

void CSiteManagerDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

void CSiteManagerDialog::OnConnect(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data) {
		wxBell();
		return;
	}

	if (!Verify()) {
		wxBell();
		return;
	}

	UpdateItem();

	if (!CAskSavePasswordDialog::Run(this)) {
		return;
	}

	Save();

	RememberLastSelected();

	EndModal(wxID_YES);
}

class CSiteManagerXmlHandler_Tree : public CSiteManagerXmlHandler
{
public:
	CSiteManagerXmlHandler_Tree(wxTreeCtrlEx* pTree, wxTreeItemId root, std::wstring const& lastSelection, bool predefined)
		: m_pTree(pTree), m_item(root), m_predefined(predefined)
	{
		if (!CSiteManager::UnescapeSitePath(lastSelection, m_lastSelection)) {
			m_lastSelection.clear();
		}
		m_lastSelectionIt = m_lastSelection.cbegin();

		m_wrong_sel_depth = 0;
		m_kiosk = COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE);
	}

	virtual ~CSiteManagerXmlHandler_Tree()
	{
		m_pTree->SortChildren(m_item);
		m_pTree->Expand(m_item);
	}

	virtual bool AddFolder(std::wstring const& name, bool expanded)
	{
		wxTreeItemId newItem = m_pTree->AppendItem(m_item, name, 0, 0);
		m_pTree->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
		m_pTree->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);

		m_item = newItem;
		m_expand.push_back(expanded);

		if (!m_wrong_sel_depth && m_lastSelectionIt != m_lastSelection.cend()) {
			if (*m_lastSelectionIt == name) {
				++m_lastSelectionIt;
				if (m_lastSelectionIt == m_lastSelection.cend()) {
					m_pTree->SafeSelectItem(newItem);
				}
			}
			else {
				++m_wrong_sel_depth;
			}
		}
		else {
			++m_wrong_sel_depth;
		}

		return true;
	}

	virtual bool AddSite(std::unique_ptr<Site> data)
	{
		if (m_kiosk && !m_predefined &&
			data->server_.credentials.logonType_ == LogonType::normal)
		{
			// Clear saved password
			data->server_.SetLogonType(LogonType::ask);
			data->server_.credentials.SetPass(std::wstring());
		}

		const wxString name(data->server_.server.GetName());

		CSiteManagerItemData* pData = new CSiteManagerItemData(std::move(data));
		wxTreeItemId newItem = m_pTree->AppendItem(m_item, name, 2, 2, pData);

		bool can_select = false;
		if (!m_wrong_sel_depth && m_lastSelectionIt != m_lastSelection.cend()) {
			if (*m_lastSelectionIt == name) {
				++m_lastSelectionIt;
				can_select = true;
				if (m_lastSelectionIt == m_lastSelection.cend()) {
					m_pTree->SafeSelectItem(newItem);
				}
			}
		}

		for (auto const& bookmark : pData->m_site->m_bookmarks) {
			AddBookmark(newItem, bookmark, can_select);
		}

		m_pTree->SortChildren(newItem);
		m_pTree->Expand(newItem);

		return true;
	}

	bool AddBookmark(wxTreeItemId const& parent, Bookmark const& bookmark, bool can_select)
	{
		CSiteManagerItemData* pData = new CSiteManagerItemData;
		pData->m_bookmark = std::make_unique<Bookmark>(bookmark);
		wxTreeItemId newItem = m_pTree->AppendItem(parent, bookmark.m_name, 3, 3, pData);

		if (can_select && m_lastSelectionIt != m_lastSelection.cend()) {
			if (*m_lastSelectionIt == bookmark.m_name) {
				++m_lastSelectionIt;
				if (m_lastSelectionIt == m_lastSelection.cend()) {
					m_pTree->SafeSelectItem(newItem);
				}
			}
		}

		return true;
	}

	virtual bool LevelUp()
	{
		if (m_wrong_sel_depth) {
			m_wrong_sel_depth--;
		}

		if (!m_expand.empty()) {
			const bool expand = m_expand.back();
			m_expand.pop_back();
			if (expand)
				m_pTree->Expand(m_item);
		}
		m_pTree->SortChildren(m_item);

		wxTreeItemId parent = m_pTree->GetItemParent(m_item);
		if (!parent)
			return false;

		m_item = parent;
		return true;
	}

protected:
	wxTreeCtrlEx* m_pTree;
	wxTreeItemId m_item;

	std::vector<std::wstring> m_lastSelection;
	std::vector<std::wstring>::const_iterator m_lastSelectionIt;
	int m_wrong_sel_depth;

	std::vector<bool> m_expand;

	bool m_predefined;
	int m_kiosk;
};

bool CSiteManagerDialog::Load()
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return false;

	pTree->DeleteAllItems();

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	// Load default sites
	bool hasDefaultSites = LoadDefaultSites();
	if (hasDefaultSites) {
		m_ownSites = pTree->AppendItem(pTree->GetRootItem(), _("My Sites"), 0, 0);
	}
	else {
		m_ownSites = pTree->AddRoot(_("My Sites"), 0, 0);
	}

	wxTreeItemId treeId = m_ownSites;
	pTree->SetItemImage(treeId, 1, wxTreeItemIcon_Expanded);
	pTree->SetItemImage(treeId, 1, wxTreeItemIcon_SelectedExpanded);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The Site Manager cannot be used unless the file gets repaired.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	if (file.IsFromFutureVersion()) {
		wxString msg = wxString::Format(_("The file '%s' has been created by a more recent version of FileZilla.\nLoading files created by newer versions can result in loss of data.\nDo you want to continue?"), file.GetFileName());
		if (wxMessageBoxEx(msg, _("Detected newer version of FileZilla"), wxICON_QUESTION | wxYES_NO) != wxYES) {
			return false;
		}
	}

	auto element = document.child("Servers");
	if (!element) {
		return true;
	}

	std::wstring lastSelection = COptions::Get()->GetOption(OPTION_SITEMANAGER_LASTSELECTED);
	if (!lastSelection.empty() && lastSelection[0] == '0') {
		if (lastSelection == _T("0")) {
			pTree->SafeSelectItem(treeId);
		}
		else {
			lastSelection = lastSelection.substr(1);
		}
	}
	else {
		lastSelection.clear();
	}
	CSiteManagerXmlHandler_Tree handler(pTree, treeId, lastSelection, false);

	bool res = CSiteManager::Load(element, handler);

	pTree->SortChildren(treeId);
	pTree->Expand(treeId);
	if (!pTree->GetSelection()) {
		pTree->SafeSelectItem(treeId);
	}

	pTree->EnsureVisible(pTree->GetSelection());

	return res;
}

bool CSiteManagerDialog::Save(pugi::xml_node element, wxTreeItemId treeId)
{
	if (!m_pSiteManagerMutex)
		return false;

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return false;

	if (!element || !treeId) {
		// We have to synchronize access to sitemanager.xml so that multiple processed don't write
		// to the same file or one is reading while the other one writes.
		CInterProcessMutex mutex(MUTEX_SITEMANAGER);

		CXmlFile xml(wxGetApp().GetSettingsFile(_T("sitemanager")));

		auto document = xml.Load();
		if (!document) {
			wxString msg = xml.GetError() + _T("\n") + _("Any changes made in the Site Manager could not be saved.");
			wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

			return false;
		}

		auto servers = document.child("Servers");
		while (servers) {
			document.remove_child(servers);
			servers = document.child("Servers");
		}
		element = document.append_child("Servers");

		if (!element)
			return true;

		bool res = Save(element, m_ownSites);

		if (!xml.Save(false)) {
			if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2)
				return res;
			wxString msg = wxString::Format(_("Could not write \"%s\", any changes to the Site Manager could not be saved: %s"), xml.GetFileName(), xml.GetError());
			wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
		}

		return res;
	}

	wxTreeItemId child;
	wxTreeItemIdValue cookie;
	child = pTree->GetFirstChild(treeId, cookie);
	while (child.IsOk()) {
		SaveChild(element, child);

		child = pTree->GetNextChild(treeId, cookie);
	}

	return false;
}

bool CSiteManagerDialog::SaveChild(pugi::xml_node element, wxTreeItemId child)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return false;
	}

	std::wstring const name = pTree->GetItemText(child).ToStdWstring();

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(child));
	if (!data) {
		auto node = element.append_child("Folder");
		const bool expanded = pTree->IsExpanded(child);
		SetTextAttribute(node, "expanded", expanded ? _T("1") : _T("0"));
		AddTextElement(node, name);
		Save(node, child);
	}
	else if (data->m_site) {
		auto node = element.append_child("Server");

		// Update bookmarks
		data->m_site->m_bookmarks.clear();

		wxTreeItemIdValue cookie;
		wxTreeItemId bookmarkChild = pTree->GetFirstChild(child, cookie);
		while (bookmarkChild.IsOk()) {
			CSiteManagerItemData* bookmarkData = static_cast<CSiteManagerItemData* >(pTree->GetItemData(bookmarkChild));
			wxASSERT(bookmarkData->m_bookmark);
			bookmarkData->m_bookmark->m_name = pTree->GetItemText(bookmarkChild).ToStdWstring();
			data->m_site->m_bookmarks.push_back(*bookmarkData->m_bookmark);
			bookmarkChild = pTree->GetNextChild(child, cookie);
		}

		CSiteManager::Save(node, *data->m_site);

		if (data->connected_item != -1) {
			if ((*m_connected_sites)[data->connected_item].server.server == data->m_site->server_.server) {
				(*m_connected_sites)[data->connected_item].new_path = GetSitePath(child);
				(*m_connected_sites)[data->connected_item].server = data->m_site->server_;
			}
		}
	}

	return true;
}

void CSiteManagerDialog::OnNewFolder(wxCommandEvent&)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	while (pTree->GetItemData(item))
		item = pTree->GetItemParent(item);

	if (!Verify()) {
		return;
	}

	wxString name = FindFirstFreeName(item, _("New folder"));

	wxTreeItemId newItem = pTree->AppendItem(item, name, 0, 0);
	pTree->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
	pTree->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);
	pTree->SortChildren(item);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
	pTree->EditLabel(newItem);
}

bool CSiteManagerDialog::Verify()
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return true;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return true;
	}

	CSiteManagerItemData* data = (CSiteManagerItemData *)pTree->GetItemData(item);
	if (!data) {
		return true;
	}

	if (data->m_site) {
		bool const predefined = IsPredefinedItem(item);
		if (!m_pNotebook_Site->Verify(predefined)) {
			return false;
		}
	}
	else {
		wxTreeItemId parent = pTree->GetItemParent(item);
		if (!parent) {
			return false;
		}
		CSiteManagerItemData* pServer = static_cast<CSiteManagerItemData*>(pTree->GetItemData(parent));
		if (!pServer || !pServer->m_site) {
			return false;
		}

		const wxString remotePathRaw = XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->GetValue();
		if (!remotePathRaw.empty()) {
			CServerPath remotePath;
			remotePath.SetType(pServer->m_site->server_.server.GetType());
			if (!remotePath.SetPath(remotePathRaw.ToStdWstring())) {
				XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->SetFocus();
				wxString msg;
				if (pServer->m_site->server_.server.GetType() != DEFAULT) {
					msg = wxString::Format(_("Remote path cannot be parsed. Make sure it is a valid absolute path and is supported by the servertype (%s) selected on the parent site."), CServer::GetNameFromServerType(pServer->m_site->server_.server.GetType()));
				}
				else {
					msg = _("Remote path cannot be parsed. Make sure it is a valid absolute path.");
				}
				wxMessageBoxEx(msg, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		const wxString localPath = XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->GetValue();

		if (remotePathRaw.empty() && localPath.empty()) {
			XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("You need to enter at least one path, empty bookmarks are not supported."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		if (XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->GetValue()) {
			if (remotePathRaw.empty() || localPath.empty()) {
				XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->SetFocus();
				wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this bookmark."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}
	}

	return true;
}

void CSiteManagerDialog::OnBeginLabelEdit(wxTreeEvent& event)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		event.Veto();
		return;
	}

	if (event.GetItem() != pTree->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	wxTreeItemId item = event.GetItem();
	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		event.Veto();
		return;
	}
}

void CSiteManagerDialog::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled()) {
		return;
	}

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		event.Veto();
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (item != pTree->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		event.Veto();
		return;
	}

	wxString name = event.GetLabel();

	wxTreeItemId parent = pTree->GetItemParent(item);

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = pTree->GetFirstChild(parent, cookie); child.IsOk(); child = pTree->GetNextChild(parent, cookie)) {
		if (child == item) {
			continue;
		}
		if (!name.CmpNoCase(pTree->GetItemText(child))) {
			wxMessageBoxEx(_("Name already exists"), _("Cannot rename entry"), wxICON_EXCLAMATION, this);
			event.Veto();
			return;
		}
	}

	pTree->SortChildren(parent);
}

void CSiteManagerDialog::OnRename(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item))
		return;

	pTree->EditLabel(item);
}

void CSiteManagerDialog::OnDelete(wxCommandEvent&)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		return;
	}

	CConditionalDialog dlg(this, CConditionalDialog::sitemanager_confirmdelete, CConditionalDialog::yesno);
	dlg.SetTitle(_("Delete Site Manager entry"));

	dlg.AddText(_("Do you really want to delete selected entry?"));

	if (!dlg.Run()) {
		return;
	}

	wxTreeItemId parent = pTree->GetItemParent(item);
	if (pTree->GetChildrenCount(parent) == 1) {
		pTree->Collapse(parent);
	}

	m_is_deleting = true;

	pTree->Delete(item);
	pTree->SafeSelectItem(parent);

	m_is_deleting = false;

	SetCtrlState();
}

void CSiteManagerDialog::OnSelChanging(wxTreeEvent& event)
{
	if (m_is_deleting) {
		return;
	}

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	if (!Verify()) {
		event.Veto();
	}

	UpdateItem();
}

void CSiteManagerDialog::OnSelChanged(wxTreeEvent&)
{
	SetCtrlState();
}

void CSiteManagerDialog::OnNewSite(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || IsPredefinedItem(item)) {
		return;
	}

	while (pTree->GetItemData(item)) {
		item = pTree->GetItemParent(item);
	}

	if (!Verify()) {
		return;
	}

	ServerWithCredentials server;
	server.server.SetProtocol(ServerProtocol::FTP);
	AddNewSite(item, server);
}

bool CSiteManagerDialog::UpdateItem()
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return false;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return false;
	}

	if (IsPredefinedItem(item)) {
		return true;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data) {
		return false;
	}

	if (data->m_site) {
		UpdateServer(*data->m_site, pTree->GetItemText(item));
		return true;
	}
	else {
		wxASSERT(data->m_bookmark);
		wxTreeItemId parent = pTree->GetItemParent(item);
		CSiteManagerItemData const* pServer = static_cast<CSiteManagerItemData*>(pTree->GetItemData(parent));
		if (!pServer || !pServer->m_site) {
			return false;
		}
		data->m_bookmark->m_name = pTree->GetItemText(item);
		return UpdateBookmark(*data->m_bookmark, pServer->m_site->server_);
	}
}

bool CSiteManagerDialog::UpdateBookmark(Bookmark &bookmark, ServerWithCredentials const& server)
{
	bookmark.m_localDir = xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetValue);
	bookmark.m_remoteDir = CServerPath();
	bookmark.m_remoteDir.SetType(server.server.GetType());
	bookmark.m_remoteDir.SetPath(xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::GetValue).ToStdWstring());
	bookmark.m_sync = xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::GetValue);
	bookmark.m_comparison = xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::GetValue);

	return true;
}

void CSiteManagerDialog::UpdateServer(Site &server, const wxString &name)
{
	m_pNotebook_Site->UpdateSite(server);

	server.server_.server.SetName(name.ToStdWstring());
}

bool CSiteManagerDialog::GetServer(Site& data, Bookmark& bookmark)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return false;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return false;
	}

	CSiteManagerItemData* pData = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!pData) {
		return false;
	}

	if (pData->m_bookmark) {
		item = pTree->GetItemParent(item);
		CSiteManagerItemData* pSiteData = static_cast<CSiteManagerItemData*>(pTree->GetItemData(item));

		data = *pSiteData->m_site;
		bookmark = data.m_default_bookmark;

		if (!pData->m_bookmark->m_localDir.empty()) {
			bookmark.m_localDir = pData->m_bookmark->m_localDir;
		}

		if (!pData->m_bookmark->m_remoteDir.empty()) {
			bookmark.m_remoteDir = pData->m_bookmark->m_remoteDir;
		}

		if (bookmark.m_localDir.empty() || bookmark.m_remoteDir.empty()) {
			bookmark.m_sync = false;
			bookmark.m_comparison = false;
		}
		else {
			bookmark.m_sync = pData->m_bookmark->m_sync;
			bookmark.m_comparison = pData->m_bookmark->m_comparison;
		}
	}
	else {
		data = *pData->m_site;
		bookmark = data.m_default_bookmark;
	}

	data.m_path = GetSitePath(item);

	return true;
}

void CSiteManagerDialog::OnItemActivated(wxTreeEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data) {
		if (item != pTree->GetRootItem() || !pTree->IsExpanded(item)) {
			pTree->Toggle(item);
		}
		return;
	}

	wxCommandEvent cmdEvent;
	OnConnect(cmdEvent);
}

void CSiteManagerDialog::SetCtrlState()
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();

	const bool predefined = IsPredefinedItem(item);

#ifdef __WXGTK__
	wxWindow* pFocus = FindFocus();
#endif

	CSiteManagerItemData* data = 0;
	if (item.IsOk()) {
		data = static_cast<CSiteManagerItemData*>(pTree->GetItemData(item));
	}
	if (!data) {
		m_pNotebook_Site->Show();
		m_pNotebook_Bookmark->Hide();
		m_pNotebook_Site->SetSite(Site(), predefined);
		m_pNotebook_Site->Enable(false);
		m_pNotebook_Site->GetContainingSizer()->Layout();

		// Set the control states according if it's possible to use the control
		const bool root_or_predefined = (item == pTree->GetRootItem() || item == m_ownSites || predefined);

		xrc_call(*this, "ID_RENAME", &wxWindow::Enable, !root_or_predefined);
		xrc_call(*this, "ID_DELETE", &wxWindow::Enable, !root_or_predefined);
		xrc_call(*this, "ID_COPY", &wxWindow::Enable, false);
		xrc_call(*this, "ID_NEWFOLDER", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWSITE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWBOOKMARK", &wxWindow::Enable, false);
		xrc_call(*this, "ID_CONNECT", &wxButton::Enable, false);
#ifdef __WXGTK__
		xrc_call(*this, "wxID_OK", &wxButton::SetDefault);
#endif
	}
	else if (data->m_site) {
		m_pNotebook_Site->Show();
		m_pNotebook_Bookmark->Hide();
		m_pNotebook_Site->SetSite(*data->m_site, predefined);
		m_pNotebook_Site->Enable(true);
		m_pNotebook_Site->GetContainingSizer()->Layout();

		// Set the control states according if it's possible to use the control
		xrc_call(*this, "ID_RENAME", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_DELETE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_COPY", &wxWindow::Enable, true);
		xrc_call(*this, "ID_NEWFOLDER", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWSITE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWBOOKMARK", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_CONNECT", &wxButton::Enable, true);

#ifdef __WXGTK__
		xrc_call(*this, "ID_CONNECT", &wxButton::SetDefault);
#endif
	}
	else {
		m_pNotebook_Site->Hide();
		m_pNotebook_Bookmark->Show();
		m_pNotebook_Site->GetContainingSizer()->Layout();

		// Set the control states according if it's possible to use the control
		xrc_call(*this, "ID_RENAME", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_DELETE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_COPY", &wxWindow::Enable, true);
		xrc_call(*this, "ID_NEWFOLDER", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWSITE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_NEWBOOKMARK", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_CONNECT", &wxButton::Enable, true);

		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, data->m_bookmark->m_localDir);
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, data->m_bookmark->m_remoteDir.GetPath());
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxWindow::Enable, !predefined);

		xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::Enable, !predefined);
		xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::SetValue, data->m_bookmark->m_sync);
		xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::Enable, !predefined);
		xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::SetValue, data->m_bookmark->m_comparison);
	}
#ifdef __WXGTK__
	if (pFocus && !pFocus->IsEnabled()) {
		for (wxWindow* pParent = pFocus->GetParent(); pParent; pParent = pParent->GetParent()) {
			if (pParent == this) {
				xrc_call(*this, "wxID_OK", &wxButton::SetFocus);
				break;
			}
		}
	}
#endif
}

void CSiteManagerDialog::OnCopySite(wxCommandEvent&)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data) {
		return;
	}

	if (!Verify()) {
		return;
	}

	if (!UpdateItem()) {
		return;
	}

	wxTreeItemId parent;
	if (IsPredefinedItem(item)) {
		parent = m_ownSites;
	}
	else {
		parent = pTree->GetItemParent(item);
	}

	wxString const name = pTree->GetItemText(item);
	wxString newName = wxString::Format(_("Copy of %s"), name);
	int index = 2;
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = pTree->GetFirstChild(parent, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString const child_name = pTree->GetItemText(child);
			int cmp = child_name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = pTree->GetNextChild(parent, cookie);
		}
		if (!found) {
			break;
		}

		newName = wxString::Format(_("Copy (%d) of %s"), ++index, name);
	}

	wxTreeItemId newItem;
	if (data->m_site) {
		CSiteManagerItemData* newData = new CSiteManagerItemData;
		newData->m_site = std::make_unique<Site>(*data->m_site);
		newItem = pTree->AppendItem(parent, newName, 2, 2, newData);

		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = pTree->GetFirstChild(item, cookie); child.IsOk(); child = pTree->GetNextChild(item, cookie)) {
			CSiteManagerItemData* pData = new CSiteManagerItemData;
			pData->m_bookmark = std::make_unique<Bookmark>(*static_cast<CSiteManagerItemData*>(pTree->GetItemData(child))->m_bookmark);
			pTree->AppendItem(newItem, pTree->GetItemText(child), 3, 3, pData);
		}
		if (pTree->IsExpanded(item)) {
			pTree->Expand(newItem);
		}
	}
	else {
		CSiteManagerItemData* newData = new CSiteManagerItemData;
		newData->m_bookmark = std::make_unique<Bookmark>(*data->m_bookmark);
		newItem = pTree->AppendItem(parent, newName, 3, 3, newData);
	}
	pTree->SortChildren(parent);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
	pTree->EditLabel(newItem);
}

bool CSiteManagerDialog::LoadDefaultSites()
{
	CLocalPath const defaultsDir = wxGetApp().GetDefaultsDir();
	if (defaultsDir.empty()) {
		return false;
	}

	CXmlFile file(defaultsDir.GetPath() + _T("fzdefaults.xml"));

	auto document = file.Load();
	if (!document) {
		return false;
	}

	auto element = document.child("Servers");
	if (!element) {
		return false;
	}

	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree) {
		return false;
	}

	int style = pTree->GetWindowStyle();
	pTree->SetWindowStyle(style | wxTR_HIDE_ROOT);
	wxTreeItemId root = pTree->AddRoot(wxString(), 0, 0);

	m_predefinedSites = pTree->AppendItem(root, _("Predefined Sites"), 0, 0);
	pTree->SetItemImage(m_predefinedSites, 1, wxTreeItemIcon_Expanded);
	pTree->SetItemImage(m_predefinedSites, 1, wxTreeItemIcon_SelectedExpanded);

	std::wstring lastSelection = COptions::Get()->GetOption(OPTION_SITEMANAGER_LASTSELECTED);
	if (!lastSelection.empty() && lastSelection[0] == '1') {
		if (lastSelection == _T("1")) {
			pTree->SafeSelectItem(m_predefinedSites);
		}
		else {
			lastSelection = lastSelection.substr(1);
		}
	}
	else {
		lastSelection.clear();
	}
	CSiteManagerXmlHandler_Tree handler(pTree, m_predefinedSites, lastSelection, true);

	CSiteManager::Load(element, handler);

	return true;
}

bool CSiteManagerDialog::IsPredefinedItem(wxTreeItemId item)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	wxASSERT(pTree);
	if (!pTree) {
		return false;
	}

	while (item) {
		if (item == m_predefinedSites) {
			return true;
		}
		item = pTree->GetItemParent(item);
	}

	return false;
}

void CSiteManagerDialog::OnBeginDrag(wxTreeEvent& event)
{
	if (COptions::Get()->GetOptionVal(OPTION_DND_DISABLED) != 0) {
		return;
	}

	if (!Verify()) {
		event.Veto();
		return;
	}
	UpdateItem();

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		event.Veto();
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item.IsOk()) {
		event.Veto();
		return;
	}

	const bool predefined = IsPredefinedItem(item);
	const bool root = item == pTree->GetRootItem() || item == m_ownSites;
	if (root) {
		event.Veto();
		return;
	}

	CSiteManagerDialogDataObject obj;

	wxDropSource source(this);
	source.SetData(obj);

	m_dropSource = item;

	source.DoDragDrop(predefined ? wxDrag_CopyOnly : wxDrag_DefaultMove);

	m_dropSource = wxTreeItemId();

	SetCtrlState();
}

struct itempair
{
	wxTreeItemId source;
	wxTreeItemId target;
};

bool CSiteManagerDialog::MoveItems(wxTreeItemId source, wxTreeItemId target, bool copy)
{
	if (source == target) {
		return false;
	}

	if (IsPredefinedItem(target)) {
		return false;
	}

	if (IsPredefinedItem(source) && !copy) {
		return false;
	}

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);

	CSiteManagerItemData *pTargetData = (CSiteManagerItemData *)pTree->GetItemData(target);
	CSiteManagerItemData *pSourceData = (CSiteManagerItemData *)pTree->GetItemData(source);
	if (pTargetData) {
		if (pTargetData->m_bookmark) {
			return false;
		}
		if (!pSourceData || pSourceData->m_site) {
			return false;
		}
	}
	else if (pSourceData && !pSourceData->m_site) {
		return false;
	}

	wxTreeItemId item = target;
	while (item != pTree->GetRootItem()) {
		if (item == source) {
			return false;
		}
		item = pTree->GetItemParent(item);
	}

	if (!copy && pTree->GetItemParent(source) == target) {
		return false;
	}

	wxString sourceName = pTree->GetItemText(source);


	wxTreeItemIdValue cookie;
	for (auto child = pTree->GetFirstChild(target, cookie); child.IsOk(); child = pTree->GetNextChild(target, cookie)) {
		wxString const childName = pTree->GetItemText(child);
		if (!sourceName.CmpNoCase(childName)) {
			wxMessageBoxEx(_("An item with the same name as the dragged item already exists at the target location."), _("Failed to copy or move sites"), wxICON_INFORMATION);
			return false;
		}
	}

	std::list<itempair> work;
	itempair initial;
	initial.source = source;
	initial.target = target;
	work.push_back(initial);

	std::list<wxTreeItemId> expand;

	while (!work.empty()) {
		itempair pair = work.front();
		work.pop_front();

		wxString name = pTree->GetItemText(pair.source);

		CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(pair.source));

		wxTreeItemId newItem = pTree->AppendItem(pair.target, name, data ? 2 : 0);
		if (!data) {
			pTree->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
			pTree->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);

			if (pTree->IsExpanded(pair.source))
				expand.push_back(newItem);
		}
		else if (data->m_site) {
			CSiteManagerItemData* newData = new CSiteManagerItemData;
			newData->m_site = std::make_unique<Site>(*data->m_site);
			newData->connected_item = copy ? -1 : data->connected_item;
			pTree->SetItemData(newItem, newData);
		}
		else {
			pTree->SetItemImage(newItem, 3, wxTreeItemIcon_Normal);
			pTree->SetItemImage(newItem, 3, wxTreeItemIcon_Selected);

			CSiteManagerItemData* newData = new CSiteManagerItemData;
			newData->m_bookmark = std::make_unique<Bookmark>(*data->m_bookmark);
			pTree->SetItemData(newItem, newData);
		}

		wxTreeItemIdValue cookie2;
		for (auto child = pTree->GetFirstChild(pair.source, cookie2); child.IsOk(); child = pTree->GetNextChild(pair.source, cookie2)) {
			itempair newPair;
			newPair.source = child;
			newPair.target = newItem;
			work.push_back(newPair);
		}

		pTree->SortChildren(pair.target);
	}

	if (!copy) {
		wxTreeItemId parent = pTree->GetItemParent(source);
		if (pTree->GetChildrenCount(parent) == 1) {
			pTree->Collapse(parent);
		}

		pTree->Delete(source);
	}

	for (auto iter = expand.begin(); iter != expand.end(); ++iter) {
		pTree->Expand(*iter);
	}

	pTree->Expand(target);

	return true;
}

void CSiteManagerDialog::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() != WXK_F2) {
		event.Skip();
		return;
	}

	wxCommandEvent cmdEvent;
	OnRename(cmdEvent);
}

void CSiteManagerDialog::CopyAddServer(ServerWithCredentials const& server)
{
	if (!Verify()) {
		return;
	}

	AddNewSite(m_ownSites, server, true);
}

wxString CSiteManagerDialog::FindFirstFreeName(const wxTreeItemId &parent, const wxString& name)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	wxASSERT(pTree);

	wxString newName = name;
	int index = 2;
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = pTree->GetFirstChild(parent, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString child_name = pTree->GetItemText(child);
			int cmp = child_name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = pTree->GetNextChild(parent, cookie);
		}
		if (!found)
			break;

		newName = name + wxString::Format(_T(" %d"), ++index);
	}

	return newName;
}

void CSiteManagerDialog::AddNewSite(wxTreeItemId parent, ServerWithCredentials const& server, bool connected)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree) {
		return;
	}

	wxString name = FindFirstFreeName(parent, _("New site"));

	CSiteManagerItemData* pData = new CSiteManagerItemData;
	pData->m_site = std::make_unique<Site>();
	pData->m_site->server_ = server;
	if (connected) {
		pData->connected_item = 0;
	}

	wxTreeItemId newItem = pTree->AppendItem(parent, name, 2, 2, pData);
	pTree->SortChildren(parent);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
#ifdef __WXMAC__
	// Need to trigger dirty processing of generic tree control.
	// Else edit control will be hidden behind item
	pTree->OnInternalIdle();
#endif
	pTree->EditLabel(newItem);
}

void CSiteManagerDialog::AddNewBookmark(wxTreeItemId parent)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return;

	wxString name = FindFirstFreeName(parent, _("New bookmark"));

	CSiteManagerItemData* pData = new CSiteManagerItemData;
	pData->m_bookmark = std::make_unique<Bookmark>();
	wxTreeItemId newItem = pTree->AppendItem(parent, name, 3, 3, pData);
	pTree->SortChildren(parent);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
	pTree->EditLabel(newItem);
}

void CSiteManagerDialog::RememberLastSelected()
{
	wxString path;

	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (pTree) {
		wxTreeItemId sel = pTree->GetSelection();
		if (sel) {
			path = GetSitePath(sel);
		}
	}
	COptions::Get()->SetOption(OPTION_SITEMANAGER_LASTSELECTED, path.ToStdWstring());
}

void CSiteManagerDialog::OnContextMenu(wxTreeEvent& event)
{
	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_SITEMANAGER"));
	if (!pMenu) {
		return;
	}

	m_contextMenuItem = event.GetItem();

	PopupMenu(pMenu);
	delete pMenu;
}

void CSiteManagerDialog::OnExportSelected(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}
	UpdateItem();

	wxFileDialog dlg(this, _("Select file for exported sites"), wxString(),
					_T("sites.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	CXmlFile xml(dlg.GetPath().ToStdWstring());

	auto exportRoot = xml.CreateEmpty();

	auto servers = exportRoot.append_child("Servers");
	SaveChild(servers, m_contextMenuItem);

	if (!xml.Save(false)) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the selected sites could not be exported: %s"), xml.GetFileName(), xml.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}
}

void CSiteManagerDialog::OnBookmarkBrowse(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data || !data->m_bookmark) {
		return;
	}

	wxDirDialog dlg(this, _("Choose the local directory"), XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->ChangeValue(dlg.GetPath());
}

void CSiteManagerDialog::OnNewBookmark(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return;
	}

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || IsPredefinedItem(item)) {
		return;
	}

	CSiteManagerItemData *pData = (CSiteManagerItemData *)pTree->GetItemData(item);
	if (!pData) {
		return;
	}
	if (pData->m_bookmark) {
		item = pTree->GetItemParent(item);
	}

	if (!Verify()) {
		return;
	}

	AddNewBookmark(item);
}

std::wstring CSiteManagerDialog::GetSitePath(wxTreeItemId item, bool stripBookmark)
{
	wxASSERT(item);

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		return std::wstring();
	}

	CSiteManagerItemData* pData = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!pData) {
		return std::wstring();
	}

	if (stripBookmark && pData->m_bookmark) {
		item = pTree->GetItemParent(item);
	}

	std::wstring path;
	while (item) {
		if (item == m_predefinedSites) {
			return _T("1") + path;
		}
		else if (item == m_ownSites) {
			return _T("0") + path;
		}
		path = _T("/") + CSiteManager::EscapeSegment(pTree->GetItemText(item).ToStdWstring()) + path;

		item = pTree->GetItemParent(item);
	}

	return _T("0") + path;
}
