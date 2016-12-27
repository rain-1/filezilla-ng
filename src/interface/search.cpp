#include <filezilla.h>

#define FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION

#include "search.h"
#include "commandqueue.h"
#include "filelistctrl.h"
#include "ipcmutex.h"
#include "Options.h"
#include "queue.h"
#include "remote_recursive_operation.h"
#include "sizeformatting.h"
#include "timeformatting.h"
#include "window_state_manager.h"
#include "xrc_helper.h"

#include "uri.h"

#include <wx/clipbrd.h>

class CRemoteSearchFileData final : public CDirentry
{
public:
	CServerPath path;
};

class CLocalSearchFileData final : public CLocalRecursiveOperation::listing::entry
{
public:
	bool is_dir() const { return dir; }
	bool dir{};
	CLocalPath path;
};

template<>
inline int DoCmpName(CRemoteSearchFileData const& data1, CRemoteSearchFileData const& data2, CFileListCtrlSortBase::NameSortMode const nameSortMode)
{
	int res;
	switch (nameSortMode)
	{
	case CFileListCtrlSortBase::namesort_casesensitive:
		res = CFileListCtrlSortBase::CmpCase(data1.name, data2.name);
		break;
	default:
	case CFileListCtrlSortBase::namesort_caseinsensitive:
		res = CFileListCtrlSortBase::CmpNoCase(data1.name, data2.name);
		break;
	case CFileListCtrlSortBase::namesort_natural:
		res = CFileListCtrlSortBase::CmpNatural(data1.name, data2.name);
		break;
	}

	if (!res) {
		if (data1.path < data2.path)
			res = -1;
		else if (data2.path < data1.path)
			res = 1;
	}

	return res;
}

template<>
inline int DoCmpName(CLocalSearchFileData const& data1, CLocalSearchFileData const& data2, CFileListCtrlSortBase::NameSortMode const nameSortMode)
{
	int res;
	switch (nameSortMode)
	{
	case CFileListCtrlSortBase::namesort_casesensitive:
		res = CFileListCtrlSortBase::CmpCase(data1.name, data2.name);
		break;
	default:
	case CFileListCtrlSortBase::namesort_caseinsensitive:
		res = CFileListCtrlSortBase::CmpNoCase(data1.name, data2.name);
		break;
	case CFileListCtrlSortBase::namesort_natural:
		res = CFileListCtrlSortBase::CmpNatural(data1.name, data2.name);
		break;
	}

	if (!res) {
		if (data1.path < data2.path)
			res = -1;
		else if (data2.path < data1.path)
			res = 1;
	}

	return res;
}

class CSearchDialogFileList final : public CFileListCtrl<CGenericFileData>
{
	friend class CSearchDialog;
	friend class CSearchSortType;
public:
	CSearchDialogFileList(CSearchDialog* pParent, CQueueView* pQueue);

	void clear();
	void set_mode(CSearchDialog::search_mode mode);

protected:
	virtual bool ItemIsDir(int index) const;

	virtual int64_t ItemGetSize(int index) const;

	CFileListCtrl<CGenericFileData>::CSortComparisonObject GetSortComparisonObject();

	CSearchDialog *m_searchDialog;

	virtual wxString GetItemText(int item, unsigned int column);
	virtual int OnGetItemImage(long item) const;

#ifdef __WXMSW__
	virtual int GetOverlayIndex(int item);
#endif

private:
	virtual bool CanStartComparison() { return false; }
	virtual void StartComparison() {}
	virtual bool get_next_file(wxString&, bool &, int64_t&, fz::datetime&) { return false; }
	virtual void CompareAddFile(CComparableListing::t_fileEntryFlags) {}
	virtual void FinishComparison() {}
	virtual void ScrollTopItem(int) {}
	virtual void OnExitComparisonMode() {}

	int m_dirIcon;

	CSearchDialog::search_mode mode_{};

	std::vector<CRemoteSearchFileData> remoteFileData_;
	std::vector<CLocalSearchFileData> localFileData_;
};

// Search dialog file list
// -----------------------

// Defined in RemoteListView.cpp
std::wstring StripVMSRevision(std::wstring const& name);

CSearchDialogFileList::CSearchDialogFileList(CSearchDialog* pParent, CQueueView* pQueue)
	: CFileListCtrl<CGenericFileData>(pParent, pQueue, true),
	m_searchDialog(pParent)
{
	m_hasParent = false;

	SetImageList(GetSystemImageList(), wxIMAGE_LIST_SMALL);

	m_dirIcon = GetIconIndex(iconType::dir);

	InitHeaderSortImageList();

	const unsigned long widths[7] = { 130, 130, 75, 80, 120, 80, 80 };

	AddColumn(_("Filename"), wxLIST_FORMAT_LEFT, widths[0]);
	AddColumn(_("Path"), wxLIST_FORMAT_LEFT, widths[1]);
	AddColumn(_("Filesize"), wxLIST_FORMAT_RIGHT, widths[2]);
	AddColumn(_("Filetype"), wxLIST_FORMAT_LEFT, widths[3]);
	AddColumn(_("Last modified"), wxLIST_FORMAT_LEFT, widths[4]);
	AddColumn(_("Permissions"), wxLIST_FORMAT_LEFT, widths[5]);
	AddColumn(_("Owner/Group"), wxLIST_FORMAT_LEFT, widths[6]);
	LoadColumnSettings(OPTION_SEARCH_COLUMN_WIDTHS, OPTION_SEARCH_COLUMN_SHOWN, OPTION_SEARCH_COLUMN_ORDER);

	InitSort(OPTION_SEARCH_SORTORDER);
}

bool CSearchDialogFileList::ItemIsDir(int index) const
{
	if (mode_ == CSearchDialog::search_mode::local) {
		return localFileData_[index].dir;
	}
	else {
		return remoteFileData_[index].is_dir();
	}
}

