#include <filezilla.h>
#include "Options.h"
#include "queue.h"
#include "queueview_failed.h"
#include "queueview_successful.h"
#include "sizeformatting.h"
#include "timeformatting.h"
#include "themeprovider.h"

CQueueItem::CQueueItem(CQueueItem* parent)
	: m_parent(parent)
{
}

CQueueItem::~CQueueItem()
{
	for (auto iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter)
		delete *iter;
}

void CQueueItem::SetPriority(QueuePriority priority)
{
	for (auto iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter)
		(*iter)->SetPriority(priority);
}

void CQueueItem::AddChild(CQueueItem* item)
{
	wxASSERT(GetType() != QueueItemType::Folder);
	wxASSERT(GetType() != QueueItemType::Status);

	if (m_removed_at_front) {
		m_children.erase(m_children.begin(), m_children.begin() + m_removed_at_front);
		m_removed_at_front = 0;
	}
	m_children.push_back(item);

	CQueueItem* parent = GetParent();
	while (parent) {
		if (parent->GetType() == QueueItemType::Server) {
			static_cast<CServerItem*>(parent)->m_visibleOffspring += 1 + item->GetChildrenCount(true);
			static_cast<CServerItem*>(parent)->m_maxCachedIndex = -1;
		}
		parent = parent->GetParent();
	}
}


CQueueItem* CQueueItem::GetChild(unsigned int item, bool recursive)
{
	if (!recursive) {
		if (item + m_removed_at_front >= m_children.size())
			return 0;
		return *(m_children.begin() + item + m_removed_at_front);
	}

	std::vector<CQueueItem*>::iterator iter = m_children.begin() + m_removed_at_front;
	for (; iter != m_children.end(); ++iter) {
		if (!item)
			return *iter;

		unsigned int count = (*iter)->GetChildrenCount(true);
		if (item > count) {
			item -= count + 1;
			continue;
		}

		return (*iter)->GetChild(item - 1);
	}
	return 0;
}

unsigned int CQueueItem::GetChildrenCount(bool recursive) const
{
	unsigned int count{};
	if (!recursive) {
		count = m_children.size() - m_removed_at_front;
	}
	else {
		for (auto iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter) {
			count += 1 + (*iter)->GetChildrenCount(true);
		}
	}

	return count;
}

bool CQueueItem::RemoveChild(CQueueItem* pItem, bool destroy, bool forward)
{
	int const oldVisibleOffspring = GetChildrenCount(true);
	int visibleOffspring = oldVisibleOffspring;

	bool deleted = false;

	auto doRemove = [&](std::vector<CQueueItem*>::iterator iter) {
		if (*iter == pItem) {
			visibleOffspring -= 1;
			visibleOffspring -= pItem->GetChildrenCount(true);
			if (destroy)
				delete pItem;

			if (iter - m_children.begin() - m_removed_at_front <= 10) {
				++m_removed_at_front;
				unsigned int end = iter - m_children.begin();
				int c = 0;
				for (int i = end; i >= m_removed_at_front; i--, c++)
					m_children[i] = m_children[i - 1];
			}
			else
				m_children.erase(iter);

			deleted = true;
			return;
		}

		int childVisibleOffspring = (*iter)->GetChildrenCount(true);
		if ((*iter)->RemoveChild(pItem, destroy)) {
			visibleOffspring -= childVisibleOffspring - (*iter)->GetChildrenCount(true);
			if (!((*iter)->m_children.size() - (*iter)->m_removed_at_front)) {
				visibleOffspring -= 1;
				delete *iter;

				if (iter - m_children.begin() - m_removed_at_front <= 10) {
					++m_removed_at_front;
					unsigned int end = iter - m_children.begin();
					for (int i = end; i >= m_removed_at_front; i--)
						m_children[i] = m_children[i - 1];
				}
				else
					m_children.erase(iter);
			}

			deleted = true;
		}
	};

	if (forward) {
		for (auto iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter ) {
			doRemove(iter);
			if (deleted) {
				break;
			}
		}
	}
	else {
		for (auto iter = m_children.rbegin(); iter != m_children.rend() - m_removed_at_front; ++iter ) {
			doRemove(iter.base() - 1);
			if (deleted) {
				break;
			}
		}
	}

	if (!deleted)
		return false;

	if (GetType() == QueueItemType::Server) {
		static_cast<CServerItem*>(this)->m_visibleOffspring = visibleOffspring;
	}

	// Propagate new children count to parent
	CQueueItem* parent = GetParent();
	while (parent) {
		if (parent->GetType() == QueueItemType::Server) {
			static_cast<CServerItem*>(parent)->m_maxCachedIndex = -1;
			static_cast<CServerItem*>(parent)->m_visibleOffspring -= oldVisibleOffspring - visibleOffspring;
		}
		parent = parent->GetParent();
	}

	return true;
}

CQueueItem* CQueueItem::GetTopLevelItem()
{
	if (!m_parent)
		return this;

	CQueueItem* newParent = m_parent;
	CQueueItem* parent = 0;
	while (newParent) {
		parent = newParent;
		newParent = newParent->GetParent();
	}

	return parent;
}

const CQueueItem* CQueueItem::GetTopLevelItem() const
{
	if (!m_parent)
		return this;

	const CQueueItem* newParent = m_parent;
	const CQueueItem* parent = 0;
	while (newParent)
	{
		parent = newParent;
		newParent = newParent->GetParent();
	}

	return parent;
}

