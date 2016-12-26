#include <filezilla.h>

#include "dndobjects.h"
#include "dragdropmanager.h"
#include "drop_target_ex.h"
#include "filezillaapp.h"
#include "filter.h"
#include "file_utils.h"
#include "graphics.h"
#include "inputdialog.h"
#include "LocalTreeView.h"
#include "Options.h"
#include "queue.h"
#include "themeprovider.h"

#include <libfilezilla/local_filesys.hpp>

#ifdef __WXMSW__
#include <wx/msw/registry.h>
#include <shlobj.h>
#include <dbt.h>
#include "volume_enumerator.h"
#endif

#include <algorithm>

class CTreeItemData : public wxTreeItemData
{
public:
	CTreeItemData(std::wstring const& known_subdir) : m_known_subdir(known_subdir) {}
	std::wstring m_known_subdir;
};

class CLocalTreeViewDropTarget final : public CScrollableDropTarget<wxTreeCtrlEx>
{
public:
	CLocalTreeViewDropTarget(CLocalTreeView* pLocalTreeView)
		: CScrollableDropTarget(pLocalTreeView)
		, m_pLocalTreeView(pLocalTreeView), m_pFileDataObject(new wxFileDataObject()),
		m_pRemoteDataObject(new CRemoteDataObject())
	{
		m_pDataObject = new wxDataObjectComposite;
		m_pDataObject->Add(m_pRemoteDataObject, true);
		m_pDataObject->Add(m_pFileDataObject, false);
		SetDataObject(m_pDataObject);
	}

	void ClearDropHighlight()
	{
		const wxTreeItemId dropHighlight = m_pLocalTreeView->m_dropHighlight;
		if (dropHighlight != wxTreeItemId())
		{
			m_pLocalTreeView->SetItemDropHighlight(dropHighlight, false);
			m_pLocalTreeView->m_dropHighlight = wxTreeItemId();
		}
	}

	wxString GetDirFromItem(const wxTreeItemId& item)
	{
		const wxString& dir = m_pLocalTreeView->GetDirFromItem(item);

#ifdef __WXMSW__
		if (dir == _T("/")) {
			return wxString();
		}
#endif

		return dir;
	}

	wxTreeItemId GetHit(const wxPoint& point)
	{
		int flags = 0;
		wxTreeItemId hit = m_pLocalTreeView->HitTest(point, flags);

		if (flags & (wxTREE_HITTEST_ABOVE | wxTREE_HITTEST_BELOW | wxTREE_HITTEST_NOWHERE | wxTREE_HITTEST_TOLEFT | wxTREE_HITTEST_TORIGHT)) {
			return wxTreeItemId();
		}

		return hit;
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
			return def;
		if( def == wxDragLink ) {
			def = wxDragCopy;
		}

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit)
			return wxDragNone;

		const CLocalPath path(GetDirFromItem(hit).ToStdWstring());
		if (path.empty() || !path.IsWriteable())
			return wxDragNone;

		if (!GetData())
			return wxDragError;

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager)
			pDragDropManager->pDropTarget = m_pLocalTreeView;

		if (m_pDataObject->GetReceivedFormat() == m_pFileDataObject->GetFormat())
			m_pLocalTreeView->m_state.HandleDroppedFiles(m_pFileDataObject, path, def == wxDragCopy);
		else
		{
			if (m_pRemoteDataObject->GetProcessId() != (int)wxGetProcessId())
			{
				wxMessageBoxEx(_("Drag&drop between different instances of FileZilla has not been implemented yet."));
				return wxDragNone;
			}

			if (!m_pLocalTreeView->m_state.GetServer() || !m_pRemoteDataObject->GetServer().EqualsNoPass(*m_pLocalTreeView->m_state.GetServer()))
			{
				wxMessageBoxEx(_("Drag&drop between different servers has not been implemented yet."));
				return wxDragNone;
			}

			if (!m_pLocalTreeView->m_state.DownloadDroppedFiles(m_pRemoteDataObject, path))
				return wxDragNone;
		}

		return def;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		if (!CScrollableDropTarget<wxTreeCtrlEx>::OnDrop(x, y)) {
			return false;
		}

		ClearDropHighlight();

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit) {
			return false;
		}

		std::wstring const dir = GetDirFromItem(hit).ToStdWstring();
		if (dir.empty() || !CLocalPath(dir).IsWriteable()) {
			return false;
		}

		return true;
	}

	wxTreeItemId DisplayDropHighlight(wxPoint point)
	{
		wxTreeItemId hit = GetHit(point);
		if (!hit) {
			ClearDropHighlight();
			return hit;
		}

		wxString dir = GetDirFromItem(hit);

		if (dir.empty()) {
			ClearDropHighlight();
			return wxTreeItemId();
		}

		const wxTreeItemId dropHighlight = m_pLocalTreeView->m_dropHighlight;
		if (dropHighlight != wxTreeItemId())
			m_pLocalTreeView->SetItemDropHighlight(dropHighlight, false);

		m_pLocalTreeView->SetItemDropHighlight(hit, true);
		m_pLocalTreeView->m_dropHighlight = hit;


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

		wxTreeItemId hit = DisplayDropHighlight(wxPoint(x, y));
		if (!hit.IsOk())
			return wxDragNone;

		if (def == wxDragLink)
			def = wxDragCopy;

		return def;
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

protected:
	CLocalTreeView *m_pLocalTreeView;
	wxFileDataObject* m_pFileDataObject;
	CRemoteDataObject* m_pRemoteDataObject;
	wxDataObjectComposite* m_pDataObject;
};

IMPLEMENT_CLASS(CLocalTreeView, wxTreeCtrlEx)

