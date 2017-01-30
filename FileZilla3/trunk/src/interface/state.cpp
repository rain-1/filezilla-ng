#include <filezilla.h>
#include "state.h"
#include "commandqueue.h"
#include "FileZillaEngine.h"
#include "Options.h"
#include "Mainfrm.h"
#include "queue.h"
#include "filezillaapp.h"
#include "local_recursive_operation.h"
#include "remote_recursive_operation.h"
#include "listingcomparison.h"
#include "xrc_helper.h"

#include <libfilezilla/local_filesys.hpp>

CContextManager CContextManager::m_the_context_manager;

CContextManager::CContextManager()
{
	m_current_context = -1;
}

CContextManager* CContextManager::Get()
{
	return &m_the_context_manager;
}

CState* CContextManager::CreateState(CMainFrame &mainFrame)
{
	CState* pState = new CState(mainFrame);

	m_contexts.push_back(pState);

	NotifyHandlers(pState, STATECHANGE_NEWCONTEXT, _T(""), 0);

	return pState;
}

void CContextManager::DestroyState(CState* pState)
{
	for (unsigned int i = 0; i < m_contexts.size(); ++i) {
		if (m_contexts[i] != pState)
			continue;

		m_contexts.erase(m_contexts.begin() + i);
		if ((int)i < m_current_context)
			m_current_context--;
		else if ((int)i == m_current_context) {
			if (i >= m_contexts.size())
				m_current_context--;
			NotifyHandlers(GetCurrentContext(), STATECHANGE_CHANGEDCONTEXT, _T(""), 0);
		}

		break;
	}

	NotifyHandlers(pState, STATECHANGE_REMOVECONTEXT, _T(""), 0);
	delete pState;
}

void CContextManager::SetCurrentContext(CState* pState)
{
	if (GetCurrentContext() == pState)
		return;

	for (unsigned int i = 0; i < m_contexts.size(); ++i) {
		if (m_contexts[i] != pState)
			continue;

		m_current_context = i;
		NotifyHandlers(GetCurrentContext(), STATECHANGE_CHANGEDCONTEXT, _T(""), 0);
	}
}

void CContextManager::DestroyAllStates()
{
	m_current_context = -1;
	NotifyHandlers(GetCurrentContext(), STATECHANGE_CHANGEDCONTEXT, _T(""), 0);

	while (!m_contexts.empty()) {
		CState* pState = m_contexts.back();
		m_contexts.pop_back();

		NotifyHandlers(pState, STATECHANGE_REMOVECONTEXT, _T(""), 0);
		delete pState;
	}
}

void CContextManager::RegisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification, bool current_only)
{
	wxASSERT(pHandler);
	wxASSERT(notification != STATECHANGE_MAX && notification != STATECHANGE_NONE);

	auto &handlers = m_handlers[notification];
	for (auto const& it : handlers) {
		if (it.pHandler == pHandler) {
			return;
		}
	}

	t_handler handler;
	handler.pHandler = pHandler;
	handler.current_only = current_only;
	handlers.push_back(handler);
}

void CContextManager::UnregisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification)
{
	wxASSERT(pHandler);
	wxASSERT(notification != STATECHANGE_MAX);

	if (notification == STATECHANGE_NONE) {
		for (int i = 0; i < STATECHANGE_MAX; ++i) {
			auto &handlers = m_handlers[i];
			for (auto iter = handlers.begin(); iter != handlers.end(); ++iter) {
				if (iter->pHandler == pHandler) {
					handlers.erase(iter);
					break;
				}
			}
		}
	}
	else {
		auto &handlers = m_handlers[notification];
		for (auto iter = handlers.begin(); iter != handlers.end(); ++iter) {
			if (iter->pHandler == pHandler) {
				handlers.erase(iter);
				return;
			}
		}
	}
}

size_t CContextManager::HandlerCount(t_statechange_notifications notification) const
{
	wxASSERT(notification != STATECHANGE_NONE && notification != STATECHANGE_MAX);
	return m_handlers[notification].size();
}

void CContextManager::NotifyHandlers(CState* pState, t_statechange_notifications notification, wxString const& data, void const* data2)
{
	wxASSERT(notification != STATECHANGE_NONE && notification != STATECHANGE_MAX);

	auto const& handlers = m_handlers[notification];
	for (auto const& handler : handlers) {
		if (handler.current_only && pState != GetCurrentContext()) {
			continue;
		}

		handler.pHandler->OnStateChange(pState, notification, data, data2);
	}
}

CState* CContextManager::GetCurrentContext()
{
	if (m_current_context == -1) {
		return 0;
	}

	return m_contexts[m_current_context];
}

void CContextManager::NotifyAllHandlers(t_statechange_notifications notification, wxString const& data, void const* data2)
{
	for (auto const& context : m_contexts) {
		context->NotifyHandlers(notification, data, data2);
	}
}