int64_t CSearchDialogFileList::ItemGetSize(int index) const
{
	if (mode_ == CSearchDialog::search_mode::local) {
		return localFileData_[index].size;
	}
	else {
		return remoteFileData_[index].size;
	}
}

CFileListCtrl<CGenericFileData>::CSortComparisonObject CSearchDialogFileList::GetSortComparisonObject()
{
	CFileListCtrlSortBase::DirSortMode dirSortMode = GetDirSortMode();
	CFileListCtrlSortBase::NameSortMode nameSortMode = GetNameSortMode();

	if (mode_ == CSearchDialog::search_mode::local) {
		if (!m_sortDirection) {
			if (m_sortColumn == 1)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortPath<std::vector<CLocalSearchFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 2)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortSize<std::vector<CLocalSearchFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 3)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortType<std::vector<CLocalSearchFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 4)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortTime<std::vector<CLocalSearchFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));

			else
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortNamePath<std::vector<CLocalSearchFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
		}
		else {
			if (m_sortColumn == 1)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortPath<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 2)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortSize<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 3)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortType<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 4)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortTime<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));

			else
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortNamePath<std::vector<CLocalSearchFileData>, CGenericFileData>, CGenericFileData>(localFileData_, m_fileData, dirSortMode, nameSortMode, this));
		}
	}
	else {
		if (!m_sortDirection) {
			if (m_sortColumn == 1)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortPath<std::vector<CRemoteSearchFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 2)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortSize<std::vector<CRemoteSearchFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 3)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortType<std::vector<CRemoteSearchFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 4)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortTime<std::vector<CRemoteSearchFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 5)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortPermissions<std::vector<CRemoteSearchFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 6)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortOwnerGroup<std::vector<CRemoteSearchFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortNamePath<std::vector<CRemoteSearchFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
		}
		else {
			if (m_sortColumn == 1)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortPath<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 2)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortSize<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 3)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortType<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 4)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortTime<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 5)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortPermissions<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else if (m_sortColumn == 6)
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortOwnerGroup<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
			else
				return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortNamePath<std::vector<CRemoteSearchFileData>, CGenericFileData>, CGenericFileData>(remoteFileData_, m_fileData, dirSortMode, nameSortMode, this));
		}
	}
}

wxString CSearchDialogFileList::GetItemText(int item, unsigned int column)
{
	if (item < 0 || item >= (int)m_indexMapping.size())
		return wxString();
	int index = m_indexMapping[item];

	if (mode_ == CSearchDialog::search_mode::local) {
		CLocalSearchFileData const& entry = localFileData_[index];
		if (!column)
			return entry.name;
		else if (column == 1)
			return entry.path.GetPath();
		else if (column == 2) {
			if (entry.is_dir() || entry.size < 0)
				return wxString();
			else
				return CSizeFormat::Format(entry.size);
		}
		else if (column == 3) {
			auto & data = m_fileData[index];
			if (data.fileType.empty()) {
				data.fileType = GetType(entry.name, entry.is_dir()).ToStdWstring();
			}

			return data.fileType;
		}
		else if (column == 4)
			return CTimeFormat::Format(entry.time);
	}
	else {
		CRemoteSearchFileData const& entry = remoteFileData_[index];
		if (!column)
			return entry.name;
		else if (column == 1)
			return entry.path.GetPath();
		else if (column == 2) {
			if (entry.is_dir() || entry.size < 0)
				return wxString();
			else
				return CSizeFormat::Format(entry.size);
		}
		else if (column == 3) {
			auto & data = m_fileData[index];
			if (data.fileType.empty()) {
				if (entry.path.GetType() == VMS)
					data.fileType = GetType(StripVMSRevision(entry.name), entry.is_dir()).ToStdWstring();
				else
					data.fileType = GetType(entry.name, entry.is_dir()).ToStdWstring();
			}

			return data.fileType;
		}
		else if (column == 4)
			return CTimeFormat::Format(entry.time);
		else if (column == 5)
			return *entry.permissions;
		else if (column == 6)
			return *entry.ownerGroup;
	}
	return wxString();
}

int CSearchDialogFileList::OnGetItemImage(long item) const
{
	CSearchDialogFileList *pThis = const_cast<CSearchDialogFileList *>(this);
	if (item < 0 || item >= (int)m_indexMapping.size())
		return -1;
	int index = m_indexMapping[item];

	int &icon = pThis->m_fileData[index].icon;

	if (icon != -2)
		return icon;

	if (mode_ == CSearchDialog::search_mode::local) {
		auto const& file = localFileData_[index];
		icon = pThis->GetIconIndex(iconType::file, file.path.GetPath() + file.name, true);
	}
	else {
		icon = pThis->GetIconIndex(iconType::file, remoteFileData_[index].name, false);
	}

	return icon;
}

void CSearchDialogFileList::clear()
{
	ClearSelection();
	m_indexMapping.clear();
	m_fileData.clear();
	localFileData_.clear();
	remoteFileData_.clear();
	SetItemCount(0);
	RefreshListOnly(true);
	GetFilelistStatusBar()->Clear();
}

void CSearchDialogFileList::set_mode(CSearchDialog::search_mode mode)
{
	mode_ = mode;
}

// Search dialog
// -------------

BEGIN_EVENT_TABLE(CSearchDialog, CFilterConditionsDialog)
EVT_BUTTON(XRCID("ID_START"), CSearchDialog::OnSearch)
EVT_BUTTON(XRCID("ID_STOP"), CSearchDialog::OnStop)
EVT_CONTEXT_MENU(CSearchDialog::OnContextMenu)
EVT_MENU(XRCID("ID_MENU_SEARCH_DOWNLOAD"), CSearchDialog::OnDownload)
EVT_MENU(XRCID("ID_MENU_SEARCH_UPLOAD"), CSearchDialog::OnUpload)
EVT_MENU(XRCID("ID_MENU_SEARCH_EDIT"), CSearchDialog::OnEdit)
EVT_MENU(XRCID("ID_MENU_SEARCH_DELETE"), CSearchDialog::OnDelete)
EVT_MENU(XRCID("ID_MENU_SEARCH_GETURL"), CSearchDialog::OnGetUrl)
EVT_MENU(XRCID("ID_MENU_SEARCH_GETURL_PASSWORD"), CSearchDialog::OnGetUrl)
EVT_CHAR_HOOK(CSearchDialog::OnCharHook)
EVT_RADIOBUTTON(XRCID("ID_LOCAL_SEARCH"), CSearchDialog::OnChangeSearchMode)
EVT_RADIOBUTTON(XRCID("ID_REMOTE_SEARCH"), CSearchDialog::OnChangeSearchMode)
END_EVENT_TABLE()