BEGIN_EVENT_TABLE(CLocalTreeView, wxTreeCtrlEx)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CLocalTreeView::OnItemExpanding)
#ifdef __WXMSW__
EVT_TREE_SEL_CHANGING(wxID_ANY, CLocalTreeView::OnSelectionChanging)
#endif
EVT_TREE_SEL_CHANGED(wxID_ANY, CLocalTreeView::OnSelectionChanged)
EVT_TREE_BEGIN_DRAG(wxID_ANY, CLocalTreeView::OnBeginDrag)
#ifdef __WXMSW__
EVT_COMMAND(-1, fzEVT_VOLUMESENUMERATED, CLocalTreeView::OnVolumesEnumerated)
EVT_COMMAND(-1, fzEVT_VOLUMEENUMERATED, CLocalTreeView::OnVolumesEnumerated)
#endif //__WXMSW__
EVT_TREE_ITEM_MENU(wxID_ANY, CLocalTreeView::OnContextMenu)
EVT_MENU(XRCID("ID_UPLOAD"), CLocalTreeView::OnMenuUpload)
EVT_MENU(XRCID("ID_ADDTOQUEUE"), CLocalTreeView::OnMenuUpload)
EVT_MENU(XRCID("ID_DELETE"), CLocalTreeView::OnMenuDelete)
EVT_MENU(XRCID("ID_RENAME"), CLocalTreeView::OnMenuRename)
EVT_MENU(XRCID("ID_MKDIR"), CLocalTreeView::OnMenuMkdir)
EVT_MENU(XRCID("ID_MKDIR_CHGDIR"), CLocalTreeView::OnMenuMkdirChgDir)
EVT_TREE_BEGIN_LABEL_EDIT(wxID_ANY, CLocalTreeView::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(wxID_ANY, CLocalTreeView::OnEndLabelEdit)
EVT_CHAR(CLocalTreeView::OnChar)
EVT_MENU(XRCID("ID_OPEN"), CLocalTreeView::OnMenuOpen)
END_EVENT_TABLE()

CLocalTreeView::CLocalTreeView(wxWindow* parent, wxWindowID id, CState& state, CQueueView *pQueueView)
	: wxTreeCtrlEx(parent, id, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxTR_EDIT_LABELS | wxTR_LINES_AT_ROOT | wxTR_HAS_BUTTONS | wxNO_BORDER),
	CSystemImageList(CThemeProvider::GetIconSize(iconSizeSmall).x),
	CStateEventHandler(state),
	m_pQueueView(pQueueView)
{
	wxGetApp().AddStartupProfileRecord("CLocalTreeView::CLocalTreeView");
#ifdef __WXMAC__
	SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#endif

	state.RegisterHandler(this, STATECHANGE_LOCAL_DIR);
	state.RegisterHandler(this, STATECHANGE_APPLYFILTER);
	state.RegisterHandler(this, STATECHANGE_SERVER);

	SetImageList(GetSystemImageList());

	UpdateSortMode();
	RegisterOption(OPTION_FILELIST_NAMESORT);

#ifdef __WXMSW__
	m_pVolumeEnumeratorThread = 0;

	CreateRoot();
#else
	wxTreeItemId root = AddRoot(_T("/"));
	SetItemImage(root, GetIconIndex(iconType::dir), wxTreeItemIcon_Normal);
	SetItemImage(root, GetIconIndex(iconType::opened_dir), wxTreeItemIcon_Selected);
	SetItemImage(root, GetIconIndex(iconType::dir), wxTreeItemIcon_Expanded);
	SetItemImage(root, GetIconIndex(iconType::opened_dir), wxTreeItemIcon_SelectedExpanded);

	SetDir(_T("/"));
#endif

	SetDropTarget(new CLocalTreeViewDropTarget(this));

	m_windowTinter = std::make_unique<CWindowTinter>(*this);
}

CLocalTreeView::~CLocalTreeView()
{
#ifdef __WXMSW__
	delete m_pVolumeEnumeratorThread;
#endif
}

void CLocalTreeView::SetDir(wxString localDir)
{
	if (m_currentDir == localDir)
	{
		RefreshListing();
		return;
	}

	if (localDir.Left(2) == _T("\\\\"))
	{
		// TODO: UNC path, don't display it yet
		m_currentDir = _T("");
		SafeSelectItem(wxTreeItemId());
		return;
	}
	m_currentDir = localDir;

#ifdef __WXMSW__
	if (localDir == _T("\\"))
	{
		SafeSelectItem(m_drives);
		return;
	}
#endif

	wxString subDirs = localDir;
	wxTreeItemId parent = GetNearestParent(subDirs);
	if (!parent)
	{
		SafeSelectItem(wxTreeItemId());
		return;
	}

	if (subDirs.empty())
	{
		SafeSelectItem(parent);
		return;
	}
	wxTreeItemId item = MakeSubdirs(parent, localDir.Left(localDir.Length() - subDirs.Length()), subDirs);
	if (!item)
		return;

	SafeSelectItem(item);
}

wxTreeItemId CLocalTreeView::GetNearestParent(wxString& localDir)
{
	const wxString separator = wxFileName::GetPathSeparator();
#ifdef __WXMSW__
	int pos = localDir.Find(separator);
	if (pos == -1)
		return wxTreeItemId();

	wxString drive = localDir.Left(pos);
	localDir = localDir.Mid(pos + 1);

	wxTreeItemIdValue value;
	wxTreeItemId root = GetFirstChild(m_drives, value);
	while (root) {
		if (!GetItemText(root).Left(drive.Length()).CmpNoCase(drive))
			break;

		root = GetNextSibling(root);
	}
	if (!root) {
		if (drive[1] == ':')
			return AddDrive(drive[0]);
		return wxTreeItemId();
	}
#else
		if (localDir[0] == '/')
			localDir = localDir.Mid(1);
		wxTreeItemId root = GetRootItem();
#endif

	while (!localDir.empty()) {
		wxString subDir;
		int pos2 = localDir.Find(separator);
		if (pos2 == -1)
			subDir = localDir;
		else
			subDir = localDir.Left(pos2);

		wxTreeItemId child = GetSubdir(root, subDir);
		if (!child)
			return root;

		if (!pos2)
			return child;

		root = child;
		localDir = localDir.Mid(pos2 + 1);
	}

	return root;
}

wxTreeItemId CLocalTreeView::GetSubdir(wxTreeItemId parent, const wxString& subDir)
{
	wxTreeItemIdValue value;
	wxTreeItemId child = GetFirstChild(parent, value);
	while (child)
	{
#ifdef __WXMSW__
		if (!GetItemText(child).CmpNoCase(subDir))
#else
		if (GetItemText(child) == subDir)
#endif
			return child;

		child = GetNextSibling(child);
	}
	return wxTreeItemId();
}

#ifdef __WXMSW__

bool CLocalTreeView::DisplayDrives(wxTreeItemId parent)
{
	wxGetApp().AddStartupProfileRecord("CLocalTreeView::DisplayDrives");

	std::list<std::wstring> drives = CVolumeDescriptionEnumeratorThread::GetDrives();

	m_pVolumeEnumeratorThread = new CVolumeDescriptionEnumeratorThread(this);
	if (m_pVolumeEnumeratorThread->Failed()) {
		delete m_pVolumeEnumeratorThread;
		m_pVolumeEnumeratorThread = 0;
	}

	for (auto it = drives.begin(); it != drives.end(); ++it) {
		wxString drive = *it;
		if (drive.Right(1) == _T("\\"))
			drive = drive.RemoveLast();

		wxTreeItemId item = AppendItem(parent, drive, GetIconIndex(iconType::dir, _T(""), false));
		AppendItem(item, _T(""));
	}
	SortChildren(parent);

	wxGetApp().AddStartupProfileRecord("CLocalTreeView::DisplayDrives adding drives done");

	return true;
}

#endif

void CLocalTreeView::DisplayDir(wxTreeItemId parent, const wxString& dirname, std::wstring const& knownSubdir)
{
	fz::local_filesys local_filesys;

	{
		wxLogNull log;
		if (!local_filesys.begin_find_files(fz::to_native(dirname), true)) {
			if (!knownSubdir.empty()) {
				wxTreeItemId item = GetSubdir(parent, knownSubdir);
				if (item != wxTreeItemId())
					return;

				const wxString fullName = dirname + knownSubdir;
				item = AppendItem(parent, knownSubdir, GetIconIndex(iconType::dir, fullName),
#ifdef __WXMSW__
						-1
#else
						GetIconIndex(iconType::opened_dir, fullName)
#endif
					);
				CheckSubdirStatus(item, fullName);
			}
			else
			{
				m_setSelection = true;
				DeleteChildren(parent);
				m_setSelection = false;
			}
			return;
		}
	}

	wxASSERT(parent);
	m_setSelection = true;
	DeleteChildren(parent);
	m_setSelection = false;

	CFilterManager filter;

	bool matchedKnown = false;

	fz::native_string file;
	bool wasLink;
	int attributes;
	bool is_dir;
	static int64_t const size(-1);
	fz::datetime date;
	while (local_filesys.get_next_file(file, wasLink, is_dir, 0, &date, &attributes)) {
		wxASSERT(is_dir);
		if (file.empty()) {
			wxGetApp().DisplayEncodingWarning();
			continue;
		}
		std::wstring wfile = fz::to_wstring(file);

		std::wstring fullName = dirname.ToStdWstring() + wfile;
#ifdef __WXMSW__
		if (fz::stricmp(wfile, knownSubdir))
#else
		if (wfile != knownSubdir)
#endif
		{
			if (filter.FilenameFiltered(wfile, dirname, true, size, true, attributes, date)) {
				continue;
			}
		}
		else {
			matchedKnown = true;
		}

		wxTreeItemId item = AppendItem(parent, wfile, GetIconIndex(iconType::dir, fullName),
#ifdef __WXMSW__
				-1
#else
				GetIconIndex(iconType::opened_dir, fullName)
#endif
			);

		CheckSubdirStatus(item, fullName);
	}

	if (!matchedKnown && !knownSubdir.empty()) {
		const wxString fullName = dirname + knownSubdir;
		wxTreeItemId item = AppendItem(parent, knownSubdir, GetIconIndex(iconType::dir, fullName),
#ifdef __WXMSW__
				-1
#else
				GetIconIndex(iconType::opened_dir, fullName)
#endif
			);

		CheckSubdirStatus(item, fullName);
	}

	SortChildren(parent);
}

wxString CLocalTreeView::HasSubdir(const wxString& dirname)
{
	wxLogNull nullLog;

	CFilterManager filter;

	fz::local_filesys local_filesys;
	if (!local_filesys.begin_find_files(fz::to_native(dirname), true)) {
		return wxString();
	}

	fz::native_string file;
	bool wasLink;
	int attributes;
	bool is_dir;
	static int64_t const size(-1);
	fz::datetime date;
	while (local_filesys.get_next_file(file, wasLink, is_dir, 0, &date, &attributes)) {
		wxASSERT(is_dir);
		if (file.empty()) {
			wxGetApp().DisplayEncodingWarning();
			continue;
		}

		std::wstring wfile = fz::to_wstring(file);
		if (filter.FilenameFiltered(wfile, dirname, true, size, true, attributes, date)) {
			continue;
		}

		return wfile;
	}

	return wxString();
}

wxTreeItemId CLocalTreeView::MakeSubdirs(wxTreeItemId parent, wxString dirname, wxString subDir)
{
	const wxString& separator = wxFileName::GetPathSeparator();

	while (!subDir.empty()) {
		int pos = subDir.Find(separator);
		wxString segment;
		if (pos == -1) {
			segment = subDir;
			subDir = _T("");
		}
		else {
			segment = subDir.Left(pos);
			subDir = subDir.Mid(pos + 1);
		}

		DisplayDir(parent, dirname, segment.ToStdWstring());

		wxTreeItemId item = GetSubdir(parent, segment);
		if (!item)
			return wxTreeItemId();

		parent = item;
		dirname += segment + separator;
	}

	// Not needed, stays unexpanded by default
	// DisplayDir(parent, dirname);
	return parent;
}

void CLocalTreeView::OnItemExpanding(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();

	wxTreeItemIdValue value;
	wxTreeItemId child = GetFirstChild(item, value);
	if (child && GetItemText(child).empty())
		DisplayDir(item, GetDirFromItem(item));
}

wxString CLocalTreeView::GetDirFromItem(wxTreeItemId item)
{
	const wxString separator = wxFileName::GetPathSeparator();
	wxString dir;
	while (item)
	{
#ifdef __WXMSW__
		if (item == m_desktop) {
			wxChar path[MAX_PATH + 1];
			if (SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, 0, SHGFP_TYPE_CURRENT, path) != S_OK) {
				if (SHGetFolderPath(0, CSIDL_DESKTOP, 0, SHGFP_TYPE_CURRENT, path) != S_OK) {
					wxMessageBoxEx(_("Failed to get desktop path"));
					return _T("/");
				}
			}
			dir = path;
			if (dir.empty() || dir.Last() != separator)
				dir += separator;
			return dir;
		}
		else if (item == m_documents) {
			wxChar path[MAX_PATH + 1];
			if (SHGetFolderPath(0, CSIDL_PERSONAL, 0, SHGFP_TYPE_CURRENT, path) != S_OK) {
				wxMessageBoxEx(_("Failed to get 'My Documents' path"));
				return _T("/");
			}
			dir = path;
			if (dir.empty() || dir.Last() != separator)
				dir += separator;
			return dir;
		}
		else if (item == m_drives)
			return _T("/");
		else if (GetItemParent(item) == m_drives) {
			wxString text = GetItemText(item);
			int pos = text.Find(_T(" "));
			if (pos == -1)
				return text + separator + dir;
			else
				return text.Left(pos) + separator + dir;
		}
		else
#endif
		if (item == GetRootItem())
			return _T("/") + dir;

		dir = GetItemText(item) + separator + dir;

		item = GetItemParent(item);
	}

	return separator;
}