void CContextManager::NotifyGlobalHandlers(t_statechange_notifications notification, wxString const& data, void const* data2)
{
	auto const& handlers = m_handlers[notification];
	for (auto const& handler : handlers) {
		handler.pHandler->OnStateChange(0, notification, data, data2);
	}
}

void CContextManager::ProcessDirectoryListing(CServer const& server, std::shared_ptr<CDirectoryListing> const& listing, CState const* exempt)
{
	for (auto state : m_contexts) {
		if (state == exempt) {
			continue;
		}
		if (state->GetServer() && *state->GetServer() == server) {
			state->SetRemoteDir(listing, true);
		}
	}
}

CState::CState(CMainFrame &mainFrame)
	: m_mainFrame(mainFrame)
{
	m_title = _("Not connected");

	m_pComparisonManager = new CComparisonManager(*this);

	m_pLocalRecursiveOperation = new CLocalRecursiveOperation(*this);
	m_pRemoteRecursiveOperation = new CRemoteRecursiveOperation(*this);

	m_localDir.SetPath(std::wstring(1, CLocalPath::path_separator));
}

CState::~CState()
{
	delete m_pComparisonManager;
	delete m_pCommandQueue;
	delete m_pEngine;
	delete m_pLocalRecursiveOperation;
	delete m_pRemoteRecursiveOperation;

	// Unregister all handlers
	for (int i = 0; i < STATECHANGE_MAX; ++i) {
		wxASSERT(m_handlers[i].empty());
	}
}

CLocalPath CState::GetLocalDir() const
{
	return m_localDir;
}


bool CState::SetLocalDir(std::wstring const& dir, std::wstring *error, bool rememberPreviousSubdir)
{
	CLocalPath p(m_localDir);
#ifdef __WXMSW__
	if (dir == _T("..") && !p.HasParent() && p.HasLogicalParent()) {
		// Parent of C:\ is drive list
		if (!p.MakeParent()) {
			return false;
		}
	}
	else
#endif
	if (!p.ChangePath(dir)) {
		return false;
	}

	return SetLocalDir(p, error, rememberPreviousSubdir);
}

bool CState::SetLocalDir(CLocalPath const& dir, std::wstring *error, bool rememberPreviousSubdir)
{
	if (m_changeDirFlags.syncbrowse) {
		wxMessageBoxEx(_T("Cannot change directory, there already is a synchronized browsing operation in progress."), _("Synchronized browsing"));
		return false;
	}

	if (!dir.Exists(error)) {
		return false;
	}

	if (!m_sync_browse.local_root.empty()) {
		wxASSERT(m_site.m_server);

		if (dir != m_sync_browse.local_root && !dir.IsSubdirOf(m_sync_browse.local_root)) {
			wxString msg = wxString::Format(_("The local directory '%s' is not below the synchronization root (%s).\nDisable synchronized browsing and continue changing the local directory?"),
					dir.GetPath(),
					m_sync_browse.local_root.GetPath());
			if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
				return false;
			}
			SetSyncBrowse(false);
		}
		else if (!IsRemoteIdle(true)) {
			wxString msg(_("A remote operation is in progress and synchronized browsing is enabled.\nDisable synchronized browsing and continue changing the local directory?"));
			if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
				return false;
			}
			SetSyncBrowse(false);
		}
		else {
			CServerPath remote_path = GetSynchronizedDirectory(dir);
			if (remote_path.empty()) {
				SetSyncBrowse(false);
				wxString msg = wxString::Format(_("Could not obtain corresponding remote directory for the local directory '%s'.\nSynchronized browsing has been disabled."),
					dir.GetPath());
				wxMessageBoxEx(msg, _("Synchronized browsing"));
				return false;
			}

			m_changeDirFlags.syncbrowse = true;
			m_changeDirFlags.compare = m_pComparisonManager->IsComparing();
			m_sync_browse.target_path = remote_path;
			CListCommand *pCommand = new CListCommand(remote_path);
			m_pCommandQueue->ProcessCommand(pCommand);

			return true;
		}
	}

	if (dir == m_localDir.GetParent() && rememberPreviousSubdir) {
#ifdef __WXMSW__
		if (dir.GetPath() == _T("\\")) {
			m_previouslyVisitedLocalSubdir = m_localDir.GetPath();
			m_previouslyVisitedLocalSubdir.RemoveLast();
		}
		else
#endif
			m_previouslyVisitedLocalSubdir = m_localDir.GetLastSegment();
	}
	else {
		m_previouslyVisitedLocalSubdir = _T("");
	}


	m_localDir = dir;

	COptions::Get()->SetOption(OPTION_LASTLOCALDIR, m_localDir.GetPath());

	NotifyHandlers(STATECHANGE_LOCAL_DIR);

	return true;
}