CSearchDialog::CSearchDialog(wxWindow* parent, CState& state, CQueueView* pQueue)
	: CStateEventHandler(state)
	, m_parent(parent)
	, m_pQueue(pQueue)
{
}

CSearchDialog::~CSearchDialog()
{
	if (m_pWindowStateManager) {
		m_pWindowStateManager->Remember(OPTION_SEARCH_SIZE);
		delete m_pWindowStateManager;
	}
}

bool CSearchDialog::Load()
{
	if (!wxDialogEx::Load(m_parent, _T("ID_SEARCH")))
		return false;

	// XRCed complains if adding a status bar to a dialog, so do it here instead
	CFilelistStatusBar* pStatusBar = new CFilelistStatusBar(this);
	pStatusBar->SetEmptyString(_("No search results"));
	pStatusBar->SetConnected(true);

	GetSizer()->Add(pStatusBar, 0, wxGROW);

	if (!CreateListControl(filter_name | filter_size | filter_path | filter_date))
		return false;

	m_results = new CSearchDialogFileList(this, 0);
	ReplaceControl(XRCCTRL(*this, "ID_RESULTS", wxWindow), m_results);

	m_results->SetFilelistStatusBar(pStatusBar);

	SetCtrlState();

	m_pWindowStateManager = new CWindowStateManager(this);
	m_pWindowStateManager->Restore(OPTION_SEARCH_SIZE, wxSize(750, 500));

	Layout();

	LoadConditions();
	EditFilter(m_search_filter);

	xrc_call(*this, "ID_CASE", &wxCheckBox::SetValue, m_search_filter.matchCase);
	xrc_call(*this, "ID_FIND_FILES", &wxCheckBox::SetValue, m_search_filter.filterFiles);
	xrc_call(*this, "ID_FIND_DIRS", &wxCheckBox::SetValue, m_search_filter.filterDirs);

	if (m_state.IsRemoteConnected()) {
		CServerPath const path = m_state.GetRemotePath();
		if (!path.empty())
			xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, path.GetPath());
	}
	else {
		CLocalPath const path = m_state.GetLocalDir();
		xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, path.GetPath());
		xrc_call(*this, "ID_REMOTE_SEARCH", &wxRadioButton::Disable);
		xrc_call(*this, "ID_LOCAL_SEARCH", &wxRadioButton::SetValue, true);
	}

	return true;
}

void CSearchDialog::Run()
{
	m_original_dir = m_state.GetRemotePath();

	m_state.RegisterHandler(this, STATECHANGE_REMOTE_DIR_OTHER, m_state.GetRemoteRecursiveOperation());
	m_state.RegisterHandler(this, STATECHANGE_REMOTE_IDLE, m_state.GetRemoteRecursiveOperation());
	m_state.RegisterHandler(this, STATECHANGE_LOCAL_RECURSION_LISTING);
	m_state.RegisterHandler(this, STATECHANGE_LOCAL_RECURSION_STATUS);

	ShowModal();

	SaveConditions();

	m_state.UnregisterHandler(this, STATECHANGE_LOCAL_RECURSION_STATUS);
	m_state.UnregisterHandler(this, STATECHANGE_LOCAL_RECURSION_LISTING);
	m_state.UnregisterHandler(this, STATECHANGE_REMOTE_IDLE);
	m_state.UnregisterHandler(this, STATECHANGE_REMOTE_DIR_OTHER);

	if (m_searching == search_mode::remote) {
		if (!m_state.IsRemoteIdle()) {
			m_state.m_pCommandQueue->Cancel();
			m_state.GetRemoteRecursiveOperation()->StopRecursiveOperation();
		}
		if (!m_original_dir.empty())
			m_state.ChangeRemoteDir(m_original_dir);
	}
	else {
		if (m_state.IsRemoteIdle() && !m_original_dir.empty())
			m_state.ChangeRemoteDir(m_original_dir);
	}
}

void CSearchDialog::OnStateChange(t_statechange_notifications notification, const wxString&, const void* data2)
{
	if (notification == STATECHANGE_REMOTE_DIR_OTHER && data2) {
		if (m_searching == search_mode::remote) {
			auto recursiveOperation = m_state.GetRemoteRecursiveOperation();
			if (recursiveOperation && recursiveOperation->GetOperationMode() == CRecursiveOperation::recursive_list) {
				std::shared_ptr<CDirectoryListing> const& listing = *reinterpret_cast<std::shared_ptr<CDirectoryListing> const*>(data2);
				ProcessDirectoryListing(listing);
			}
		}
	}
	else if (notification == STATECHANGE_REMOTE_IDLE) {
		if (m_searching == search_mode::remote) {
			if (m_state.IsRemoteIdle()) {
				m_searching = search_mode::none;
			}
		}
		if (m_searching != search_mode::local) {
			SetCtrlState();
		}
	}
	else if (notification == STATECHANGE_LOCAL_RECURSION_LISTING) {
		if (m_searching == search_mode::local && data2) {
			auto listing = reinterpret_cast<CLocalRecursiveOperation::listing const*>(data2);
			ProcessDirectoryListing(*listing);
		}
	}
	else if (notification == STATECHANGE_LOCAL_RECURSION_STATUS) {
		if (m_searching == search_mode::local && m_state.IsLocalIdle()) {
			m_searching = search_mode::none;
			SetCtrlState();
		}
	}
}

