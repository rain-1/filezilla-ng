#include <filezilla.h>
#include "remote_recursive_operation.h"
#include "commandqueue.h"
#include "chmoddialog.h"
#include "filter.h"
#include "Options.h"
#include "queue.h"

#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/recursive_remove.hpp>

recursion_root::recursion_root(CServerPath const& start_dir, bool allow_parent)
	: m_remoteStartDir(start_dir)
	, m_allowParent(allow_parent)
{
	wxASSERT_MSG(!start_dir.empty(), _T("Empty startDir in recursion_root constructor"));
}

void recursion_root::add_dir_to_visit(CServerPath const& path, std::wstring const& subdir, CLocalPath const& localDir, bool is_link)
{
	new_dir dirToVisit;

	dirToVisit.localDir = localDir;
	dirToVisit.parent = path;
	dirToVisit.subdir = subdir;
	dirToVisit.link = is_link ? 2 : 0;
	m_dirsToVisit.push_back(dirToVisit);
}

void recursion_root::add_dir_to_visit_restricted(CServerPath const& path, std::wstring const& restrict, bool recurse)
{
	new_dir dirToVisit;
	dirToVisit.parent = path;
	dirToVisit.recurse = recurse;
	if (!restrict.empty()) {
		dirToVisit.restrict = fz::sparse_optional<std::wstring>(restrict);
	}
	m_dirsToVisit.push_back(dirToVisit);
}

CRemoteRecursiveOperation::CRemoteRecursiveOperation(CState &state)
	: CRecursiveOperation(state)
{
	state.RegisterHandler(this, STATECHANGE_REMOTE_DIR_OTHER);
	state.RegisterHandler(this, STATECHANGE_REMOTE_LINKNOTDIR);
}

CRemoteRecursiveOperation::~CRemoteRecursiveOperation()
{
	if (m_pChmodDlg) {
		m_pChmodDlg->Destroy();
		m_pChmodDlg = 0;
	}
}

void CRemoteRecursiveOperation::OnStateChange(t_statechange_notifications notification, const wxString&, const void* data2)
{
	if (notification == STATECHANGE_REMOTE_DIR_OTHER && data2) {
		std::shared_ptr<CDirectoryListing> const& listing = *reinterpret_cast<std::shared_ptr<CDirectoryListing> const*>(data2);
		ProcessDirectoryListing(listing.get());
	}
	else if (notification == STATECHANGE_REMOTE_LINKNOTDIR) {
		wxASSERT(data2);
		LinkIsNotDir();
	}
}

void CRemoteRecursiveOperation::AddRecursionRoot(recursion_root && root)
{
	if (!root.empty()) {
		recursion_roots_.push_back(std::forward<recursion_root>(root));
	}
}

void CRemoteRecursiveOperation::StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, CServerPath const& finalDir, bool immediate)
{
	wxCHECK_RET(m_operationMode == recursive_none, _T("StartRecursiveOperation called with m_operationMode != recursive_none"));
	wxCHECK_RET(m_state.IsRemoteConnected(), _T("StartRecursiveOperation while disconnected"));
	wxCHECK_RET(!finalDir.empty(), _T("Empty final dir in recursive operation"));

	if (mode == recursive_chmod && !m_pChmodDlg) {
		return;
	}

	if ((mode == recursive_transfer || mode == recursive_transfer_flatten) && !m_pQueue) {
		return;
	}

	if (recursion_roots_.empty()) {
		// Nothing to do in this case
		return;
	}

	m_processedFiles = 0;
	m_processedDirectories = 0;

	m_immediate = immediate;
	m_operationMode = mode;

	if ((mode == CRecursiveOperation::recursive_transfer || mode == CRecursiveOperation::recursive_transfer_flatten) && immediate) {
		m_actionAfterBlocker = m_pQueue->GetActionAfterBlocker();
	}

	m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
	m_state.NotifyHandlers(STATECHANGE_REMOTE_RECURSION_STATUS);

	m_filters = filters;

	NextOperation();
}