struct t_dir
{
	wxString dir;
	wxTreeItemId item;
};

void CLocalTreeView::UpdateSortMode()
{
	switch (COptions::Get()->GetOptionVal(OPTION_FILELIST_NAMESORT))
	{
	case 0:
	default:
		m_nameSortMode = CFileListCtrlSortBase::namesort_caseinsensitive;
		break;
	case 1:
		m_nameSortMode = CFileListCtrlSortBase::namesort_casesensitive;
		break;
	case 2:
		m_nameSortMode = CFileListCtrlSortBase::namesort_natural;
		break;
	}
}

void CLocalTreeView::RefreshListing()
{
	wxLogNull nullLog;

	const wxString separator = wxFileName::GetPathSeparator();

	std::list<t_dir> dirsToCheck;

#ifdef __WXMSW__
	int prevErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);

	wxTreeItemIdValue tmp;
	for (auto child = GetFirstChild(m_drives, tmp); child; child = GetNextSibling(child)) {
		if (IsExpanded(child)) {
			wxString drive = GetItemText(child);
			int pos = drive.Find(_T(" "));
			if (pos != -1)
				drive = drive.Left(pos);

			t_dir dir;
			dir.dir = drive + separator;
			dir.item = child;
			dirsToCheck.push_back(dir);
		}
	}