bool CState::SetRemoteDir(std::shared_ptr<CDirectoryListing> const& pDirectoryListing, bool modified)
{
	if (!pDirectoryListing) {
		m_changeDirFlags.compare = false;
		SetSyncBrowse(false);
		if (modified) {
			return false;
		}

		if (m_pDirectoryListing) {
			m_pDirectoryListing = 0;
			NotifyHandlers(STATECHANGE_REMOTE_DIR, wxString(), &modified);
		}
		m_previouslyVisitedRemoteSubdir = _T("");
		return true;
	}

	wxASSERT(pDirectoryListing->m_firstListTime);

	if (pDirectoryListing && m_pDirectoryListing &&
		pDirectoryListing->path == m_pDirectoryListing->path.GetParent())
	{
		m_previouslyVisitedRemoteSubdir = m_pDirectoryListing->path.GetLastSegment();
	}
	else {
		m_previouslyVisitedRemoteSubdir = _T("");
	}

	if (modified) {
		if (!m_pDirectoryListing || m_pDirectoryListing->path != pDirectoryListing->path) {
			// We aren't interested in these listings
			return true;
		}
	}
	else {
		m_last_path = pDirectoryListing->path;
	}

	if (m_pDirectoryListing && m_pDirectoryListing->path == pDirectoryListing->path &&
		pDirectoryListing->failed())
	{
		// We still got an old listing, no need to display the new one
		return true;
	}

	m_pDirectoryListing = pDirectoryListing;

	NotifyHandlers(STATECHANGE_REMOTE_DIR, wxString(), &modified);

	bool compare = m_changeDirFlags.compare;
	if (!modified) {
		m_changeDirFlags.compare = false;
	}

	if (m_changeDirFlags.syncbrowse && !modified) {
		m_changeDirFlags.syncbrowse = false;
		if (m_pDirectoryListing->path != m_sync_browse.remote_root && !m_pDirectoryListing->path.IsSubdirOf(m_sync_browse.remote_root, false)) {
			SetSyncBrowse(false);
			wxString msg = wxString::Format(_("Current remote directory (%s) is not below the synchronization root (%s).\nSynchronized browsing has been disabled."),
					m_pDirectoryListing->path.GetPath(),
					m_sync_browse.remote_root.GetPath());
			wxMessageBoxEx(msg, _("Synchronized browsing"));
		}
		else {
			CLocalPath local_path = GetSynchronizedDirectory(m_pDirectoryListing->path);
			if (local_path.empty()) {
				SetSyncBrowse(false);
				wxString msg = wxString::Format(_("Could not obtain corresponding local directory for the remote directory '%s'.\nSynchronized browsing has been disabled."),
					m_pDirectoryListing->path.GetPath());
				wxMessageBoxEx(msg, _("Synchronized browsing"));
				compare = false;
			}
			else {
				std::wstring error;
				if (!local_path.Exists(&error)) {
					SetSyncBrowse(false);
					wxString msg = error + _T("\n") + _("Synchronized browsing has been disabled.");
					wxMessageBoxEx(msg, _("Synchronized browsing"));
					compare = false;
				}
				else {
					m_localDir = local_path;

					COptions::Get()->SetOption(OPTION_LASTLOCALDIR, m_localDir.GetPath());

					NotifyHandlers(STATECHANGE_LOCAL_DIR);
				}
			}
		}
	}

	if (compare && !m_pComparisonManager->IsComparing()) {
		m_pComparisonManager->CompareListings();
	}

	return true;
}

std::shared_ptr<CDirectoryListing> CState::GetRemoteDir() const
{
	return m_pDirectoryListing;
}

const CServerPath CState::GetRemotePath() const
{
	if (!m_pDirectoryListing)
		return CServerPath();

	return m_pDirectoryListing->path;
}

void CState::RefreshLocal()
{
	NotifyHandlers(STATECHANGE_LOCAL_DIR);
}

void CState::RefreshLocalFile(std::wstring const& file)
{
	std::wstring file_name;
	CLocalPath path(file, &file_name);
	if (path.empty()) {
		return;
	}

	if (file_name.empty()) {
		if (!path.HasParent()) {
			return;
		}
		path.MakeParent(&file_name);
		wxASSERT(!file_name.empty());
	}

	if (path != m_localDir) {
		return;
	}

	NotifyHandlers(STATECHANGE_LOCAL_REFRESH_FILE, file_name);
}

void CState::LocalDirCreated(const CLocalPath& path)
{
	if (!path.IsSubdirOf(m_localDir)) {
		return;
	}

	wxString next_segment = path.GetPath().substr(m_localDir.GetPath().size());
	int pos = next_segment.Find(CLocalPath::path_separator);
	if (pos <= 0) {
		// Shouldn't ever come true
		return;
	}

	// Current local path is /foo/
	// Called with /foo/bar/baz/
	// -> Refresh /foo/bar/
	next_segment = next_segment.Left(pos);
	NotifyHandlers(STATECHANGE_LOCAL_REFRESH_FILE, next_segment);
}