bool CRemoteRecursiveOperation::NextOperation()
{
	if (m_operationMode == recursive_none)
		return false;

	while (!recursion_roots_.empty()) {
		auto & root = recursion_roots_.front();
		while (!root.m_dirsToVisit.empty()) {
			const recursion_root::new_dir& dirToVisit = root.m_dirsToVisit.front();
			if (m_operationMode == recursive_delete && !dirToVisit.doVisit) {
				m_state.m_pCommandQueue->ProcessCommand(new CRemoveDirCommand(dirToVisit.parent, dirToVisit.subdir), CCommandQueue::recursiveOperation);
				root.m_dirsToVisit.pop_front();
				continue;
			}

			CListCommand* cmd = new CListCommand(dirToVisit.parent, dirToVisit.subdir, dirToVisit.link ? LIST_FLAG_LINK : 0);
			m_state.m_pCommandQueue->ProcessCommand(cmd, CCommandQueue::recursiveOperation);
			return true;
		}

		recursion_roots_.pop_front();
	}

	if (m_operationMode == recursive_delete && !m_finalDir.empty()) {
		// After a deletion we cannot refresh if inside the deleted directories. Navigate user out if it
		auto curPath = m_state.GetRemotePath();
		if (!curPath.empty() && (curPath == m_finalDir || m_finalDir.IsParentOf(curPath, false))) {
			StopRecursiveOperation();
			m_state.ChangeRemoteDir(m_finalDir, std::wstring(), LIST_FLAG_REFRESH);
			return false;
		}
	}

	StopRecursiveOperation();
	m_state.RefreshRemote();
	return false;
}

bool CRemoteRecursiveOperation::BelowRecursionRoot(const CServerPath& path, recursion_root::new_dir &dir)
{
	if (!dir.start_dir.empty()) {
		if (path.IsSubdirOf(dir.start_dir, false))
			return true;
		else
			return false;
	}

	auto & root = recursion_roots_.front();
	if (path.IsSubdirOf(root.m_remoteStartDir, false))
		return true;

	// In some cases (chmod from tree for example) it is neccessary to list the
	// parent first
	if (path == root.m_remoteStartDir && root.m_allowParent)
		return true;

	if (dir.link == 2) {
		dir.start_dir = path;
		return true;
	}

	return false;
}

// Defined in RemoteListView.cpp
std::wstring StripVMSRevision(std::wstring const& name);