#else
	t_dir root_dir;
	root_dir.dir = separator;
	root_dir.item = GetRootItem();
	dirsToCheck.push_back(root_dir);
#endif

	CFilterManager filter;

	while (!dirsToCheck.empty()) {
		t_dir dir = dirsToCheck.front();
		dirsToCheck.pop_front();

		// Step 1: Check if directory exists
		fz::local_filesys local_filesys;
		if (!local_filesys.begin_find_files(fz::to_native(dir.dir), true)) {
			// Dir does exist (listed in parent) but may not be accessible.
			// Recurse into children anyhow, they might be accessible again.
			wxTreeItemIdValue value;
			for (auto child = GetFirstChild(dir.item, value); child; child = GetNextSibling(child)) {
				t_dir subdir;
				subdir.dir = dir.dir + GetItemText(child) + separator;
				subdir.item = child;
				dirsToCheck.push_back(subdir);
			}
			continue;
		}

		// Step 2: Enumerate subdirectories on disk and sort them
		std::vector<std::wstring> dirs;

		fz::native_string file;
		static int64_t const size(-1);
		bool was_link;
		bool is_dir;
		int attributes;
		fz::datetime date;
		while (local_filesys.get_next_file(file, was_link, is_dir, 0, &date, &attributes)) {
			if (file.empty()) {
				wxGetApp().DisplayEncodingWarning();
				continue;
			}

			std::wstring wfile = fz::to_wstring(file);
			if (filter.FilenameFiltered(wfile, dir.dir, true, size, true, attributes, date)) {
				continue;
			}

			dirs.emplace_back(std::move(wfile));
		}
		auto const& sortFunc = CFileListCtrlSortBase::GetCmpFunction(m_nameSortMode);
		std::sort(dirs.begin(), dirs.end(), [&](auto const& lhs, auto const& rhs) { return sortFunc(lhs, rhs) < 0; });

		bool inserted = false;

		// Step 3: Merge list of subdirectories with subtree.
		wxTreeItemId child = GetLastChild(dir.item);
		auto iter = dirs.rbegin();
		while (child || iter != dirs.rend()) {
			int cmp;
			if (child && iter != dirs.rend())
#ifdef __WXMSW__
				cmp = GetItemText(child).CmpNoCase(*iter);
#else
				cmp = GetItemText(child).Cmp(*iter);
#endif
			else if (child)
				cmp = 1;
			else
				cmp = -1;

			if (!cmp) {
				// Found item with same name. Mark it for further processing
				if (!IsExpanded(child)) {
					wxString path = dir.dir + *iter + separator;
					if (!CheckSubdirStatus(child, path)) {
						t_dir subdir;
						subdir.dir = path;
						subdir.item = child;
						dirsToCheck.push_front(subdir);
					}
				}
				else {
					t_dir subdir;
					subdir.dir = dir.dir + *iter + separator;
					subdir.item = child;
					dirsToCheck.push_front(subdir);
				}
				child = GetPrevSibling(child);
				++iter;
			}
			else if (cmp > 0) {
				// Subdirectory currently in tree no longer exists.
				// Delete child from tree, unless current selection
				// is in the subtree.
				wxTreeItemId sel = GetSelection();
				while (sel && sel != child)
					sel = GetItemParent(sel);
				wxTreeItemId prev = GetPrevSibling(child);
				if (!sel)
					Delete(child);
				child = prev;
			}
			else if (cmp < 0) {
				// New subdirectory, add treeitem
				wxString fullname = dir.dir + *iter + separator;
				wxTreeItemId newItem = AppendItem(dir.item, *iter, GetIconIndex(iconType::dir, fullname),
#ifdef __WXMSW__
						-1
#else
						GetIconIndex(iconType::opened_dir, fullname)
#endif
					);

				CheckSubdirStatus(newItem, fullname);
				++iter;
				inserted = true;
			}
		}
		if (inserted)
			SortChildren(dir.item);
	}