void CState::SetSite(Site const& site, CServerPath const& path)
{
	if (m_site.m_server) {
		if (site.m_server && site.m_server == m_site.m_server &&
			site.m_server.GetName() == m_site.m_server.GetName() &&
			site.m_server.MaximumMultipleConnections() == m_site.m_server.MaximumMultipleConnections())
		{
			// Nothing changes
			return;
		}

		SetRemoteDir(0);
		m_pCertificate.reset();
		m_pSftpEncryptionInfo.reset();
	}
	if (site.m_server) {
		if (!path.empty()) {
			m_last_path = path;
		}
		else if (m_last_site.m_server != site.m_server) {
			m_last_path.clear();
		}
		m_last_site = site;
	}

	m_site = site;

	UpdateTitle();

	m_successful_connect = false;

	NotifyHandlers(STATECHANGE_SERVER);
}

Site const& CState::GetSite() const
{
	return m_site;
}

CServer const* CState::GetServer() const
{
	return m_site.m_server ? &m_site.m_server : 0;
}

wxString CState::GetTitle() const
{
	return m_title;
}

bool CState::Connect(Site const& site, CServerPath const& path, bool compare)
{
	if (!site.m_server) {
		return false;
	}
	if (!m_pEngine) {
		return false;
	}
	if (m_pEngine->IsConnected() || m_pEngine->IsBusy() || !m_pCommandQueue->Idle()) {
		m_pCommandQueue->Cancel();
	}
	m_pRemoteRecursiveOperation->StopRecursiveOperation();
	SetSyncBrowse(false);

	m_pCommandQueue->ProcessCommand(new CConnectCommand(site.m_server));
	m_pCommandQueue->ProcessCommand(new CListCommand(path, _T(""), LIST_FLAG_FALLBACK_CURRENT));

	SetSite(site, path);

	m_changeDirFlags.compare = compare;

	return true;
}

bool CState::Disconnect()
{
	if (!m_pEngine) {
		return false;
	}

	if (!IsRemoteConnected()) {
		return true;
	}

	if (!IsRemoteIdle()) {
		return false;
	}

	SetSite(Site());
	m_pCommandQueue->ProcessCommand(new CDisconnectCommand());

	return true;
}

bool CState::CreateEngine()
{
	wxASSERT(!m_pEngine);
	if (m_pEngine)
		return true;

	m_pEngine = new CFileZillaEngine(m_mainFrame.GetEngineContext(), m_mainFrame);

	m_pCommandQueue = new CCommandQueue(m_pEngine, &m_mainFrame, *this);

	return true;
}

void CState::DestroyEngine()
{
	delete m_pCommandQueue;
	m_pCommandQueue = 0;
	delete m_pEngine;
	m_pEngine = 0;
}

void CState::RegisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification, CStateEventHandler* insertBefore)
{
	wxASSERT(pHandler);
	wxASSERT(&pHandler->m_state == this);
	if (!pHandler || &pHandler->m_state != this)
		return;
	wxASSERT(notification != STATECHANGE_MAX && notification != STATECHANGE_NONE);
	wxASSERT(pHandler != insertBefore);


	auto &handlers = m_handlers[notification];
	auto insertionPoint = handlers.end();

	for (auto it = handlers.begin(); it != handlers.end(); ++it) {
		if (it->pHandler == insertBefore) {
			insertionPoint = it;
		}
		if (it->pHandler == pHandler) {
			wxASSERT(insertionPoint == handlers.end());
			return;
		}
	}

	t_handler handler;
	handler.pHandler = pHandler;
	handlers.insert(insertionPoint, handler);
}

void CState::UnregisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification)
{
	wxASSERT(pHandler);
	wxASSERT(notification != STATECHANGE_MAX);

	if (notification == STATECHANGE_NONE) {
		for (int i = 0; i < STATECHANGE_MAX; ++i) {
			auto &handlers = m_handlers[i];
			for (auto iter = handlers.begin(); iter != handlers.end(); ++iter) {
				if (iter->pHandler == pHandler) {
					handlers.erase(iter);
					break;
				}
			}
		}
	}
	else {
		auto &handlers = m_handlers[notification];
		for (auto iter = handlers.begin(); iter != handlers.end(); ++iter) {
			if (iter->pHandler == pHandler) {
				handlers.erase(iter);
				return;
			}
		}
	}
}

void CState::NotifyHandlers(t_statechange_notifications notification, const wxString& data, const void* data2)
{
	wxASSERT(notification != STATECHANGE_NONE && notification != STATECHANGE_MAX);

	auto const& handlers = m_handlers[notification];
	for (auto const& handler : handlers) {
		handler.pHandler->OnStateChange(notification, data, data2);
	}

	CContextManager::Get()->NotifyHandlers(this, notification, data, data2);
}

CStateEventHandler::CStateEventHandler(CState &state)
	: m_state(state)
{
}

CStateEventHandler::~CStateEventHandler()
{
	m_state.UnregisterHandler(this, STATECHANGE_NONE);
}

CGlobalStateEventHandler::~CGlobalStateEventHandler()
{
	CContextManager::Get()->UnregisterHandler(this, STATECHANGE_NONE);
}