int CQueueItem::GetItemIndex() const
{
	const CQueueItem* pParent = GetParent();
	if (!pParent)
		return 0;

	int index = 1;
	for (std::vector<CQueueItem*>::const_iterator iter = pParent->m_children.begin() + pParent->m_removed_at_front; iter != pParent->m_children.end(); ++iter)
	{
		if (*iter == this)
			break;

		index += (*iter)->GetChildrenCount(true) + 1;
	}

	return index + pParent->GetItemIndex();
}

CFileItem::CFileItem(CServerItem* parent, bool queued, bool download,
					 std::wstring const& sourceFile, std::wstring const& targetFile,
					 CLocalPath const& localPath, CServerPath const& remotePath, int64_t size)
	: CQueueItem(parent)
	, m_sourceFile(sourceFile)
	, m_targetFile(targetFile.empty() ? fz::sparse_optional<std::wstring>() : fz::sparse_optional<std::wstring>(targetFile))
	, m_localPath(localPath)
	, m_remotePath(remotePath)
	, m_size(size)
{
	if (download)
		flags |= flag_download;
	if (queued)
		flags |= flag_queued;
}

CFileItem::~CFileItem()
{
}

void CFileItem::SetPriority(QueuePriority priority)
{
	if (priority == m_priority)
		return;

	if (m_parent)
	{
		CServerItem* parent = static_cast<CServerItem*>(m_parent);
		parent->SetChildPriority(this, m_priority, priority);
	}
	m_priority = priority;
}

void CFileItem::SetPriorityRaw(QueuePriority priority)
{
	m_priority = priority;
}

QueuePriority CFileItem::GetPriority() const
{
	return m_priority;
}

void CFileItem::SetActive(const bool active)
{
	if (active && !IsActive()) {
		wxASSERT(!GetChildrenCount(false));
		AddChild(new CStatusItem);
		flags |= flag_active;
	}
	else if (!active && IsActive()) {
		CQueueItem* pItem = GetChild(0, false);
		RemoveChild(pItem);
		flags &= ~flag_active;
	}
}

void CFileItem::SaveItem(pugi::xml_node& element) const
{
	if (m_edit != CEditHandler::none || !element) {
		return;
	}

	auto file = element.append_child("File");

	AddTextElement(file, "LocalFile", m_localPath.GetPath() + GetLocalFile());
	AddTextElement(file, "RemoteFile", GetRemoteFile());
	AddTextElement(file, "RemotePath", m_remotePath.GetSafePath());
	AddTextElementUtf8(file, "Download", Download() ? "1" : "0");
	if (m_size != -1) {
		AddTextElement(file, "Size", m_size);
	}
	if (m_errorCount) {
		AddTextElement(file, "ErrorCount", m_errorCount);
	}
	if (m_priority != QueuePriority::normal) {
		AddTextElement(file, "Priority", static_cast<int>(m_priority));
	}
	AddTextElementUtf8(file, "DataType", Ascii() ? "0" : "1");
	if (m_defaultFileExistsAction != CFileExistsNotification::unknown) {
		AddTextElement(file, "OverwriteAction", m_defaultFileExistsAction);
	}
}

bool CFileItem::TryRemoveAll()
{
	if (!IsActive()) {
		return true;
	}

	set_pending_remove(true);
	return false;
}

void CFileItem::SetTargetFile(wxString const& file)
{
	if (!file.empty() && file != m_sourceFile) {
		m_targetFile = fz::sparse_optional<std::wstring>(to_wstring(file));
	}
	else {
		m_targetFile.clear();
	}
}

void CFileItem::SetStatusMessage(CFileItem::Status status)
{
	m_status = status;
}

wxString const& CFileItem::GetStatusMessage() const
{
	static wxString statusTexts[] = {
		wxString(),
		_("Incorrect password"),
		_("Timeout"),
		_("Disconnecting from previous server"),
		_("Disconnected from server"),
		_("Connecting"),
		_("Connection attempt failed"),
		_("Interrupted by user"),
		_("Waiting for browsing connection"),
		_("Waiting for password"),
		_("Could not write to local file"),
		_("Could not start transfer"),
		_("Transferring"),
		_("Creating directory")
	};

	return statusTexts[m_status];
}

CFolderItem::CFolderItem(CServerItem* parent, bool queued, CLocalPath const& localPath)
	: CFileItem(parent, queued, true, std::wstring(), std::wstring(), localPath, CServerPath(), -1)
{
}

CFolderItem::CFolderItem(CServerItem* parent, bool queued, CServerPath const& remotePath, std::wstring const& remoteFile)
	: CFileItem(parent, queued, false, std::wstring(), remoteFile, CLocalPath(), remotePath, -1)
{
}

void CFolderItem::SaveItem(pugi::xml_node& element) const
{
	auto file = element.append_child("Folder");

	if (Download()) {
		AddTextElement(file, "LocalFile", GetLocalPath().GetPath() + GetLocalFile());
	}
	else {
		AddTextElement(file, "RemoteFile", GetRemoteFile());
		AddTextElement(file, "RemotePath", m_remotePath.GetSafePath());
	}
	AddTextElementUtf8(file, "Download", Download() ? "1" : "0");
}

void CFolderItem::SetActive(const bool active)
{
	if (active) {
		flags |= flag_active;
	}
	else {
		flags &= ~flag_active;
	}
}