void CSearchDialog::ProcessDirectoryListing(std::shared_ptr<CDirectoryListing> const& listing)
{
	if (!listing || listing->failed())
		return;

	// Do not process same directory multiple times
	if (!m_visited.insert(listing->path).second)
		return;

	int old_count = m_results->m_fileData.size();
	int added_count = 0;

	wxString const path = listing->path.GetPath();

	bool const has_selections = m_results->GetSelectedItemCount() != 0;

	std::vector<int> added_indexes;
	if (has_selections) {
		added_indexes.reserve(listing->GetCount());
	}

	CFileListCtrl<CGenericFileData>::CSortComparisonObject compare = m_results->GetSortComparisonObject();
	for (unsigned int i = 0; i < listing->GetCount(); ++i) {
		const CDirentry& entry = (*listing)[i];

		if (!CFilterManager::FilenameFilteredByFilter(m_search_filter, entry.name, path, entry.is_dir(), entry.size, 0, entry.time)) {
			continue;
		}

		CRemoteSearchFileData remoteData;
		static_cast<CDirentry&>(remoteData) = entry;
		remoteData.path = listing->path;
		m_results->remoteFileData_.push_back(remoteData);

		CGenericFileData data;
		data.icon = entry.is_dir() ? m_results->m_dirIcon : -2;
		m_results->m_fileData.push_back(data);

		auto insertPos = std::lower_bound(m_results->m_indexMapping.begin(), m_results->m_indexMapping.end(), old_count + added_count, compare);
		int const added_index = insertPos - m_results->m_indexMapping.begin();
		m_results->m_indexMapping.insert(insertPos, old_count + added_count++);

		// Remember inserted index
		if (has_selections) {
			auto const added_indexes_insert_pos = std::lower_bound(added_indexes.begin(), added_indexes.end(), added_index);
			for (auto index = added_indexes_insert_pos; index != added_indexes.end(); ++index) {
				++(*index);
			}
			added_indexes.insert(added_indexes_insert_pos, added_index);
		}

		if (entry.is_dir())
			m_results->GetFilelistStatusBar()->AddDirectory();
		else
			m_results->GetFilelistStatusBar()->AddFile(entry.size);
	}
	compare.Destroy();

	if (added_count) {
		m_results->SetItemCount(old_count + added_count);
		m_results->UpdateSelections_ItemsAdded(added_indexes);
		m_results->RefreshListOnly(false);
	}
}

void CSearchDialog::ProcessDirectoryListing(CLocalRecursiveOperation::listing const& listing)
{
	int old_count = m_results->m_fileData.size();
	int added_count = 0;

	wxString const path = listing.localPath.GetPath();

	bool const has_selections = m_results->GetSelectedItemCount() != 0;

	std::vector<int> added_indexes;
	if (has_selections) {
		added_indexes.reserve(listing.files.size() + listing.dirs.size());
	}

	CFileListCtrl<CGenericFileData>::CSortComparisonObject compare = m_results->GetSortComparisonObject();

	auto const& add_entry = [&](CLocalRecursiveOperation::listing::entry const& entry, bool dir) {
		if (!CFilterManager::FilenameFilteredByFilter(m_search_filter, entry.name, path, dir, entry.size, entry.attributes, entry.time)) {
			return;
		}

		CLocalSearchFileData localData;
		static_cast<CLocalRecursiveOperation::listing::entry&>(localData) = entry;
		localData.path = listing.localPath;
		localData.dir = dir;
		m_results->localFileData_.push_back(localData);

		CGenericFileData data;
		data.icon = dir ? m_results->m_dirIcon : -2;
		m_results->m_fileData.push_back(data);

		auto insertPos = std::lower_bound(m_results->m_indexMapping.begin(), m_results->m_indexMapping.end(), old_count + added_count, compare);
		int const added_index = insertPos - m_results->m_indexMapping.begin();
		m_results->m_indexMapping.insert(insertPos, old_count + added_count++);

		// Remember inserted index
		if (has_selections) {
			auto const added_indexes_insert_pos = std::lower_bound(added_indexes.begin(), added_indexes.end(), added_index);
			for (auto index = added_indexes_insert_pos; index != added_indexes.end(); ++index) {
				++(*index);
			}
			added_indexes.insert(added_indexes_insert_pos, added_index);
		}

		if (dir) {
			m_results->GetFilelistStatusBar()->AddDirectory();
		}
		else {
			m_results->GetFilelistStatusBar()->AddFile(entry.size);
		}
	};

	for (auto const& file : listing.files) {
		add_entry(file, false);
	}
	for (auto const& dir : listing.dirs) {
		add_entry(dir, true);
	}
	compare.Destroy();

	if (added_count) {
		m_results->SetItemCount(old_count + added_count);
		m_results->UpdateSelections_ItemsAdded(added_indexes);
		m_results->RefreshListOnly(false);
	}
}