void CState::UploadDroppedFiles(const wxFileDataObject* pFileDataObject, const wxString& subdir, bool queueOnly)
{
	if (!m_site.m_server || !m_pDirectoryListing) {
		return;
	}

	CServerPath path = m_pDirectoryListing->path;
	if (subdir == _T("..") && path.HasParent()) {
		path = path.GetParent();
	}
	else if (!subdir.empty()) {
		path.AddSegment(subdir.ToStdWstring());
	}

	UploadDroppedFiles(pFileDataObject, path, queueOnly);
}

void CState::UploadDroppedFiles(const wxFileDataObject* pFileDataObject, const CServerPath& path, bool queueOnly)
{
	if (!m_site.m_server) {
		return;
	}

	if (path.empty()) {
		return;
	}

	auto recursiveOperation = GetLocalRecursiveOperation();
	if (!recursiveOperation || recursiveOperation->IsActive()) {
		wxBell();
		return;
	}

	wxArrayString const& files = pFileDataObject->GetFilenames();

	for (unsigned int i = 0; i < files.Count(); ++i) {
		int64_t size;
		bool is_link;
		fz::local_filesys::type type = fz::local_filesys::get_file_info(fz::to_native(files[i]), is_link, &size, 0, 0);
		if (type == fz::local_filesys::file) {
			std::wstring localFile;
			const CLocalPath localPath(files[i].ToStdWstring(), &localFile);
			m_mainFrame.GetQueue()->QueueFile(queueOnly, false, localFile, wxEmptyString, localPath, path, m_site.m_server, size);
			m_mainFrame.GetQueue()->QueueFile_Finish(!queueOnly);
		}
		else if (type == fz::local_filesys::dir) {
			CLocalPath localPath(files[i].ToStdWstring());
			if (localPath.HasParent()) {

				CServerPath remotePath = path;
				if (!remotePath.ChangePath(localPath.GetLastSegment())) {
					continue;
				}

				local_recursion_root root;
				root.add_dir_to_visit(localPath, remotePath);
				recursiveOperation->AddRecursionRoot(std::move(root));
			}
		}
	}

	CFilterManager filter;
	recursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_transfer, filter.GetActiveFilters(), !queueOnly);
}

void CState::HandleDroppedFiles(const wxFileDataObject* pFileDataObject, const CLocalPath& path, bool copy)
{
	const wxArrayString &files = pFileDataObject->GetFilenames();
	if (!files.Count())
		return;

#ifdef __WXMSW__
	int len = 1;

	for (unsigned int i = 0; i < files.Count(); ++i) {
		len += files[i].Len() + 1;
	}

	// SHFILEOPSTRUCT's pTo and pFrom accept null-terminated lists
	// of null-terminated filenames.
	wxChar* from = new wxChar[len];
	wxChar* p = from;
	for (unsigned int i = 0; i < files.Count(); ++i) {
		wxStrcpy(p, files[i]);
		p += files[i].Len() + 1;
	}
	*p = 0; // End of list

	wxChar* to = new wxChar[path.GetPath().size() + 2];
	wxStrcpy(to, path.GetPath());
	to[path.GetPath().size() + 1] = 0; // End of list

	SHFILEOPSTRUCT op = {0};
	op.pFrom = from;
	op.pTo = to;
	op.wFunc = copy ? FO_COPY : FO_MOVE;
	op.hwnd = (HWND)m_mainFrame.GetHandle();
	SHFileOperation(&op);

	delete [] to;
	delete [] from;
#else
	wxString error;
	for (unsigned int i = 0; i < files.Count(); ++i) {
		std::wstring const file(files[i].ToStdWstring());

		int64_t size;
		bool is_link;
		fz::local_filesys::type type = fz::local_filesys::get_file_info(fz::to_native(file), is_link, &size, 0, 0);
		if (type == fz::local_filesys::file) {
			std::wstring name;
			CLocalPath sourcePath(file, &name);
			if (name.empty()) {
				continue;
			}
			wxString target = path.GetPath() + name;
			if (file == target)
				continue;

			if (copy)
				wxCopyFile(file, target);
			else
				wxRenameFile(file, target);
		}
		else if (type == fz::local_filesys::dir) {
			CLocalPath sourcePath(file);
			if (sourcePath == path || sourcePath.GetParent() == path)
				continue;
			if (sourcePath.IsParentOf(path)) {
				error = _("A directory cannot be dragged into one of its subdirectories.");
				continue;
			}

			if (copy)
				RecursiveCopy(sourcePath, path);
			else {
				if (!sourcePath.HasParent())
					continue;
				wxRenameFile(file, path.GetPath() + sourcePath.GetLastSegment());
			}
		}
	}
	if (!error.empty())
		wxMessageBoxEx(error, _("Could not complete operation"));
#endif

	RefreshLocal();
}