CServerItem::CServerItem(const CServer& server)
	: m_activeCount(0)
	, m_server(server)
{
}

CServerItem::~CServerItem()
{
}

const CServer& CServerItem::GetServer() const
{
	return m_server;
}

wxString CServerItem::GetName() const
{
	return m_server.Format(ServerFormat::with_user_and_optional_port);
}

void CServerItem::AddChild(CQueueItem* pItem)
{
	CQueueItem::AddChild(pItem);
	m_maxCachedIndex = -1;
	m_visibleOffspring += 1 + pItem->GetChildrenCount(true);
	if (pItem->GetType() == QueueItemType::File ||
		pItem->GetType() == QueueItemType::Folder)
		AddFileItemToList((CFileItem*)pItem);

	wxASSERT(m_visibleOffspring >= static_cast<int>(m_children.size()) - m_removed_at_front);
	wxASSERT(((m_children.size() - m_removed_at_front) != 0) == (m_visibleOffspring != 0));
}

unsigned int CServerItem::GetChildrenCount(bool recursive) const
{
	if (!recursive)
		return m_children.size() - m_removed_at_front;

	return m_visibleOffspring;
}

void CServerItem::AddFileItemToList(CFileItem* pItem)
{
	if (!pItem)
		return;

	m_fileList[pItem->queued() ? 0 : 1][static_cast<int>(pItem->GetPriority())].push_back(pItem);
}

void CServerItem::RemoveFileItemFromList(CFileItem* pItem, bool forward)
{
	std::deque<CFileItem*>& fileList = m_fileList[pItem->queued() ? 0 : 1][static_cast<int>(pItem->GetPriority())];
	if (forward) {
		for (auto iter = fileList.begin(); iter != fileList.end(); ++iter) {
			if (*iter == pItem) {
				fileList.erase(iter);
				return;
			}
		}
	}
	else {
		for (auto iter = fileList.rbegin(); iter != fileList.rend(); ++iter) {
			if (*iter == pItem) {
				fileList.erase(iter.base() - 1);
				return;
			}
		}
	}
	wxFAIL_MSG(_T("File item not deleted from m_fileList"));
}

void CServerItem::SetDefaultFileExistsAction(CFileExistsNotification::OverwriteAction action, const TransferDirection direction)
{
	for (auto iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter) {
		CQueueItem *pItem = *iter;
		if (pItem->GetType() == QueueItemType::File) {
			CFileItem* pFileItem = ((CFileItem *)pItem);
			if (direction == TransferDirection::upload && pFileItem->Download())
				continue;
			else if (direction == TransferDirection::download && !pFileItem->Download())
				continue;
			pFileItem->m_defaultFileExistsAction = action;
		}
	}
}

CQueueItem* CServerItem::GetChild(unsigned int item, bool recursive)
{
	std::vector<CQueueItem*>::iterator iter;
	if (!recursive) {
		if (item + m_removed_at_front >= m_children.size())
			return 0;
		iter = m_children.begin();
		iter += item + m_removed_at_front;
		return *iter;
	}

	if ((int)item <= m_maxCachedIndex) {
		// Index is cached
		iter = m_children.begin() + m_removed_at_front;
		iter += m_lookupCache[item].child;
		item -= m_lookupCache[item].index;
		if (!item)
			return *iter;
		else
			return (*iter)->GetChild(item - 1);
	}

	int index;
	int child;
	iter = m_children.begin() + m_removed_at_front;
	if (m_maxCachedIndex == -1) {
		child = 0;
		index = 0;
	}
	else {
		// Start with loop with the last cached item index
		iter += m_lookupCache[m_maxCachedIndex].child + 1;
		item -= m_maxCachedIndex + 1;
		index = m_maxCachedIndex + 1;
		child = m_lookupCache[m_maxCachedIndex].child + 1;
	}

	for (; iter != m_children.end(); ++iter, ++child) {
		if (!item)
			return *iter;

		unsigned int count = (*iter)->GetChildrenCount(true);
		if (item > count) {
			if (m_maxCachedIndex == -1 && m_lookupCache.size() < (unsigned int)m_visibleOffspring)
				m_lookupCache.resize(m_visibleOffspring);
			for (unsigned int k = index; k <= index + count; ++k) {
				m_lookupCache[k].child = child;
				m_lookupCache[k].index = index;
			}
			m_maxCachedIndex = index + count;
			item -= count + 1;
			index += count + 1;
			continue;
		}

		return (*iter)->GetChild(item - 1);
	}
	return 0;
}

namespace {
CFileItem* DoGetIdleChild(std::deque<CFileItem*> const* fileList, TransferDirection direction)
{
	int i = 0;
	for (i = static_cast<int>(QueuePriority::count) - 1; i >= 0; --i) {
		for (auto const& item : fileList[i]) {
			if (item->IsActive())
				continue;

			if (direction == TransferDirection::both)
				return item;

			if (direction == TransferDirection::download)
			{
				if (item->Download())
					return item;
			}
			else if (!item->Download())
				return item;
		}
	}
	return 0;
}
}

CFileItem* CServerItem::GetIdleChild(bool immediateOnly, TransferDirection direction)
{
	CFileItem* item = DoGetIdleChild(m_fileList[1], direction);
	if( !item && !immediateOnly ) {
		item = DoGetIdleChild(m_fileList[0], direction);
	}
	return item;
}