void CSearchDialog::OnSearch(wxCommandEvent&)
{
	bool const localSearch = xrc_call(*this, "ID_LOCAL_SEARCH", &wxRadioButton::GetValue);

	if (localSearch) {
		if (!m_state.IsLocalIdle()) {
			wxBell();
			return;
		}

		CLocalPath path;
		if (!path.SetPath(xrc_call(*this, "ID_PATH", &wxTextCtrl::GetValue).ToStdWstring()) || path.empty()) {
			wxMessageBoxEx(_("Need to enter valid local path"), _("Local file search"), wxICON_EXCLAMATION);
			return;
		}

		std::wstring error;
		if (!path.Exists(&error)) {
			wxMessageBoxEx(error, _("Local file search"), wxICON_EXCLAMATION);
			return;
		}

		m_local_search_root = path;
	}
	else {
		if (!m_state.IsRemoteIdle()) {
			wxBell();
			return;
		}

		CServerPath path;

		const CServer* pServer = m_state.GetServer();
		if (!pServer) {
			wxMessageBoxEx(_("Connection to server lost."), _("Remote file search"), wxICON_EXCLAMATION);
			return;
		}
		path.SetType(pServer->GetType());
		if (!path.SetPath(xrc_call(*this, "ID_PATH", &wxTextCtrl::GetValue).ToStdWstring()) || path.empty()) {
			wxMessageBoxEx(_("Need to enter valid remote path"), _("Remote file search"), wxICON_EXCLAMATION);
			return;
		}

		m_remote_search_root = path;
	}

	// Prepare filter
	wxString error;
	if (!ValidateFilter(error, true)) {
		wxMessageBoxEx(wxString::Format(_("Invalid search conditions: %s"), error), _("File search"), wxICON_EXCLAMATION);
		return;
	}
	m_search_filter = GetFilter();
	if (!CFilterManager::CompileRegexes(m_search_filter)) {
		wxMessageBoxEx(_("Invalid regular expression in search conditions."), _("File search"), wxICON_EXCLAMATION);
		return;
	}
	m_search_filter.matchCase = xrc_call(*this, "ID_CASE", &wxCheckBox::GetValue);
	m_search_filter.filterFiles = xrc_call(*this, "ID_FIND_FILES", &wxCheckBox::GetValue);
	m_search_filter.filterDirs = xrc_call(*this, "ID_FIND_DIRS", &wxCheckBox::GetValue);

	m_searching = localSearch ? search_mode::local : search_mode::remote;

	// Delete old results
	m_results->clear();
	m_results->set_mode(m_searching);
	m_visited.clear();

	// Start

	if (localSearch) {
		local_recursion_root root;
		root.add_dir_to_visit(m_local_search_root);
		m_state.GetLocalRecursiveOperation()->AddRecursionRoot(std::move(root));
		ActiveFilters const filters; // Empty, recurse into everything
		m_state.GetLocalRecursiveOperation()->StartRecursiveOperation(CRecursiveOperation::recursive_list, filters);
	}
	else {
		recursion_root root(m_remote_search_root, true);
		root.add_dir_to_visit_restricted(m_remote_search_root, std::wstring(), true);
		m_state.GetRemoteRecursiveOperation()->AddRecursionRoot(std::move(root));
		ActiveFilters const filters; // Empty, recurse into everything
		m_state.GetRemoteRecursiveOperation()->StartRecursiveOperation(CRecursiveOperation::recursive_list, filters, m_remote_search_root);
	}

	SetCtrlState();
}

void CSearchDialog::OnStop(wxCommandEvent&)
{
	if (m_searching == search_mode::remote) {
		if (!m_state.IsRemoteIdle()) {
			m_state.m_pCommandQueue->Cancel();
			m_state.GetRemoteRecursiveOperation()->StopRecursiveOperation();
		}
	}
	else if (m_searching == search_mode::local) {
		if (!m_state.IsLocalIdle()) {
			m_state.GetLocalRecursiveOperation()->StopRecursiveOperation();
		}
	}
}

void CSearchDialog::SetCtrlState()
{
	bool const localSearch = xrc_call(*this, "ID_LOCAL_SEARCH", &wxRadioButton::GetValue);

	bool idle = m_searching == search_mode::none;
	if (idle) {
		if (!localSearch) {
			if (!m_state.IsRemoteIdle()) {
				idle = false;
			}
		}
		else {
			if (!m_state.IsLocalIdle()) {
				idle = false;
			}
		}
	}

	XRCCTRL(*this, "ID_START", wxButton)->Enable(idle);
	XRCCTRL(*this, "ID_STOP", wxButton)->Enable(!idle);
}

void CSearchDialog::OnContextMenu(wxContextMenuEvent& event)
{
	if (event.GetEventObject() != m_results && event.GetEventObject() != m_results->GetMainWindow()) {
		event.Skip();
		return;
	}

	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_SEARCH"));
	if (!pMenu) {
		return;
	}

	if (!m_state.IsRemoteIdle() || !m_state.IsRemoteConnected()) {
		pMenu->Enable(XRCID("ID_MENU_SEARCH_UPLOAD"), false);
		pMenu->Enable(XRCID("ID_MENU_SEARCH_DOWNLOAD"), false);
		pMenu->Enable(XRCID("ID_MENU_SEARCH_DELETE"), false);
		pMenu->Enable(XRCID("ID_MENU_SEARCH_EDIT"), false);
	}

	bool const localSearch = m_results->mode_ == search_mode::local;
	if (localSearch) {
		pMenu->Delete(XRCID("ID_MENU_SEARCH_DOWNLOAD"));
		pMenu->Delete(XRCID("ID_MENU_SEARCH_DELETE"));
		pMenu->Delete(XRCID("ID_MENU_SEARCH_EDIT"));
		pMenu->Delete(XRCID("ID_MENU_SEARCH_GETURL"));
		pMenu->Delete(XRCID("ID_MENU_SEARCH_GETURL_PASSWORD"));
	}
	else {
		pMenu->Delete(XRCID("ID_MENU_SEARCH_UPLOAD"));
		pMenu->Delete(XRCID(wxGetKeyState(WXK_SHIFT) ? "ID_MENU_SEARCH_GETURL" : "ID_MENU_SEARCH_GETURL_PASSWORD"));
	}

	PopupMenu(pMenu);
	delete pMenu;
}