bool CState::RecursiveCopy(CLocalPath source, const CLocalPath& target)
{
	if (source.empty() || target.empty()) {
		return false;
	}

	if (source == target) {
		return false;
	}

	if (source.IsParentOf(target)) {
		return false;
	}

	if (!source.HasParent()) {
		return false;
	}

	std::wstring last_segment;
	if (!source.MakeParent(&last_segment)) {
		return false;
	}

	std::list<std::wstring> dirsToVisit;
	dirsToVisit.push_back(last_segment + CLocalPath::path_separator);

	fz::native_string const nsource = fz::to_native(source.GetPath());

	// Process any subdirs which still have to be visited
	while (!dirsToVisit.empty()) {
		std::wstring dirname = dirsToVisit.front();
		dirsToVisit.pop_front();
		wxMkdir(target.GetPath() + dirname);

		fz::local_filesys fs;
		if (!fs.begin_find_files(nsource + fz::to_native(dirname), false)) {
			continue;
		}

		bool is_dir, is_link;
		fz::native_string file;
		while (fs.get_next_file(file, is_link, is_dir, 0, 0, 0)) {
			if (file.empty()) {
				wxGetApp().DisplayEncodingWarning();
				continue;
			}

			if (is_dir) {
				if (is_link) {
					continue;
				}

				std::wstring const subDir = dirname + fz::to_wstring(file) + CLocalPath::path_separator;
				dirsToVisit.push_back(subDir);
			}
			else {
				wxCopyFile(source.GetPath() + dirname + file, target.GetPath() + dirname + file);
			}
		}
	}

	return true;
}

bool CState::DownloadDroppedFiles(const CRemoteDataObject* pRemoteDataObject, const CLocalPath& path, bool queueOnly /*=false*/)
{
	bool hasDirs = false;
	bool hasFiles = false;
	const std::list<CRemoteDataObject::t_fileInfo>& files = pRemoteDataObject->GetFiles();
	for (auto const& fileInfo : files) {
		if (fileInfo.dir) {
			hasDirs = true;
		}
		else {
			hasFiles = true;
		}
	}

	if (hasDirs) {
		if (!IsRemoteConnected() || !IsRemoteIdle()) {
			return false;
		}
	}

	if (hasFiles) {
		m_mainFrame.GetQueue()->QueueFiles(queueOnly, path, *pRemoteDataObject);
	}

	if (!hasDirs) {
		return true;
	}

	recursion_root root(pRemoteDataObject->GetServerPath(), false);
	for (auto const& fileInfo : files) {
		if (!fileInfo.dir) {
			continue;
		}

		CLocalPath newPath(path);
		newPath.AddSegment(CQueueView::ReplaceInvalidCharacters(fileInfo.name));
		root.add_dir_to_visit(pRemoteDataObject->GetServerPath(), fileInfo.name, newPath, fileInfo.link);
	}

	if (!root.empty()) {
		m_pRemoteRecursiveOperation->AddRecursionRoot(std::move(root));

		CFilterManager filter;
		m_pRemoteRecursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_transfer, filter.GetActiveFilters(), pRemoteDataObject->GetServerPath(), !queueOnly);
	}

	return true;
}

bool CState::IsRemoteConnected() const
{
	if (!m_pEngine) {
		return false;
	}

	return static_cast<bool>(m_site.m_server);
}

bool CState::IsRemoteIdle(bool ignore_recursive) const
{
	if (!ignore_recursive && m_pRemoteRecursiveOperation && m_pRemoteRecursiveOperation->GetOperationMode() != CRecursiveOperation::recursive_none)
		return false;

	if (!m_pCommandQueue)
		return true;

	return m_pCommandQueue->Idle(ignore_recursive ? CCommandQueue::normal : CCommandQueue::any);
}

bool CState::IsLocalIdle(bool ignore_recursive) const
{
	if (!ignore_recursive && m_pLocalRecursiveOperation && m_pLocalRecursiveOperation->GetOperationMode() != CRecursiveOperation::recursive_none)
		return false;

	return true;
}

void CState::ListingFailed(int)
{
	bool const compare = m_changeDirFlags.compare;
	m_changeDirFlags.compare = false;

	if (m_changeDirFlags.syncbrowse && !m_sync_browse.target_path.empty()) {
		wxDialogEx dlg;
		if (!dlg.Load(0, _T("ID_SYNCBROWSE_NONEXISTING"))) {
			SetSyncBrowse(false);
			return;
		}

		xrc_call(dlg, "ID_SYNCBROWSE_NONEXISTING_LABEL", &wxStaticText::SetLabel, wxString::Format(_("The remote directory '%s' does not exist."), m_sync_browse.target_path.GetPath()));
		xrc_call(dlg, "ID_SYNCBROWSE_CREATE", &wxRadioButton::SetLabel, _("Create &missing remote directory and enter it"));
		xrc_call(dlg, "ID_SYNCBROWSE_DISABLE", &wxRadioButton::SetLabel, _("&Disable synchronized browsing and continue changing the local directory"));
		dlg.GetSizer()->Fit(&dlg);
		if (dlg.ShowModal() == wxID_OK) {
			if (xrc_call(dlg, "ID_SYNCBROWSE_CREATE", &wxRadioButton::GetValue)) {
				m_pCommandQueue->ProcessCommand(new CMkdirCommand(m_sync_browse.target_path));
				m_pCommandQueue->ProcessCommand(new CListCommand(m_sync_browse.target_path));
				m_sync_browse.target_path.clear();
				m_changeDirFlags.compare = compare;
				return;
			}
			else {
				CLocalPath local = GetSynchronizedDirectory(m_sync_browse.target_path);
				SetSyncBrowse(false);
				SetLocalDir(local);
			}
		}
		else {
			m_changeDirFlags.syncbrowse = false;
		}
	}
}