#ifdef __WXMSW__
	SetErrorMode(prevErrorMode);
#endif
}

void CLocalTreeView::OnSelectionChanged(wxTreeEvent& event)
{
	if (m_setSelection) {
		event.Skip();
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item) {
		return;
	}

	std::wstring dir = GetDirFromItem(item).ToStdWstring();

	std::wstring error;
	if (!m_state.SetLocalDir(dir, &error)) {
		if (!error.empty()) {
			wxMessageBoxEx(error, _("Failed to change directory"), wxICON_INFORMATION);
		}
		else {
			wxBell();
		}
		m_setSelection = true;
		SelectItem(event.GetOldItem());
		m_setSelection = false;
	}
}

void CLocalTreeView::OnStateChange(t_statechange_notifications notification, const wxString&, const void*)
{
	if (notification == STATECHANGE_LOCAL_DIR) {
		SetDir(m_state.GetLocalDir().GetPath());
	}
	else if (notification == STATECHANGE_SERVER) {
		m_windowTinter->SetBackgroundTint(m_state.GetSite().m_colour);
	}
	else {
		wxASSERT(notification == STATECHANGE_APPLYFILTER);
		RefreshListing();
	}
}

void CLocalTreeView::OnBeginDrag(wxTreeEvent& event)
{
	if (COptions::Get()->GetOptionVal(OPTION_DND_DISABLED) != 0) {
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item)
		return;

#ifdef __WXMSW__
	if (item == m_drives || item == m_desktop || item == m_documents)
		return;
#endif

	wxString dir = GetDirFromItem(item);
	if (dir == _T("/"))
		return;

#ifdef __WXMSW__
	if (!dir.empty() && dir.Last() == '\\')
		dir.RemoveLast();
#endif
	if (!dir.empty() && dir.Last() == '/')
		dir.RemoveLast();

#ifdef __WXMSW__
	if (!dir.empty() && dir.Last() == ':')
		return;
#endif

	CDragDropManager* pDragDropManager = CDragDropManager::Init();
	pDragDropManager->pDragSource = this;

	wxFileDataObject obj;

	obj.AddFile(dir);

	wxDropSource source(this);
	source.SetData(obj);
	int res = source.DoDragDrop(wxDrag_AllowMove);

	bool handled_internally = pDragDropManager->pDropTarget != 0;

	pDragDropManager->Release();

	if (!handled_internally && (res == wxDragCopy || res == wxDragMove))
	{
		// We only need to refresh local side if the operation got handled
		// externally, the internal handlers do this for us already
		m_state.RefreshLocal();
	}
}

#ifdef __WXMSW__

wxString CLocalTreeView::GetSpecialFolder(int folder, int &iconIndex, int &openIconIndex)
{
	wxString name;

	LPITEMIDLIST list{};
	if (SHGetSpecialFolderLocation((HWND)GetHandle(), folder, &list) == S_OK) {
		SHFILEINFO shFinfo{};
		if (SHGetFileInfo((LPCTSTR)list, 0, &shFinfo, sizeof(shFinfo), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON) != 0) {
			DestroyIcon(shFinfo.hIcon);
			iconIndex = shFinfo.iIcon;
		}

		if (SHGetFileInfo((LPCTSTR)list, 0, &shFinfo, sizeof(shFinfo), SHGFI_PIDL | SHGFI_ICON | SHGFI_SMALLICON | SHGFI_OPENICON | SHGFI_DISPLAYNAME) != 0) {
			DestroyIcon(shFinfo.hIcon);
			openIconIndex = shFinfo.iIcon;
			name = shFinfo.szDisplayName;
		}

		CoTaskMemFree(list);
	}

	return name;
}