class CSearchTransferDialog final : public wxDialogEx
{
public:
	bool Run(wxWindow* parent, bool download, const wxString& path, int count_files, int count_dirs)
	{
		download_ = download;

		if (!Load(parent, download ? _T("ID_SEARCH_DOWNLOAD") : _T("ID_SEARCH_UPLOAD")))
			return false;

		wxString desc;
		if (!count_dirs)
			desc.Printf(wxPLURAL("Selected %d file for transfer.", "Selected %d files for transfer.", count_files), count_files);
		else if (!count_files)
			desc.Printf(wxPLURAL("Selected %d directory with its contents for transfer.", "Selected %d directories with their contents for transfer.", count_dirs), count_dirs);
		else {
			wxString files = wxString::Format(wxPLURAL("%d file", "%d files", count_files), count_files);
			wxString dirs = wxString::Format(wxPLURAL("%d directory with its contents", "%d directories with their contents", count_dirs), count_dirs);
			desc.Printf(_("Selected %s and %s for transfer."), files, dirs);
		}
		XRCCTRL(*this, "ID_DESC", wxStaticText)->SetLabel(desc);

		if (download) {
			XRCCTRL(*this, "ID_LOCALPATH", wxTextCtrl)->ChangeValue(path);
		}
		else {
			XRCCTRL(*this, "ID_REMOTEPATH", wxTextCtrl)->ChangeValue(path);
		}

		if (ShowModal() != wxID_OK)
			return false;

		return true;
	}

protected:
	bool download_{};

	DECLARE_EVENT_TABLE()
	void OnBrowse(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
};

BEGIN_EVENT_TABLE(CSearchTransferDialog, wxDialogEx)
EVT_BUTTON(XRCID("ID_BROWSE"), CSearchTransferDialog::OnBrowse)
EVT_BUTTON(XRCID("wxID_OK"), CSearchTransferDialog::OnOK)
END_EVENT_TABLE()

void CSearchTransferDialog::OnBrowse(wxCommandEvent&)
{
	wxTextCtrl *pText = XRCCTRL(*this, "ID_LOCALPATH", wxTextCtrl);
	if (!pText) {
		return;
	}

	wxDirDialog dlg(this, _("Select target download directory"), pText->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK)
		pText->ChangeValue(dlg.GetPath());
}

void CSearchTransferDialog::OnOK(wxCommandEvent&)
{
	if (download_) {
		wxTextCtrl *pText = XRCCTRL(*this, "ID_LOCALPATH", wxTextCtrl);

		CLocalPath path(pText->GetValue().ToStdWstring());
		if (path.empty()) {
			wxMessageBoxEx(_("You have to enter a local directory."), _("Download search results"), wxICON_EXCLAMATION);
			return;
		}

		if (!path.IsWriteable()) {
			wxMessageBoxEx(_("You have to enter a writable local directory."), _("Download search results"), wxICON_EXCLAMATION);
			return;
		}
	}
	else {
		wxTextCtrl *pText = XRCCTRL(*this, "ID_REMOTEPATH", wxTextCtrl);

		CServerPath path(pText->GetValue().ToStdWstring());
		if (path.empty()) {
			wxMessageBoxEx(_("You have to enter a remote directory."), _("Upload search results"), wxICON_EXCLAMATION);
			return;
		}
	}

	EndDialog(wxID_OK);
}

namespace {

bool isSubdir(CServerPath const& a, CServerPath const&b) {
	return a.IsSubdirOf(b, false);
}

bool isSubdir(CLocalPath const& a, CLocalPath const&b) {
	return a.IsSubdirOf(b);
}

template<typename Path, typename FileData>
void ProcessSelection(std::list<int> &selected_files, std::deque<Path> &selected_dirs, std::vector<FileData> const& fileData, CSearchDialogFileList const* results)
{
	std::deque<Path> dirs;

	int sel = -1;
	while ((sel = results->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (static_cast<size_t>(sel) >= results->indexMapping().size()) {
			continue;
		}
		int index = results->indexMapping()[sel];

		if (fileData[index].is_dir()) {
			Path path = fileData[index].path;
			path.ChangePath(fileData[index].name);
			if (path.empty())
				continue;

			dirs.push_back(path);
		}
		else
			selected_files.push_back(index);
	}

	// Make sure that selected_dirs does not contain
	// any directories that are in a parent-child relationship
	// Resolve by only keeping topmost parents
	std::sort(dirs.begin(), dirs.end());
	for (Path const& path : dirs) {
		if (!selected_dirs.empty() && (isSubdir(path, selected_dirs.back()) || path == selected_dirs.back())) {
			continue;
		}
		selected_dirs.push_back(path);
	}

	// Now in a second phase filter out all files that are also in a directory
	std::list<int> selected_files_new;
	for (auto const& sel_file : selected_files) {
		Path const& path = fileData[sel_file].path;
		typename std::deque<Path>::iterator path_iter;
		for (path_iter = selected_dirs.begin(); path_iter != selected_dirs.end(); ++path_iter) {
			if (*path_iter == path || isSubdir(path, *path_iter)) {
				break;
			}
		}
		if (path_iter == selected_dirs.end())
			selected_files_new.push_back(sel_file);
	}
	selected_files.swap(selected_files_new);

	// At this point selected_dirs contains uncomparable
	// paths and selected_files contains only files not
	// covered by any of those directories.
}
}

void CSearchDialog::OnDownload(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	// Find all selected files and directories
	std::deque<CServerPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, m_results->remoteFileData_, m_results);

	if (selected_files.empty() && selected_dirs.empty()) {
		return;
	}

	CSearchTransferDialog dlg;
	if (!dlg.Run(this, true, m_state.GetLocalDir().GetPath(), selected_files.size(), selected_dirs.size())) {
		return;
	}

	wxTextCtrl *pText = XRCCTRL(dlg, "ID_LOCALPATH", wxTextCtrl);

	CLocalPath path(pText->GetValue().ToStdWstring());
	if (path.empty() || !path.IsWriteable()) {
		wxBell();
		return;
	}

	CServer const* pServer = m_state.GetServer();
	if (!pServer) {
		wxBell();
		return;
	}

	bool start = XRCCTRL(dlg, "ID_QUEUE_START", wxRadioButton)->GetValue();
	bool flatten = XRCCTRL(dlg, "ID_PATHS_FLATTEN", wxRadioButton)->GetValue();