void CRemoteRecursiveOperation::ProcessDirectoryListing(const CDirectoryListing* pDirectoryListing)
{
	if (!pDirectoryListing) {
		StopRecursiveOperation();
		return;
	}

	if (m_operationMode == recursive_none || recursion_roots_.empty())
		return;

	if (pDirectoryListing->failed()) {
		// Ignore this.
		// It will get handled by the failed command in ListingFailed
		return;
	}

	auto & root = recursion_roots_.front();
	wxASSERT(!root.m_dirsToVisit.empty());

	if (!m_state.IsRemoteConnected() || root.m_dirsToVisit.empty()) {
		StopRecursiveOperation();
		return;
	}

	recursion_root::new_dir dir = root.m_dirsToVisit.front();
	root.m_dirsToVisit.pop_front();

	if (!BelowRecursionRoot(pDirectoryListing->path, dir)) {
		NextOperation();
		return;
	}

	if (m_operationMode == recursive_delete && dir.doVisit && !dir.subdir.empty()) {
		// After recursing into directory to delete its contents, delete directory itself
		// Gets handled in NextOperation
		recursion_root::new_dir dir2 = dir;
		dir2.doVisit = false;
		root.m_dirsToVisit.push_front(dir2);
	}

	if (dir.link && !dir.recurse) {
		NextOperation();
		return;
	}

	// Check if we have already visited the directory
	if (!root.m_visitedDirs.insert(pDirectoryListing->path).second) {
		NextOperation();
		return;
	}

	++m_processedDirectories;

	const CServer* pServer = m_state.GetServer();
	wxASSERT(pServer);

	if (!pDirectoryListing->GetCount() && m_operationMode == recursive_transfer) {
		if (m_immediate) {
			wxFileName::Mkdir(dir.localDir.GetPath(), 0777, wxPATH_MKDIR_FULL);
			m_state.RefreshLocalFile(dir.localDir.GetPath());
		}
		else {
			m_pQueue->QueueFile(true, true, _T(""), _T(""), dir.localDir, CServerPath(), *pServer, -1);
			m_pQueue->QueueFile_Finish(false);
		}
	}

	CFilterManager filter;

	// Is operation restricted to a single child?
	bool const restrict = static_cast<bool>(dir.restrict);

	std::deque<std::wstring> filesToDelete;

	std::wstring const remotePath = pDirectoryListing->path.GetPath();

	if (m_operationMode == recursive_synchronize_download && !dir.localDir.empty()) {
		// Step one in synchronization: Delete local files not on the server
		fz::local_filesys fs;
		if (fs.begin_find_files(fz::to_native(dir.localDir.GetPath()))) {
			std::list<fz::native_string> paths_to_delete;

			bool isLink{};
			fz::native_string name;
			bool isDir{};
			int64_t size{};
			fz::datetime time;
			int attributes{};
			while (fs.get_next_file(name, isLink, isDir, &size, &time, &attributes)) {
				if (isLink) {
					continue;
				}
				auto const wname = fz::to_wstring(name);
				if (filter.FilenameFiltered(m_filters.first, wname, dir.localDir.GetPath(), isDir, size, attributes, time)) {
					continue;
				}

				// Local item isn't filtered

				int remoteIndex = pDirectoryListing->FindFile_CmpCase(fz::to_wstring(name));
				if (remoteIndex != -1) {
					CDirentry const& entry = (*pDirectoryListing)[remoteIndex];
					if (!filter.FilenameFiltered(m_filters.second, entry.name, remotePath, entry.is_dir(), entry.size, 0, entry.time)) {
						// Both local and remote items exist

						if (isDir == entry.is_dir() || entry.is_link()) {
							// Normal item, nothing we should do
							continue;
						}
					}
				}

				// Local item should be deleted if reaching this point
				paths_to_delete.push_back(fz::to_native(dir.localDir.GetPath()) + name);
			}

			fz::recursive_remove r;
			r.remove(paths_to_delete);
		}
	}

	bool added = false;

	for (int i = pDirectoryListing->GetCount() - 1; i >= 0; --i) {
		const CDirentry& entry = (*pDirectoryListing)[i];

		if (restrict) {
			if (entry.name != *dir.restrict)
				continue;
		}
		else if (filter.FilenameFiltered(m_filters.second, entry.name, remotePath, entry.is_dir(), entry.size, 0, entry.time))
			continue;

		if (!entry.is_dir()) {
			++m_processedFiles;
		}

		if (entry.is_dir() && (!entry.is_link() || m_operationMode != recursive_delete)) {
			if (dir.recurse) {
				recursion_root::new_dir dirToVisit;
				dirToVisit.parent = pDirectoryListing->path;
				dirToVisit.subdir = entry.name;
				dirToVisit.localDir = dir.localDir;
				dirToVisit.start_dir = dir.start_dir;

				if (m_operationMode == recursive_transfer || m_operationMode == recursive_synchronize_download) {
					// Non-flatten case
					dirToVisit.localDir.AddSegment(CQueueView::ReplaceInvalidCharacters(entry.name));
				}
				if (entry.is_link()) {
					dirToVisit.link = 1;
					dirToVisit.recurse = false;
				}
				root.m_dirsToVisit.push_front(dirToVisit);
			}
		}
		else {
			switch (m_operationMode)
			{
			case recursive_transfer:
			case recursive_transfer_flatten:
			case recursive_synchronize_download:
				{
					std::wstring localFile = CQueueView::ReplaceInvalidCharacters(entry.name);
					if (pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION)) {
						localFile = StripVMSRevision(localFile);
					}
					m_pQueue->QueueFile(!m_immediate, true,
						entry.name, (entry.name == localFile) ? std::wstring() : localFile,
						dir.localDir, pDirectoryListing->path, *pServer, entry.size);
					added = true;
				}
				break;
			case recursive_delete:
				filesToDelete.push_back(entry.name);
				break;
			default:
				break;
			}
		}

		if (m_operationMode == recursive_chmod && m_pChmodDlg) {
			const int applyType = m_pChmodDlg->GetApplyType();
			if (!applyType ||
				(!entry.is_dir() && applyType == 1) ||
				(entry.is_dir() && applyType == 2))
			{
				char permissions[9];
				bool res = m_pChmodDlg->ConvertPermissions(*entry.permissions, permissions);
				std::wstring newPerms = m_pChmodDlg->GetPermissions(res ? permissions : 0, entry.is_dir()).ToStdWstring();
				m_state.m_pCommandQueue->ProcessCommand(new CChmodCommand(pDirectoryListing->path, entry.name, newPerms), CCommandQueue::recursiveOperation);
			}
		}
	}
	if (added) {
		m_pQueue->QueueFile_Finish(m_immediate);
	}

	if (m_operationMode == recursive_delete && !filesToDelete.empty()) {
		m_state.m_pCommandQueue->ProcessCommand(new CDeleteCommand(pDirectoryListing->path, std::move(filesToDelete)), CCommandQueue::recursiveOperation);
	}

	m_state.NotifyHandlers(STATECHANGE_REMOTE_RECURSION_STATUS);

	NextOperation();
}