void CState::LinkIsNotDir(const CServerPath& path, const wxString& subdir)
{
	m_changeDirFlags.compare = false;
	m_changeDirFlags.syncbrowse = false;

	NotifyHandlers(STATECHANGE_REMOTE_LINKNOTDIR, subdir, &path);
}

bool CState::ChangeRemoteDir(CServerPath const& path, std::wstring const& subdir, int flags, bool ignore_busy, bool compare)
{
	if (!m_site.m_server || !m_pCommandQueue) {
		return false;
	}

	if (!m_sync_browse.local_root.empty()) {
		CServerPath p(path);
		if (!subdir.empty() && !p.ChangePath(subdir)) {
			wxString msg = wxString::Format(_("Could not get full remote path."));
			wxMessageBoxEx(msg, _("Synchronized browsing"));
			return false;
		}

		if (p != m_sync_browse.remote_root && !p.IsSubdirOf(m_sync_browse.remote_root, false)) {
			wxString msg = wxString::Format(_("The remote directory '%s' is not below the synchronization root (%s).\nDisable synchronized browsing and continue changing the remote directory?"),
					p.GetPath(),
					m_sync_browse.remote_root.GetPath());
			if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
				return false;
			}
			SetSyncBrowse(false);
		}
		else if (!IsRemoteIdle(true) && !ignore_busy) {
			wxString msg(_("Another remote operation is already in progress, cannot change directory now."));
			wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_EXCLAMATION);
			return false;
		}
		else {
			std::wstring error;
			CLocalPath local_path = GetSynchronizedDirectory(p);
			if (local_path.empty()) {
				wxString msg = wxString::Format(_("Could not obtain corresponding local directory for the remote directory '%s'.\nDisable synchronized browsing and continue changing the remote directory?"),
					p.GetPath());
				if (wxMessageBoxEx(msg, _("Synchronized browsing"), wxICON_QUESTION | wxYES_NO) != wxYES) {
					return false;
				}
				SetSyncBrowse(false);
			}
			else if (!local_path.Exists(&error)) {
				wxString msg = error + _T("\n") + _("Disable synchronized browsing and continue changing the remote directory?");

				wxDialogEx dlg;
				if (!dlg.Load(0, _T("ID_SYNCBROWSE_NONEXISTING"))) {
					return false;
				}
				xrc_call(dlg, "ID_SYNCBROWSE_NONEXISTING_LABEL", &wxStaticText::SetLabel, wxString::Format(_("The local directory '%s' does not exist."), local_path.GetPath()));
				xrc_call(dlg, "ID_SYNCBROWSE_CREATE", &wxRadioButton::SetLabel, _("Create &missing local directory and enter it"));
				xrc_call(dlg, "ID_SYNCBROWSE_DISABLE", &wxRadioButton::SetLabel, _("&Disable synchronized browsing and continue changing the remote directory"));
				dlg.GetSizer()->Fit(&dlg);
				if (dlg.ShowModal() != wxID_OK) {
					return false;
				}

				if (xrc_call(dlg, "ID_SYNCBROWSE_CREATE", &wxRadioButton::GetValue)) {
					{
						wxLogNull log;
						wxMkdir(local_path.GetPath());
					}

					if (!local_path.Exists(&error)) {
						wxMessageBox(wxString::Format(_("The local directory '%s' could not be created."), local_path.GetPath()), _("Synchronized browsing"), wxICON_EXCLAMATION);
						return false;
					}
					m_changeDirFlags.syncbrowse = true;
					m_changeDirFlags.compare = m_pComparisonManager->IsComparing();
					m_sync_browse.target_path.clear();
				}
				else {
					SetSyncBrowse(false);
				}
			}
			else {
				m_changeDirFlags.syncbrowse = true;
				m_changeDirFlags.compare = m_pComparisonManager->IsComparing();
				m_sync_browse.target_path.clear();
			}
		}
	}

	CListCommand *pCommand = new CListCommand(path, subdir, flags);
	m_pCommandQueue->ProcessCommand(pCommand);

	if (compare) {
		m_changeDirFlags.compare = true;
	}

	return true;
}