bool CServerItem::RemoveChild(CQueueItem* pItem, bool destroy, bool forward)
{
	if (!pItem)
		return false;

	if (pItem->GetType() == QueueItemType::File || pItem->GetType() == QueueItemType::Folder) {
		CFileItem* pFileItem = static_cast<CFileItem*>(pItem);
		RemoveFileItemFromList(pFileItem, forward);
	}

	bool removed = CQueueItem::RemoveChild(pItem, destroy, forward);
	if (removed) {
		m_maxCachedIndex = -1;
	}

	wxASSERT(m_visibleOffspring >= static_cast<int>(m_children.size()) - m_removed_at_front);
	wxASSERT(((m_children.size() - m_removed_at_front) != 0) == (m_visibleOffspring != 0));

	return removed;
}

void CServerItem::QueueImmediateFiles()
{
	for (int i = 0; i < static_cast<int>(QueuePriority::count); ++i) {
		std::deque<CFileItem*> activeList;
		std::deque<CFileItem*>& fileList = m_fileList[1][i];
		for (auto iter = fileList.rbegin(); iter != fileList.rend(); ++iter) {
			CFileItem* item = *iter;
			wxASSERT(!item->queued());
			if (item->IsActive())
				activeList.push_front(item);
			else {
				item->set_queued(true);
				m_fileList[0][i].push_front(item);
			}
		}
		std::swap(fileList, activeList);
	}
}

void CServerItem::QueueImmediateFile(CFileItem* pItem)
{
	if (pItem->queued())
		return;

	std::deque<CFileItem*>& fileList = m_fileList[1][static_cast<int>(pItem->GetPriority())];
	for (auto iter = fileList.begin(); iter != fileList.end(); ++iter) {
		if (*iter != pItem)
			continue;

		pItem->set_queued(true);
		fileList.erase(iter);
		m_fileList[0][static_cast<int>(pItem->GetPriority())].push_front(pItem);
		return;
	}
	wxASSERT(false);
}

void CServerItem::SaveItem(pugi::xml_node& element) const
{
	auto server = element.append_child("Server");
	SetServer(server, m_server);

	for (auto iter = m_children.cbegin() + m_removed_at_front; iter != m_children.cend(); ++iter)
		(*iter)->SaveItem(server);
}

int64_t CServerItem::GetTotalSize(int& filesWithUnknownSize, int& queuedFiles) const
{
	int64_t totalSize = 0;
	for (int i = 0; i < static_cast<int>(QueuePriority::count); ++i) {
		for (int j = 0; j < 2; ++j) {
			const std::deque<CFileItem*>& fileList = m_fileList[j][i];
			for (auto const& item : fileList) {
				int64_t size = item->GetSize();
				if (size >= 0)
					totalSize += size;
				else
					filesWithUnknownSize++;
			}
		}
	}

	for (std::vector<CQueueItem*>::const_iterator iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter) {
		if ((*iter)->GetType() == QueueItemType::File ||
			(*iter)->GetType() == QueueItemType::Folder)
			queuedFiles++;
	}

	return totalSize;
}

bool CServerItem::TryRemoveAll()
{
	wxASSERT(!GetParent());

	const int oldVisibleOffspring = m_visibleOffspring;
	std::vector<CQueueItem*>::iterator iter;
	std::vector<CQueueItem*> keepChildren;
	m_visibleOffspring = 0;
	for (iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter) {
		CQueueItem* pItem = *iter;
		if (pItem->TryRemoveAll()) {
			if (pItem->GetType() == QueueItemType::File || pItem->GetType() == QueueItemType::Folder) {
				CFileItem* pFileItem = static_cast<CFileItem*>(pItem);
				RemoveFileItemFromList(pFileItem, true);
			}
			delete pItem;
		}
		else {
			keepChildren.push_back(pItem);
			m_visibleOffspring++;
			m_visibleOffspring += pItem->GetChildrenCount(true);
		}
	}
	std::swap(m_children, keepChildren);
	m_removed_at_front = 0;

	m_maxCachedIndex = -1;

	wxASSERT(oldVisibleOffspring >= m_visibleOffspring);
	wxASSERT(m_visibleOffspring >= static_cast<int>(m_children.size()));
	(void)oldVisibleOffspring;

	return m_children.empty();
}

void CServerItem::DetachChildren()
{
	wxASSERT(!m_activeCount);

	m_children.clear();
	m_visibleOffspring = 0;
	m_maxCachedIndex = -1;
	m_removed_at_front = 0;

	for (int i = 0; i < 2; ++i)
		for (int j = 0; j < static_cast<int>(QueuePriority::count); j++)
			m_fileList[i][j].clear();
}

void CServerItem::SetPriority(QueuePriority priority)
{
	std::vector<CQueueItem*>::iterator iter;
	for (iter = m_children.begin() + m_removed_at_front; iter != m_children.end(); ++iter) {
		if ((*iter)->GetType() == QueueItemType::File)
			((CFileItem*)(*iter))->SetPriorityRaw(priority);
		else
			(*iter)->SetPriority(priority);
	}

	for (int i = 0; i < 2; ++i)
		for (int j = 0; j < static_cast<int>(QueuePriority::count); ++j) {
			if (j != static_cast<int>(priority)) {
				std::move(m_fileList[i][j].begin(), m_fileList[i][j].end(), std::back_inserter(m_fileList[i][static_cast<int>(priority)]));
				m_fileList[i][j].clear();
			}
		}
}