bool CLocalTreeView::CreateRoot()
{
	int iconIndex, openIconIndex;
	wxString name = GetSpecialFolder(CSIDL_DESKTOP, iconIndex, openIconIndex);
	if (name.empty())
	{
		name = _("Desktop");
		iconIndex = openIconIndex = -1;
	}

	m_desktop = AddRoot(name, iconIndex, openIconIndex);

	name = GetSpecialFolder(CSIDL_PERSONAL, iconIndex, openIconIndex);
	if (name.empty())
	{
		name = _("My Documents");
		iconIndex = openIconIndex = -1;
	}

	m_documents = AppendItem(m_desktop, name, iconIndex, openIconIndex);


	name = GetSpecialFolder(CSIDL_DRIVES, iconIndex, openIconIndex);
	if (name.empty())
	{
		name = _("My Computer");
		iconIndex = openIconIndex = -1;
	}

	m_drives = AppendItem(m_desktop, name, iconIndex, openIconIndex);

	DisplayDrives(m_drives);
	Expand(m_desktop);
	Expand(m_drives);

	return true;
}

void CLocalTreeView::OnVolumesEnumerated(wxCommandEvent& event)
{
	if (!m_pVolumeEnumeratorThread) {
		return;
	}

	std::list<CVolumeDescriptionEnumeratorThread::t_VolumeInfo> volumeInfo;
	volumeInfo = m_pVolumeEnumeratorThread->GetVolumes();

	if (event.GetEventType() == fzEVT_VOLUMESENUMERATED) {
		delete m_pVolumeEnumeratorThread;
		m_pVolumeEnumeratorThread = 0;
	}

	for (std::list<CVolumeDescriptionEnumeratorThread::t_VolumeInfo>::const_iterator iter = volumeInfo.begin(); iter != volumeInfo.end(); ++iter) {
		wxString drive = iter->volume;

		wxTreeItemIdValue tmp;
		wxTreeItemId item = GetFirstChild(m_drives, tmp);
		while (item) {
			wxString name = GetItemText(item);
			if (name == drive || name.Left(drive.Len() + 1) == drive + _T(" ")) {

				if (!iter->volumeName.empty()) {
					SetItemText(item, drive + _T(" (") + iter->volumeName + _T(")"));
				}
				if (iter->icon != -1) {
					SetItemImage(item, iter->icon);
				}
				break;
			}
			item = GetNextChild(m_drives, tmp);
		}
	}
}

#endif

void CLocalTreeView::OnContextMenu(wxTreeEvent& event)
{
	m_contextMenuItem = event.GetItem();
	if (!m_contextMenuItem.IsOk()) {
		return;
	}

	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_LOCALTREE"));
	if (!pMenu) {
		return;
	}

	CLocalPath const path(GetDirFromItem(m_contextMenuItem).ToStdWstring());

	const bool hasParent = path.HasParent();
	const bool writeable = path.IsWriteable();

	const bool remoteConnected = m_state.IsRemoteConnected() && !m_state.GetRemotePath().empty();

	bool const idle = m_state.GetLocalRecursiveOperation() && !m_state.GetLocalRecursiveOperation()->IsActive();

	pMenu->Enable(XRCID("ID_UPLOAD"), hasParent && remoteConnected && idle);
	pMenu->Enable(XRCID("ID_ADDTOQUEUE"), hasParent && remoteConnected && idle);
	pMenu->Enable(XRCID("ID_MKDIR"), writeable);
	pMenu->Enable(XRCID("ID_DELETE"), writeable && hasParent);
	pMenu->Enable(XRCID("ID_RENAME"), writeable && hasParent);

	PopupMenu(pMenu);
	delete pMenu;
}

void CLocalTreeView::OnMenuUpload(wxCommandEvent& event)
{
	auto recursiveOperation = m_state.GetLocalRecursiveOperation();
	if (recursiveOperation->IsActive()) {
		return;
	}

	if (!m_contextMenuItem.IsOk()) {
		return;
	}

	CLocalPath path(GetDirFromItem(m_contextMenuItem).ToStdWstring());

	if (!path.HasParent()) {
		return;
	}

	if (!m_state.IsRemoteConnected()) {
		return;
	}

	const CServer server = *m_state.GetServer();
	CServerPath remotePath = m_state.GetRemotePath();
	if (remotePath.empty()) {
		return;
	}

	if (!remotePath.ChangePath(GetItemText(m_contextMenuItem).ToStdWstring())) {
		return;
	}

	local_recursion_root root;
	root.add_dir_to_visit(path, remotePath);
	recursiveOperation->AddRecursionRoot(std::move(root));

	bool const queue_only = event.GetId() == XRCID("ID_ADDTOQUEUE");

	CFilterManager filter;
	recursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_transfer, filter.GetActiveFilters(), !queue_only);
}

// Create a new Directory
void CLocalTreeView::OnMenuMkdir(wxCommandEvent&)
{
	wxString newdir = MenuMkdir();
	if (!newdir.empty()) {
		RefreshListing();
		m_state.RefreshLocal();
	}
}

// Create a new Directory and enter the new Directory
void CLocalTreeView::OnMenuMkdirChgDir(wxCommandEvent&)
{
	std::wstring newdir = MenuMkdir().ToStdWstring();
	if (newdir.empty()) {
		return;
	}

	// OnMenuEnter
	std::wstring error;
	if (!m_state.SetLocalDir(newdir, &error)) {
		if (!error.empty()) {
			wxMessageBoxEx(error, _("Failed to change directory"), wxICON_INFORMATION);
		}
		else {
			wxBell();
		}
	}
}

// Helper-Function to create a new Directory
// Returns the name of the new directory
wxString CLocalTreeView::MenuMkdir()
{
	if (!m_contextMenuItem.IsOk()) {
		return wxString();
	}

	std::wstring path = GetDirFromItem(m_contextMenuItem).ToStdWstring();
	if (!path.empty() && path.back() != wxFileName::GetPathSeparator()) {
		path += wxFileName::GetPathSeparator();
	}

	if (!CLocalPath(path).IsWriteable()) {
		wxBell();
		return wxString();
	}

	CInputDialog dlg;
	if (!dlg.Create(this, _("Create directory"), _("Please enter the name of the directory which should be created:"))) {
		return wxString();
	}

	wxString newName = _("New directory");
	dlg.SetValue(path + newName);
	dlg.SelectText(path.size(), path.size() + newName.size());

	if (dlg.ShowModal() != wxID_OK) {
		return wxString();
	}

	wxFileName fn(dlg.GetValue(), _T(""));
	if (!fn.Normalize(wxPATH_NORM_ALL, path)) {
		wxBell();
		return wxString();
	}

	bool res;
	{
		wxLogNull log;
		res = fn.Mkdir(fn.GetPath(), 0777, wxPATH_MKDIR_FULL);
	}

	if (!res) {
		wxBell();
		return wxString();
	}

	return fn.GetPath();
}