void CRemoteRecursiveOperation::SetChmodDialog(CChmodDialog* pChmodDialog)
{
	wxASSERT(pChmodDialog);

	if (m_pChmodDlg)
		m_pChmodDlg->Destroy();

	m_pChmodDlg = pChmodDialog;
}

void CRemoteRecursiveOperation::StopRecursiveOperation()
{
	if (m_operationMode != recursive_none) {
		m_operationMode = recursive_none;
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		m_state.NotifyHandlers(STATECHANGE_REMOTE_RECURSION_STATUS);
	}
	recursion_roots_.clear();

	if (m_pChmodDlg) {
		m_pChmodDlg->Destroy();
		m_pChmodDlg = 0;
	}

	m_actionAfterBlocker.reset();
}

void CRemoteRecursiveOperation::ListingFailed(int error)
{
	if (m_operationMode == recursive_none || recursion_roots_.empty())
		return;

	if( (error & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
		// User has cancelled operation
		StopRecursiveOperation();
		return;
	}

	auto & root = recursion_roots_.front();
	wxCHECK_RET(!root.m_dirsToVisit.empty(), _T("Empty dirs to visit"));

	recursion_root::new_dir dir = root.m_dirsToVisit.front();
	root.m_dirsToVisit.pop_front();
	if ((error & FZ_REPLY_CRITICALERROR) != FZ_REPLY_CRITICALERROR && !dir.second_try) {
		// Retry, could have been a temporary socket creating failure
		// (e.g. hitting a blocked port) or a disconnect (e.g. no-filetransfer-timeout)
		dir.second_try = true;
		root.m_dirsToVisit.push_front(dir);
	}

	NextOperation();
}

void CRemoteRecursiveOperation::LinkIsNotDir()
{
	if (m_operationMode == recursive_none || recursion_roots_.empty()) {
		return;
	}

	auto & root = recursion_roots_.front();
	wxCHECK_RET(!root.m_dirsToVisit.empty(), _T("Empty dirs to visit"));

	recursion_root::new_dir dir = root.m_dirsToVisit.front();
	root.m_dirsToVisit.pop_front();

	const CServer* pServer = m_state.GetServer();
	if (!pServer) {
		NextOperation();
		return;
	}

	if (m_operationMode == recursive_delete) {
		if (!dir.subdir.empty()) {
			std::deque<std::wstring> files;
			files.push_back(dir.subdir);
			m_state.m_pCommandQueue->ProcessCommand(new CDeleteCommand(dir.parent, std::move(files)), CCommandQueue::recursiveOperation);
		}
		NextOperation();
		return;
	}
	else if (m_operationMode != recursive_list) {
		CLocalPath localPath = dir.localDir;
		std::wstring localFile = dir.subdir;
		if (m_operationMode != recursive_transfer_flatten) {
			localPath.MakeParent();
		}
		m_pQueue->QueueFile(!m_immediate, true, dir.subdir, (dir.subdir == localFile) ? std::wstring() : localFile, localPath, dir.parent, *pServer, -1);
		m_pQueue->QueueFile_Finish(m_immediate);
	}

	NextOperation();
}