void CServerItem::SetChildPriority(CFileItem* pItem, QueuePriority oldPriority, QueuePriority newPriority)
{
	int i = pItem->queued() ? 0 : 1;

	for (auto iter = m_fileList[i][static_cast<int>(oldPriority)].begin(); iter != m_fileList[i][static_cast<int>(oldPriority)].end(); ++iter) {
		if (*iter != pItem)
			continue;

		m_fileList[i][static_cast<int>(oldPriority)].erase(iter);
		m_fileList[i][static_cast<int>(newPriority)].push_back(pItem);
		return;
	}

	wxFAIL;
}

// --------------
// CQueueViewBase
// --------------

BEGIN_EVENT_TABLE(CQueueViewBase, wxListCtrlEx)
EVT_ERASE_BACKGROUND(CQueueViewBase::OnEraseBackground)
EVT_CHAR(CQueueViewBase::OnChar)
EVT_LIST_COL_END_DRAG(wxID_ANY, CQueueViewBase::OnEndColumnDrag)
EVT_TIMER(wxID_ANY, CQueueViewBase::OnTimer)
EVT_KEY_DOWN(CQueueViewBase::OnKeyDown)
EVT_MENU(XRCID("ID_EXPORT"), CQueueViewBase::OnExport)
END_EVENT_TABLE()

CQueueViewBase::CQueueViewBase(CQueue* parent, int index, const wxString& title)
	: wxListCtrlEx(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN | wxLC_REPORT | wxLC_VIRTUAL | wxSUNKEN_BORDER | wxTAB_TRAVERSAL)
	, m_pageIndex(index)
	, m_title(title)
{
	m_pQueue = parent;

	// Create and assign the image list for the queue
	wxSize s = CThemeProvider::GetIconSize(iconSizeSmall);
	wxImageList* pImageList = new wxImageList(s.x, s.y);

	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_SERVER"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall)));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_FILE"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall)));
	pImageList->Add(CThemeProvider::Get()->CreateBitmap(_T("ART_FOLDER"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall)));

	AssignImageList(pImageList, wxIMAGE_LIST_SMALL);

	m_filecount_delay_timer.SetOwner(this);
}

CQueueViewBase::~CQueueViewBase()
{
	for (auto server : m_serverList) {
		delete server;
	}
}

CQueueItem* CQueueViewBase::GetQueueItem(unsigned int item) const
{
	for (auto iter = m_serverList.cbegin(); iter != m_serverList.cend(); ++iter) {
		if (!item)
			return *iter;

		unsigned int count = (*iter)->GetChildrenCount(true);
		if (item > count) {
			item -= count + 1;
			continue;
		}

		return (*iter)->GetChild(item - 1);
	}
	return 0;
}

int CQueueViewBase::GetItemIndex(const CQueueItem* item)
{
	const CQueueItem* pTopLevelItem = item->GetTopLevelItem();

	int index = 0;
	for (std::vector<CServerItem*>::const_iterator iter = m_serverList.begin(); iter != m_serverList.end(); ++iter)
	{
		if (pTopLevelItem == *iter)
			break;

		index += (*iter)->GetChildrenCount(true) + 1;
	}

	return index + item->GetItemIndex();
}

void CQueueViewBase::OnEraseBackground(wxEraseEvent& event)
{
	if (m_allowBackgroundErase)
		event.Skip();
}

wxString CQueueViewBase::OnGetItemText(long item, long column) const
{
	if (column < 0 || static_cast<size_t>(column) >= m_columns.size())
		return wxString();

	CQueueViewBase* pThis = const_cast<CQueueViewBase*>(this);

	CQueueItem* pItem = pThis->GetQueueItem(item);
	if (!pItem)
		return wxString();

	return OnGetItemText(pItem, m_columns[column]);
}

wxString CQueueViewBase::OnGetItemText(CQueueItem* pItem, ColumnId column) const
{
	switch (pItem->GetType())
	{
	case QueueItemType::Server:
		{
			CServerItem* pServerItem = static_cast<CServerItem*>(pItem);
			if (!column)
				return pServerItem->GetName();
		}
		break;
	case QueueItemType::File:
		{
			CFileItem* pFileItem = static_cast<CFileItem*>(pItem);
			switch (column)
			{
			case colLocalName:
				return _T("  ") + pFileItem->GetLocalPath().GetPath() + pFileItem->GetLocalFile();
			case colDirection:
				if (pFileItem->Download())
					if (pFileItem->queued())
						return _T("<--");
					else
						return _T("<<--");
				else
					if (pFileItem->queued())
						return _T("-->");
					else
						return _T("-->>");
				break;
			case colRemoteName:
				return pFileItem->GetRemotePath().FormatFilename(pFileItem->GetRemoteFile());
			case colSize:
				{
					auto const& size = pFileItem->GetSize();
					if (size >= 0)
						return CSizeFormat::Format(size);
					else
						return _T("?");
				}
			case colPriority:
				switch (pFileItem->GetPriority())
				{
				case QueuePriority::lowest:
					return _("Lowest");
				case QueuePriority::low:
					return _("Low");
				default:
				case QueuePriority::normal:
					return _("Normal");
				case QueuePriority::high:
					return _("High");
				case QueuePriority::highest:
					return _("Highest");
				}
				break;
			case colTransferStatus:
			case colErrorReason:
				return pFileItem->GetStatusMessage();
			case colTime:
				return CTimeFormat::FormatDateTime(pItem->GetTime());
			default:
				break;
			}
		}
		break;
	case QueueItemType::Folder:
		{
			CFileItem* pFolderItem = static_cast<CFolderItem*>(pItem);
			switch (column)
			{
			case colLocalName:
				if (pFolderItem->Download())
					return _T("  ") + pFolderItem->GetLocalPath().GetPath() + pFolderItem->GetLocalFile();
				break;
			case colDirection:
				if (pFolderItem->Download())
					if (pFolderItem->queued())
						return _T("<--");
					else
						return _T("<<--");
				else
					if (pFolderItem->queued())
						return _T("-->");
					else
						return _T("-->>");
				break;
			case colRemoteName:
				if (!pFolderItem->Download())
				{
					if (pFolderItem->GetRemoteFile().empty())
						return pFolderItem->GetRemotePath().GetPath();
					else
						return pFolderItem->GetRemotePath().FormatFilename(pFolderItem->GetRemoteFile());
				}
				break;
			case colPriority:
				switch (pFolderItem->GetPriority())
				{
				case QueuePriority::lowest:
					return _("Lowest");
				case QueuePriority::low:
					return _("Low");
				default:
				case QueuePriority::normal:
					return _("Normal");
				case QueuePriority::high:
					return _("High");
				case QueuePriority::highest:
					return _("Highest");
				}
				break;
			case colTransferStatus:
			case colErrorReason:
				return pFolderItem->GetStatusMessage();
			case colTime:
				return CTimeFormat::FormatDateTime(pItem->GetTime());
			default:
				break;
			}
		}
		break;
	default:
		break;
	}

	return wxString();
}