	for (auto const& sel : selected_files) {
		CRemoteSearchFileData const& entry = m_results->remoteFileData_[sel];

		CLocalPath target_path = path;
		if (!flatten) {
			// Append relative path to search root to local target path
			CServerPath remote_path = entry.path;
			std::list<std::wstring> segments;
			while (m_remote_search_root.IsParentOf(remote_path, false) && remote_path.HasParent()) {
				segments.push_front(remote_path.GetLastSegment());
				remote_path = remote_path.GetParent();
			}
			for (auto const& segment : segments) {
				target_path.AddSegment(segment);
			}
		}

		CServerPath remote_path = entry.path;
		std::wstring localName = CQueueView::ReplaceInvalidCharacters(entry.name);
		if (!entry.is_dir() && remote_path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION))
			localName = StripVMSRevision(localName);

		m_pQueue->QueueFile(!start, true,
			entry.name, (localName != entry.name) ? localName : std::wstring(),
			target_path, remote_path, *pServer, entry.size);
	}
	m_pQueue->QueueFile_Finish(start);

	auto const mode = flatten ? CRecursiveOperation::recursive_transfer_flatten : CRecursiveOperation::recursive_transfer;

	for (auto const& dir : selected_dirs) {
		CLocalPath target_path = path;
		if (!flatten && dir.HasParent()) {
			target_path.AddSegment(dir.GetLastSegment());
		}

		recursion_root root(dir, true);
		root.add_dir_to_visit(dir, std::wstring(), target_path, false);
		m_state.GetRemoteRecursiveOperation()->AddRecursionRoot(std::move(root));
	}
	ActiveFilters const filters; // Empty, recurse into everything
	m_state.GetRemoteRecursiveOperation()->StartRecursiveOperation(mode, filters, m_original_dir, start);
}

void CSearchDialog::OnUpload(wxCommandEvent&)
{
	if (!m_state.IsLocalIdle()) {
		return;
	}

	// Find all selected files and directories
	std::deque<CLocalPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, m_results->localFileData_, m_results);

	if (selected_files.empty() && selected_dirs.empty()) {
		return;
	}

	CSearchTransferDialog dlg;
	if (!dlg.Run(this, false, m_state.GetRemotePath().GetPath(), selected_files.size(), selected_dirs.size()))
		return;

	wxTextCtrl *pText = XRCCTRL(dlg, "ID_REMOTEPATH", wxTextCtrl);

	CServerPath path(pText->GetValue().ToStdWstring());
	if (path.empty()) {
		wxBell();
		return;
	}

	CServer const* pServer = m_state.GetServer();
	if (!pServer) {
		wxBell();
		return;
	}

	bool start = XRCCTRL(dlg, "ID_QUEUE_START", wxRadioButton)->GetValue();
	bool flatten = XRCCTRL(dlg, "ID_PATHS_FLATTEN", wxRadioButton)->GetValue();

	for (auto const& sel : selected_files) {
		CLocalSearchFileData const& entry = m_results->localFileData_[sel];

		CServerPath target_path = path;
		if (!flatten) {
			// Append relative path to search root to local target path
			CLocalPath local_path = entry.path;
			std::list<std::wstring> segments;
			while (m_local_search_root.IsParentOf(local_path) && local_path.HasParent()) {
				segments.push_front(local_path.GetLastSegment());
				local_path = local_path.GetParent();
			}
			for (auto const& segment : segments) {
				target_path.AddSegment(segment);
			}
		}

		CLocalPath local_path = entry.path;
		std::wstring localName = CQueueView::ReplaceInvalidCharacters(entry.name);

		m_pQueue->QueueFile(!start, false,
			entry.name, (localName != entry.name) ? localName : std::wstring(),
			local_path, target_path, *pServer, entry.size);
	}
	m_pQueue->QueueFile_Finish(start);

	auto const mode = flatten ? CRecursiveOperation::recursive_transfer_flatten : CRecursiveOperation::recursive_transfer;

	for (auto const& dir : selected_dirs) {
		CServerPath target_path = path;
		if (!flatten && dir.HasParent()) {
			target_path.AddSegment(dir.GetLastSegment());
		}

		local_recursion_root root;
		root.add_dir_to_visit(dir, target_path);
		m_state.GetLocalRecursiveOperation()->AddRecursionRoot(std::move(root));
	}
	ActiveFilters const filters; // Empty, recurse into everything
	m_state.GetLocalRecursiveOperation()->StartRecursiveOperation(mode, filters, start);
}

void CSearchDialog::OnEdit(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	if (m_results->mode_ != search_mode::remote) {
		return;
	}

	// Find all selected files and directories
	std::deque<CServerPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, m_results->remoteFileData_, m_results);

	if (selected_files.empty() && selected_dirs.empty())
		return;

	if (!selected_dirs.empty()) {
		wxMessageBoxEx(_("Editing directories is not supported"), _("Editing search results"), wxICON_EXCLAMATION);
		return;
	}

	CEditHandler* pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		wxBell();
		return;
	}

	const wxString& localDir = pEditHandler->GetLocalDirectory();
	if (localDir.empty()) {
		wxMessageBoxEx(_("Could not get temporary directory to download file into."), _("Cannot edit file"), wxICON_STOP);
		return;
	}

	const CServer* pServer = m_state.GetServer();
	if (!pServer) {
		wxBell();
		return;
	}

	if (selected_files.size() > 10) {
		CConditionalDialog dlg(this, CConditionalDialog::many_selected_for_edit, CConditionalDialog::yesno);
		dlg.SetTitle(_("Confirmation needed"));
		dlg.AddText(_("You have selected more than 10 files for editing, do you really want to continue?"));

		if (!dlg.Run())
			return;
	}

	for (auto const& item : selected_files) {
		const CDirentry& entry = m_results->remoteFileData_[item];
		const CServerPath path = m_results->remoteFileData_[item].path;

		pEditHandler->Edit(CEditHandler::remote, entry.name, path, *pServer, entry.size, this);
	}
}