void CLocalTreeView::OnMenuRename(wxCommandEvent&)
{
	if (!m_contextMenuItem.IsOk())
		return;

#ifdef __WXMSW__
	if (m_contextMenuItem == m_desktop || m_contextMenuItem == m_documents) {
		wxBell();
		return;
	}
#endif

	CLocalPath path(GetDirFromItem(m_contextMenuItem).ToStdWstring());
	if (!path.HasParent() || !path.IsWriteable()) {
		wxBell();
		return;
	}

	EditLabel(m_contextMenuItem);
}

void CLocalTreeView::OnMenuDelete(wxCommandEvent&)
{
	if (!m_contextMenuItem.IsOk()) {
		return;
	}

	std::wstring path = GetDirFromItem(m_contextMenuItem).ToStdWstring();

	CLocalPath local_path(path);
	if (!local_path.HasParent() || !local_path.IsWriteable()) {
		return;
	}

	gui_recursive_remove rmd(this);
	rmd.remove(fz::to_native(path));

	wxTreeItemId item = GetSelection();
	while (item && item != m_contextMenuItem) {
		item = GetItemParent(item);
	}

	if (!item) {
		if (GetItemParent(m_contextMenuItem) == GetSelection()) {
			m_state.RefreshLocal();
		}
		else {
			RefreshListing();
		}
		return;
	}

	if (!path.empty() && path.back() == wxFileName::GetPathSeparator()) {
		path.pop_back();
	}
	size_t pos = path.rfind(wxFileName::GetPathSeparator());
	if (pos == std::wstring::npos || !pos) {
		path = _T("/");
	}
	else {
		path = path.substr(0, pos);
	}

	m_state.SetLocalDir(path);
	RefreshListing();
}

void CLocalTreeView::OnBeginLabelEdit(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();

#ifdef __WXMSW__
	if (item == m_desktop || item == m_documents)
	{
		wxBell();
		event.Veto();
		return;
	}
#endif

	CLocalPath path(GetDirFromItem(item).ToStdWstring());

	if (!path.HasParent() || !path.IsWriteable())
	{
		wxBell();
		event.Veto();
		return;
	}
}

void CLocalTreeView::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled()) {
		event.Veto();
		return;
	}

	wxTreeItemId item = event.GetItem();

#ifdef __WXMSW__
	if (item == m_desktop || item == m_documents) {
		wxBell();
		event.Veto();
		return;
	}
#endif

	std::wstring path = GetDirFromItem(item).ToStdWstring();

	CLocalPath local_path(path);
	if (!local_path.HasParent() || !local_path.IsWriteable()) {
		wxBell();
		event.Veto();
		return;
	}

	if (!path.empty() && path.back() == wxFileName::GetPathSeparator()) {
		path.pop_back();
	}

	size_t pos = path.rfind(wxFileName::GetPathSeparator());
	wxASSERT(pos != std::wstring::npos);

	std::wstring parent = path.substr(0, pos + 1);

	const wxString& oldName = GetItemText(item);
	std::wstring const newName = event.GetLabel().ToStdWstring();
	if (newName.empty()) {
		wxBell();
		event.Veto();
		return;
	}

	wxASSERT(parent + oldName == path);

	if (oldName == newName) {
		return;
	}

	if (!RenameFile(this, parent, oldName, newName)) {
		event.Veto();
		return;
	}

	// We may call SetLocalDir, item might be deleted by it, so
	// if we don't rename item now and veto the event, wxWidgets
	// might access deleted item.
	event.Veto();
	SetItemText(item, newName);

	wxTreeItemId currentSel = GetSelection();
	if (currentSel == wxTreeItemId()) {
		RefreshListing();
		return;
	}

	if (item == currentSel) {
		m_state.SetLocalDir(parent + newName);
		return;
	}

	std::wstring sub;

	wxTreeItemId tmp = currentSel;
	while (tmp != GetRootItem() && tmp != item) {
		sub = (wxFileName::GetPathSeparator() + GetItemText(tmp) + sub).ToStdWstring();
		tmp = GetItemParent(tmp);
	}

	if (tmp == GetRootItem()) {
		// Rename unrelated to current selection
		return;
	}

	// Current selection below renamed item
	m_state.SetLocalDir(parent + newName + sub);
}

void CLocalTreeView::OnChar(wxKeyEvent& event)
{
	m_contextMenuItem = GetSelection();

	wxCommandEvent cmdEvt;
	if (event.GetKeyCode() == WXK_F2)
		OnMenuRename(cmdEvt);
	else if (event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_NUMPAD_DELETE)
		OnMenuDelete(cmdEvt);
	else
		event.Skip();
}