bool CState::SetSyncBrowse(bool enable, CServerPath const& assumed_remote_root)
{
	if (enable != m_sync_browse.local_root.empty()) {
		return enable;
	}

	if (!enable) {
		wxASSERT(assumed_remote_root.empty());
		m_sync_browse.local_root.clear();
		m_sync_browse.remote_root.clear();
		m_changeDirFlags.syncbrowse = false;

		NotifyHandlers(STATECHANGE_SYNC_BROWSE);
		return false;
	}

	if (!m_pDirectoryListing && assumed_remote_root.empty()) {
		NotifyHandlers(STATECHANGE_SYNC_BROWSE);
		return false;
	}

	m_changeDirFlags.syncbrowse = false;
	m_sync_browse.local_root = m_localDir;

	if (assumed_remote_root.empty()) {
		m_sync_browse.remote_root = m_pDirectoryListing->path;
	}
	else {
		m_sync_browse.remote_root = assumed_remote_root;
		m_changeDirFlags.syncbrowse = true;
	}

	while (m_sync_browse.local_root.HasParent() && m_sync_browse.remote_root.HasParent() &&
		m_sync_browse.local_root.GetLastSegment() == m_sync_browse.remote_root.GetLastSegment())
	{
		m_sync_browse.local_root.MakeParent();
		m_sync_browse.remote_root = m_sync_browse.remote_root.GetParent();
	}

	NotifyHandlers(STATECHANGE_SYNC_BROWSE);
	return true;
}

CLocalPath CState::GetSynchronizedDirectory(CServerPath remote_path)
{
	std::list<std::wstring> segments;
	while (remote_path.HasParent() && remote_path != m_sync_browse.remote_root) {
		segments.push_front(remote_path.GetLastSegment());
		remote_path = remote_path.GetParent();
	}
	if (remote_path != m_sync_browse.remote_root) {
		return CLocalPath();
	}

	CLocalPath local_path = m_sync_browse.local_root;
	for (auto const& segment : segments) {
		local_path.AddSegment(segment);
	}

	return local_path;
}


CServerPath CState::GetSynchronizedDirectory(CLocalPath local_path)
{
	std::list<std::wstring> segments;
	while (local_path.HasParent() && local_path != m_sync_browse.local_root) {
		std::wstring last;
		local_path.MakeParent(&last);
		segments.push_front(last);
	}
	if (local_path != m_sync_browse.local_root) {
		return CServerPath();
	}

	CServerPath remote_path = m_sync_browse.remote_root;
	for (auto const& segment : segments) {
		remote_path.AddSegment(segment);
	}

	return remote_path;
}

bool CState::RefreshRemote()
{
	if (!m_pCommandQueue) {
		return false;
	}

	if (!IsRemoteConnected() || !IsRemoteIdle(true)) {
		return false;
	}

	return ChangeRemoteDir(GetRemotePath(), _T(""), LIST_FLAG_REFRESH);
}

bool CState::GetSecurityInfo(CCertificateNotification *& pInfo)
{
	pInfo = m_pCertificate.get();
	return m_pCertificate != 0;
}

bool CState::GetSecurityInfo(CSftpEncryptionNotification *& pInfo)
{
	pInfo = m_pSftpEncryptionInfo.get();
	return m_pSftpEncryptionInfo != 0;
}

void CState::SetSecurityInfo(CCertificateNotification const& info)
{
	m_pSftpEncryptionInfo.reset();
	m_pCertificate = std::make_unique<CCertificateNotification>(info);
	NotifyHandlers(STATECHANGE_ENCRYPTION);
}

void CState::SetSecurityInfo(CSftpEncryptionNotification const& info)
{
	m_pCertificate.reset();
	m_pSftpEncryptionInfo = std::make_unique<CSftpEncryptionNotification>(info);
	NotifyHandlers(STATECHANGE_ENCRYPTION);
}

void CState::UpdateSite(wxString const& oldPath, Site const& newSite)
{
	if (newSite.m_path.empty() || !newSite.m_server) {
		return;
	}

	bool changed = false;
	if (m_site.m_server && m_site != newSite) {
		if (m_site.m_path == oldPath && m_site.m_server.EqualsNoPass(newSite.m_server)) {
			m_site = newSite;
			changed = true;
		}
	}
	if (m_last_site.m_server && m_last_site != newSite) {
		if (m_last_site.m_path == oldPath && m_last_site.m_server.EqualsNoPass(newSite.m_server)) {
			m_last_site = newSite;
			if (!m_site.m_server) {
				// Active site has precedence over historic data
				changed = true;
			}
		}
	}
	if (changed) {
		UpdateTitle();
		NotifyHandlers(STATECHANGE_SERVER);
	}
}

void CState::UpdateTitle()
{
	if (m_site.m_server) {
		wxString const& name = m_site.m_server.GetName();
		m_title.clear();
		if (!name.empty()) {
			m_title = name + _T(" - ");
		}
		m_title += m_site.m_server.Format(ServerFormat::with_user_and_optional_port);
	}
	else {
		m_title = _("Not connected");
	}
}