int CQueueViewBase::OnGetItemImage(long item) const
{
	CQueueViewBase* pThis = const_cast<CQueueViewBase*>(this);

	CQueueItem* pItem = pThis->GetQueueItem(item);
	if (!pItem)
		return -1;

	switch (pItem->GetType())
	{
	case QueueItemType::Server:
		return 0;
	case QueueItemType::File:
		return 1;
	case QueueItemType::Folder:
		return 2;
	default:
		return -1;
	}

	return -1;
}

void CQueueViewBase::UpdateSelections_ItemAdded(int added)
{
	// This is the fastest algorithm I can think of to move all
	// selections. Though worst case is still O(n), as with every algorithm to
	// move selections.

#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	const int selection_count = GetSelectedItemCount();
	if (!selection_count) {
		return;
	}
#endif

	// Go through all items, keep record of the previous selected item
	int item = GetNextItem(added - 1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	int prevItem = -1;
	while (item != -1) {
		if (prevItem != -1) {
			if (prevItem + 1 != item) {
				// Previous selected item was not the direct predecessor
				// That means we have to select the successor of prevItem
				// and unselect current item
				SetItemState(prevItem + 1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
				SetItemState(item, 0, wxLIST_STATE_SELECTED);
			}
		}
		else {
			// First selected item, no predecessor yet. We have to unselect
			SetItemState(item, 0, wxLIST_STATE_SELECTED);
		}
		prevItem = item;

		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	if (prevItem != -1 && prevItem < m_itemCount - 1) {
		// Move the very last selected item
		SetItemState(prevItem + 1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}

	SetItemState(added, 0, wxLIST_STATE_SELECTED);
}

void CQueueViewBase::UpdateSelections_ItemRangeAdded(int added, int count)
{
	wxASSERT(GetItemCount() == m_itemCount);
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	const int selection_count = GetSelectedItemCount();
	if (!selection_count) {
		return;
	}
#endif

	std::deque<int> itemsToSelect;

	// Go through all selected items
	int item = GetNextItem(added - 1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	while (item != -1) {
		// Select new items preceding to current one
		while (!itemsToSelect.empty() && itemsToSelect.front() < item) {
			SetItemState(itemsToSelect.front(), wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
			itemsToSelect.pop_front();
		}
		if (itemsToSelect.front() == item) {
			itemsToSelect.pop_front();
		}
		else {
			SetItemState(item, 0, wxLIST_STATE_SELECTED);
		}

		if (item + count < GetItemCount()) {
			// On generic list controls, new items may be selected by default after
			// increasing the item count: Internally it sometimes keeps track
			// of only unselected items.
			itemsToSelect.push_back(item + count);
		}

		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	for (auto const& sel : itemsToSelect) {
		SetItemState(sel, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}
}

void CQueueViewBase::UpdateSelections_ItemRemoved(int removed)
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	const int selection_count = GetSelectedItemCount();
	if (!selection_count)
		return;
#endif

	SetItemState(removed, 0, wxLIST_STATE_SELECTED);

	int item = GetNextItem(removed - 1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	int prevItem = -1;
	while (item != -1)
	{
		if (prevItem != -1)
		{
			if (prevItem + 1 != item)
			{
				// Previous selected item was not the direct predecessor
				// That means we have to select our predecessor and unselect
				// prevItem
				SetItemState(prevItem, 0, wxLIST_STATE_SELECTED);
				SetItemState(item - 1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
			}
		}
		else
		{
			// First selected item, no predecessor yet. We have to unselect
			SetItemState(item - 1, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		}
		prevItem = item;

		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	if (prevItem != -1)
	{
		SetItemState(prevItem, 0, wxLIST_STATE_SELECTED);
	}
}

void CQueueViewBase::UpdateSelections_ItemRangeRemoved(int removed, int count)
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	const int selection_count = GetSelectedItemCount();
	if (!selection_count)
		return;
#endif

	SetItemState(removed, 0, wxLIST_STATE_SELECTED);

	std::deque<int> itemsToUnselect;

	int item = GetNextItem(removed - 1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	while (item != -1) {
		// Unselect new items preceding to current one
		while (!itemsToUnselect.empty() && itemsToUnselect.front() < item - count) {
			SetItemState(itemsToUnselect.front(), 0, wxLIST_STATE_SELECTED);
			itemsToUnselect.pop_front();
		}

		if (itemsToUnselect.empty())
			SetItemState(item - count, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		else if (itemsToUnselect.front() == item - count)
			itemsToUnselect.pop_front();
		else
			SetItemState(item - count, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);

		itemsToUnselect.push_back(item);

		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	for (auto const& unsel : itemsToUnselect) {
		SetItemState(unsel, 0, wxLIST_STATE_SELECTED);
	}
}

void CQueueViewBase::AddQueueColumn(ColumnId id)
{
	const unsigned long widths[8] = { 180, 60, 180, 80, 60, 100, 150, 150 };
	const int alignment[8] = { wxLIST_FORMAT_LEFT, wxLIST_FORMAT_CENTER, wxLIST_FORMAT_LEFT, wxLIST_FORMAT_RIGHT, wxLIST_FORMAT_LEFT, wxLIST_FORMAT_LEFT, wxLIST_FORMAT_LEFT, wxLIST_FORMAT_LEFT };
	const wxString names[8] = { _("Server/Local file"), _("Direction"), _("Remote file"), _("Size"), _("Priority"), _("Time"), _("Status"), _("Reason") };

	AddColumn(names[id], alignment[id], widths[id]);
	m_columns.push_back(id);
}

void CQueueViewBase::CreateColumns(std::list<ColumnId> const& extraColumns)
{
	AddQueueColumn(colLocalName);
	AddQueueColumn(colDirection);
	AddQueueColumn(colRemoteName);
	AddQueueColumn(colSize);
	AddQueueColumn(colPriority);

	for( std::list<ColumnId>::const_iterator it = extraColumns.begin(); it != extraColumns.end(); ++it)
		AddQueueColumn(*it);

	LoadColumnSettings(OPTION_QUEUE_COLUMN_WIDTHS, -1, -1);
}

CServerItem* CQueueViewBase::GetServerItem(const CServer& server)
{
	for (auto iter = m_serverList.begin(); iter != m_serverList.end(); ++iter)
	{
		if ((*iter)->GetServer() == server)
			return *iter;
	}
	return NULL;
}

CServerItem* CQueueViewBase::CreateServerItem(const CServer& server)
{
	CServerItem* pItem = GetServerItem(server);

	if (!pItem) {
		pItem = new CServerItem(server);
		m_serverList.push_back(pItem);
		++m_itemCount;

		wxASSERT(m_insertionStart == -1);
		wxASSERT(m_insertionCount == 0);

		m_insertionStart = GetItemIndex(pItem);
		m_insertionCount = 1;
	}

	return pItem;
}

void CQueueViewBase::CommitChanges()
{
	SaveSetItemCount(m_itemCount);

	if (m_insertionStart != -1) {
		wxASSERT(m_insertionCount != 0);
		if (m_insertionCount == 1) {
			UpdateSelections_ItemAdded(m_insertionStart);
		}
		else {
			UpdateSelections_ItemRangeAdded(m_insertionStart, m_insertionCount);
		}

		m_insertionStart = -1;
		m_insertionCount = 0;
	}

	if (m_fileCountChanged) {
		DisplayNumberQueuedFiles();
	}
}

void CQueueViewBase::DisplayNumberQueuedFiles()
{
	if (m_filecount_delay_timer.IsRunning()) {
		m_fileCountChanged = true;
		return;
	}

	wxString str;
	if (m_fileCount > 0) {
		str.Printf(m_title + _T(" (%d)"), m_fileCount);
	}
	else {
		str = m_title;
	}
	m_pQueue->SetPageText(m_pageIndex, str);

	m_fileCountChanged = false;

	m_filecount_delay_timer.Start(200, true);
}

void CQueueViewBase::InsertItem(CServerItem* pServerItem, CQueueItem* pItem)
{
	const int newIndex = GetItemIndex(pServerItem) + pServerItem->GetChildrenCount(true) + 1;

	pServerItem->AddChild(pItem);
	m_itemCount++;

	if (m_insertionStart == -1) {
		assert(!m_insertionCount);
		m_insertionStart = newIndex;
	}
	++m_insertionCount;

	if (pItem->GetType() == QueueItemType::File || pItem->GetType() == QueueItemType::Folder) {
		m_fileCount++;
		m_fileCountChanged = true;
	}
}

bool CQueueViewBase::RemoveItem(CQueueItem* pItem, bool destroy, bool updateItemCount, bool updateSelections, bool forward)
{
	if (pItem->GetType() == QueueItemType::File || pItem->GetType() == QueueItemType::Folder) {
		wxASSERT(m_fileCount > 0);
		m_fileCount--;
		m_fileCountChanged = true;
	}

	int index = 0;
	if (updateSelections)
		index = GetItemIndex(pItem);

	CQueueItem* topLevelItem = pItem->GetTopLevelItem();

	int count = topLevelItem->GetChildrenCount(true);
	topLevelItem->RemoveChild(pItem, destroy, forward);

	bool didRemoveParent;

	int oldCount = m_itemCount;
	if (!topLevelItem->GetChild(0)) {
		std::vector<CServerItem*>::iterator iter;
		for (iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
			if (*iter == topLevelItem)
				break;
		}
		if (iter != m_serverList.end())
			m_serverList.erase(iter);

		UpdateSelections_ItemRangeRemoved(GetItemIndex(topLevelItem), count + 1);

		delete topLevelItem;

		m_itemCount -= count + 1;
		if (updateItemCount)
			SaveSetItemCount(m_itemCount);

		didRemoveParent = true;
	}
	else {
		count -= topLevelItem->GetChildrenCount(true);

		if (updateSelections)
			UpdateSelections_ItemRangeRemoved(index, count);

		m_itemCount -= count;
		if (updateItemCount)
			SaveSetItemCount(m_itemCount);

		didRemoveParent = false;
	}

	if (updateItemCount) {
		if (m_fileCountChanged)
			DisplayNumberQueuedFiles();
		if (oldCount > m_itemCount)
		{
			bool eraseBackground = GetTopItem() + GetCountPerPage() + 1 >= m_itemCount;
			RefreshListOnly(eraseBackground);
			if (eraseBackground)
				Update();
		}
	}

	return didRemoveParent;
}

void CQueueViewBase::RefreshItem(const CQueueItem* pItem)
{
	wxASSERT(pItem);
	int index = GetItemIndex(pItem);

#ifdef __WXMSW__
	wxRect rect;
	GetItemRect(index, rect);
	RefreshRect(rect, false);
#else
	wxListCtrl::RefreshItem(index);
#endif
}

void CQueueViewBase::OnChar(wxKeyEvent& event)
{
	const int code = event.GetKeyCode();
	if (code != WXK_LEFT && code != WXK_RIGHT) {
		event.Skip();
		return;
	}

	bool forward;
	if (GetLayoutDirection() != wxLayout_RightToLeft)
		forward = code == WXK_RIGHT;
	else
		forward = code == WXK_LEFT;

	int selection = m_pQueue->GetSelection();
	if (selection > 0 && !forward)
		selection--;
	else if (selection < (int)m_pQueue->GetPageCount() - 1 && forward)
		selection++;
	else
		return;

	m_pQueue->SetSelection(selection);
}

void CQueueViewBase::OnEndColumnDrag(wxListEvent&)
{
	for (unsigned int i = 0; i < m_pQueue->GetPageCount(); ++i) {
		wxWindow *page = m_pQueue->GetPage(i);

		wxListCtrl* queue_page = dynamic_cast<wxListCtrl*>(page);
		if (!queue_page || queue_page == this)
			continue;

		for (int col = 0; col < wxMin(GetColumnCount(), queue_page->GetColumnCount()); col++)
			queue_page->SetColumnWidth(col, GetColumnWidth(col));
	}
}

void CQueueViewBase::OnTimer(wxTimerEvent& event)
{
	if (event.GetId() != m_filecount_delay_timer.GetId()) {
		event.Skip();
		return;
	}

	if (m_fileCountChanged) {
		DisplayNumberQueuedFiles();
	}
}

void CQueueViewBase::OnKeyDown(wxKeyEvent& event)
{
	const int code = event.GetKeyCode();
	const int mods = event.GetModifiers();
	if (code == 'A' && (mods == wxMOD_CMD || mods == (wxMOD_CONTROL | wxMOD_META))) {
		for (unsigned int i = 0; i < (unsigned int)GetItemCount(); i++)
			SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}
	else
		event.Skip();
}

void CQueueViewBase::WriteToFile(pugi::xml_node element) const
{
	auto queue = element.child("Queue");
	if (!queue) {
		queue = element.append_child("Queue");
	}

	for (std::vector<CServerItem*>::const_iterator iter = m_serverList.begin(); iter != m_serverList.end(); ++iter)
		(*iter)->SaveItem(queue);
}

void CQueueViewBase::OnExport(wxCommandEvent&)
{
	wxFileDialog dlg(m_parent, _("Select file for exported queue"), wxString(),
		_T("FileZilla.xml"), _T("XML files (*.xml)|*.xml"),
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK)
		return;

	CXmlFile xml(dlg.GetPath().ToStdWstring());

	auto exportRoot = xml.CreateEmpty();

	WriteToFile(exportRoot);

	xml.Save(true);
}

// ------
// CQueue
// ------

CQueue::CQueue(wxWindow* parent, CMainFrame *pMainFrame, CAsyncRequestQueue *pAsyncRequestQueue)
{
	Create(parent, -1, wxDefaultPosition, wxDefaultSize, wxNO_BORDER | wxAUI_NB_BOTTOM);
	SetExArtProvider();

	m_pQueueView = new CQueueView(this, 0, pMainFrame, pAsyncRequestQueue);
	AddPage(m_pQueueView, m_pQueueView->GetTitle());

	m_pQueueView_Failed = new CQueueViewFailed(this, 1);
	AddPage(m_pQueueView_Failed, m_pQueueView_Failed->GetTitle());
	m_pQueueView_Successful = new CQueueViewSuccessful(this, 2);
	AddPage(m_pQueueView_Successful, m_pQueueView_Successful->GetTitle());

	RemoveExtraBorders();

	m_pQueueView->LoadQueue();
}

void CQueue::SetFocus()
{
	GetPage(GetSelection())->SetFocus();
}