bool CLocalTreeView::CheckSubdirStatus(wxTreeItemId& item, const wxString& path)
{
	wxTreeItemIdValue value;
	wxTreeItemId child = GetFirstChild(item, value);

	static int64_t const size(-1);

#ifdef __WXMAC__
	// By default, OS X has a list of servers mounted into /net,
	// listing that directory is slow.
	if (GetItemParent(item) == GetRootItem() && (path == _T("/net") || path == _T("/net/")))
	{
		CFilterManager filter;

		const int attributes = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
		if (!filter.FilenameFiltered(_T("localhost"), path, true, size, true, attributes, fz::datetime()))
		{
			if (!child)
				AppendItem(item, _T(""));
			return true;
		}
	}
#endif

	if (child) {
		if (!GetItemText(child).empty()) {
			return false;
		}

		CTreeItemData* pData = (CTreeItemData*)GetItemData(child);
		if (pData) {
			bool wasLink;
			int attributes;
			fz::local_filesys::type type;
			fz::datetime date;
			if (!path.empty() && path.Last() == fz::local_filesys::path_separator) {
				type = fz::local_filesys::get_file_info(fz::to_native(path + pData->m_known_subdir), wasLink, 0, &date, &attributes);
			}
			else {
				type = fz::local_filesys::get_file_info(fz::to_native(path + fz::local_filesys::path_separator + pData->m_known_subdir), wasLink, 0, &date, &attributes);
			}
			if (type == fz::local_filesys::dir) {
				CFilterManager filter;
				if (!filter.FilenameFiltered(pData->m_known_subdir, path, true, size, true, attributes, date))
					return true;
			}
		}
	}

	wxString sub = HasSubdir(path);
	if (!sub.empty()) {
		wxTreeItemId subItem = AppendItem(item, _T(""));
		SetItemData(subItem, new CTreeItemData(sub.ToStdWstring()));
	}
	else if (child)
		Delete(child);

	return true;
}

#ifdef __WXMSW__
void CLocalTreeView::OnDevicechange(WPARAM wParam, LPARAM lParam)
{
	if (!m_drives)
		return;

	if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
		DEV_BROADCAST_HDR* pHdr = (DEV_BROADCAST_HDR*)lParam;
		if (pHdr->dbch_devicetype != DBT_DEVTYP_VOLUME)
			return;

		// Added or removed volume
		DEV_BROADCAST_VOLUME* pVolume = (DEV_BROADCAST_VOLUME*)lParam;

		wxChar drive = 'A';
		int mask = 1;
		while (drive <= 'Z') {
			if (pVolume->dbcv_unitmask & mask) {
				if (wParam == DBT_DEVICEARRIVAL)
					AddDrive(drive);
				else {
					RemoveDrive(drive);

					if (pVolume->dbcv_flags & DBTF_MEDIA) {
						// E.g. disk removed from CD-ROM drive, need to keep the drive letter
						AddDrive(drive);
					}
				}
			}
			drive++;
			mask <<= 1;
		}

		if (GetSelection() == m_drives)
			m_state.RefreshLocal();
	}
}

wxTreeItemId CLocalTreeView::AddDrive(wxChar letter)
{
	wxString drive = letter;
	drive += _T(":");

	long drivesToHide = CVolumeDescriptionEnumeratorThread::GetDrivesToHide();
	if( CVolumeDescriptionEnumeratorThread::IsHidden(drive.wc_str(), drivesToHide) ) {
		return wxTreeItemId();
	}

	// Get the label of the drive
	wxChar volumeName[501];
	int oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	BOOL res = GetVolumeInformation((drive + _T("\\")).wc_str(), volumeName, 500, 0, 0, 0, 0, 0);
	SetErrorMode(oldErrorMode);

	wxString itemLabel = drive;
	if (res && volumeName[0]) {
		itemLabel += _T(" (");
		itemLabel += volumeName;
		itemLabel += _T(")");
	}

	wxTreeItemIdValue value;
	wxTreeItemId driveItem = GetFirstChild(m_drives, value);
	while (driveItem)
	{
		if (!GetItemText(driveItem).Left(2).CmpNoCase(drive))
			break;

		driveItem = GetNextSibling(driveItem);
	}
	if (driveItem)
	{
		SetItemText(driveItem, itemLabel);
		int icon = GetIconIndex(iconType::dir, drive + _T("\\"));
		SetItemImage(driveItem, icon, wxTreeItemIcon_Normal);
		SetItemImage(driveItem, icon, wxTreeItemIcon_Selected);
		SetItemImage(driveItem, icon, wxTreeItemIcon_Expanded);
		SetItemImage(driveItem, icon, wxTreeItemIcon_SelectedExpanded);

		return driveItem;
	}

	wxTreeItemId item = AppendItem(m_drives, itemLabel, GetIconIndex(iconType::dir, drive + _T("\\")));
	AppendItem(item, _T(""));
	SortChildren(m_drives);

	return item;
}

void CLocalTreeView::RemoveDrive(wxChar drive)
{
	wxString driveName = drive;
	driveName += _T(":");
	wxTreeItemIdValue value;
	wxTreeItemId driveItem = GetFirstChild(m_drives, value);
	while (driveItem)
	{
		if (!GetItemText(driveItem).Left(2).CmpNoCase(driveName))
			break;

		driveItem = GetNextSibling(driveItem);
	}
	if (!driveItem)
		return;

	Delete(driveItem);
}

void CLocalTreeView::OnSelectionChanging(wxTreeEvent& event)
{
	// On-demand open icon for selected items
	wxTreeItemId item = event.GetItem();

	if (!item)
		return;

	if (GetItemImage(item, wxTreeItemIcon_Selected) == -1)
	{
		int icon = GetIconIndex(iconType::opened_dir, GetDirFromItem(item));
		SetItemImage(item, icon, wxTreeItemIcon_Selected);
		SetItemImage(item, icon, wxTreeItemIcon_SelectedExpanded);
	}
}

#endif

void CLocalTreeView::OnMenuOpen(wxCommandEvent&)
{
	if (!m_contextMenuItem.IsOk())
		return;

	wxString path = GetDirFromItem(m_contextMenuItem);
	if (path.empty())
		return;

	OpenInFileManager(path);
}

void CLocalTreeView::OnOptionsChanged(changed_options_t const& options)
{
	if (options.test(OPTION_FILELIST_NAMESORT)) {
		UpdateSortMode();
		RefreshListing();
	}
}