void CSearchDialog::OnDelete(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		return;
	}

	if (m_results->mode_ != search_mode::remote) {
		return;
	}

	// Find all selected files and directories
	std::deque<CServerPath> selected_dirs;
	std::list<int> selected_files;
	ProcessSelection(selected_files, selected_dirs, m_results->remoteFileData_, m_results);

	if (selected_files.empty() && selected_dirs.empty())
		return;

	wxString question;
	if (selected_dirs.empty())
		question.Printf(wxPLURAL("Really delete %d file from the server?", "Really delete %d files from the server?", selected_files.size()), selected_files.size());
	else if (selected_files.empty())
		question.Printf(wxPLURAL("Really delete %d directory with its contents from the server?", "Really delete %d directories with their contents from the server?", selected_dirs.size()), selected_dirs.size());
	else {
		wxString files = wxString::Format(wxPLURAL("%d file", "%d files", selected_files.size()), selected_files.size());
		wxString dirs = wxString::Format(wxPLURAL("%d directory with its contents", "%d directories with their contents", selected_dirs.size()), selected_dirs.size());
		question.Printf(_("Really delete %s and %s from the server?"), files, dirs);
	}

	if (wxMessageBoxEx(question, _("Confirm deletion"), wxICON_QUESTION | wxYES_NO) != wxYES)
		return;

	for (auto const& file : selected_files) {
		CDirentry const& entry = m_results->remoteFileData_[file];
		std::deque<std::wstring> files_to_delete;
		files_to_delete.push_back(entry.name);
		m_state.m_pCommandQueue->ProcessCommand(new CDeleteCommand(m_results->remoteFileData_[file].path, std::move(files_to_delete)));
	}

	for (auto path : selected_dirs) {
		std::wstring segment;
		if (path.HasParent()) {
			segment = path.GetLastSegment();
			path = path.GetParent();
		}
		recursion_root root(path, !path.HasParent());
		root.add_dir_to_visit(path, segment);
		m_state.GetRemoteRecursiveOperation()->AddRecursionRoot(std::move(root));
	}
	ActiveFilters const filters; // Empty, recurse into everything
	m_state.GetRemoteRecursiveOperation()->StartRecursiveOperation(CRecursiveOperation::recursive_delete, filters, m_original_dir);
}

void CSearchDialog::OnCharHook(wxKeyEvent& event)
{
	if (IsEscapeKey(event)) {
		EndDialog(wxID_CANCEL);
		return;
	}

	event.Skip();
}

#ifdef __WXMSW__
int CSearchDialogFileList::GetOverlayIndex(int item)
{
	if (mode_ == CSearchDialog::search_mode::local) {
		return 0;
	}
	if (item < 0 || item >= (int)m_indexMapping.size()) {
		return 0;
	}
	int index = m_indexMapping[item];

	if (remoteFileData_[index].is_link()) {
		return GetLinkOverlayIndex();
	}

	return 0;
}
#endif

void CSearchDialog::LoadConditions()
{
	CInterProcessMutex mutex(MUTEX_SEARCHCONDITIONS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("search")));
	auto document = file.Load(true);
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return;
	}

	auto filter = document.child("Filter");
	if (!filter)
		return;

	if (!CFilterManager::LoadFilter(filter, m_search_filter))
		m_search_filter = CFilter();
}

void CSearchDialog::SaveConditions()
{
	CInterProcessMutex mutex(MUTEX_SEARCHCONDITIONS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("search")));
	auto document = file.Load(true);
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return;
	}

	pugi::xml_node filter;
	while ((filter = document.child("Filter")))
		document.remove_child(filter);
	filter = document.append_child("Filter");

	CFilterManager::SaveFilter(filter, m_search_filter);

	file.Save(true);
}

void CSearchDialog::OnChangeSearchMode(wxCommandEvent&)
{
	wxString const strPath = xrc_call(*this, "ID_PATH", &wxTextCtrl::GetValue);

	CLocalPath const localPath = m_state.GetLocalDir();
	CServerPath const remotePath = m_state.GetRemotePath();

	bool const local = xrc_call(*this, "ID_LOCAL_SEARCH", &wxRadioButton::GetValue);
	if (local) {
		if (strPath == remotePath.GetPath() && !localPath.empty()) {
			xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, localPath.GetPath());
		}
	}
	else {
		if (strPath == localPath.GetPath() && !remotePath.empty()) {
			xrc_call(*this, "ID_PATH", &wxTextCtrl::ChangeValue, remotePath.GetPath());
		}
	}

}

void CSearchDialog::OnGetUrl(wxCommandEvent& event)
{
	if (m_results->mode_ != search_mode::remote) {
		return;
	}

	CServer const* pServer = m_state.GetServer();
	if (!pServer) {
		wxBell();
		return;
	}

	if (!wxTheClipboard->Open()) {
		wxMessageBoxEx(_("Could not open clipboard"), _("Could not copy URLs"), wxICON_EXCLAMATION);
		return;
	}

	wxString const server = pServer->Format((event.GetId() == XRCID("ID_MENU_SEARCH_GETURL_PASSWORD")) ? ServerFormat::url_with_password : ServerFormat::url);

	auto getUrl = [](wxString const& serverPart, CServerPath const& path, std::wstring const& name) {
		wxString url = serverPart;

		auto const pathPart = fz::percent_encode_w(path.FormatFilename(name, false), true);
		if (!pathPart.empty() && pathPart[0] != '/') {
			url += '/';
		}
		url += pathPart;

		return url;
	};

	wxString urls;

	int sel = -1;
	while ((sel = m_results->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) >= 0) {
		if (static_cast<size_t>(sel) >= m_results->m_indexMapping.size()) {
			continue;
		}
		int index = m_results->m_indexMapping[sel];

		auto const& entry = m_results->remoteFileData_[index];
		urls += getUrl(server, entry.path, entry.name);
#ifdef __WXMSW__
		urls += _T("\r\n");
#else
		urls += _T("\n");
#endif
	}

	wxTheClipboard->SetData(new wxURLDataObject(urls));

	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}
