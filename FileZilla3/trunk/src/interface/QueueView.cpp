#include <filezilla.h>
#include "queue.h"
#include "Mainfrm.h"
#include "Options.h"
#include "StatusView.h"
#include "statuslinectrl.h"
#include "xmlfunctions.h"
#include "filezillaapp.h"
#include "ipcmutex.h"
#include "local_recursive_operation.h"
#include "state.h"
#include "asyncrequestqueue.h"
#include "defaultfileexistsdlg.h"
#include <wx/dnd.h>
#include "dndobjects.h"
#include "loginmanager.h"
#include "aui_notebook_ex.h"
#include "queueview_failed.h"
#include "queueview_successful.h"
#include "commandqueue.h"
#include <wx/utils.h>
#include <wx/progdlg.h>
#include <wx/sound.h>
#include "statusbar.h"
#include "remote_recursive_operation.h"
#include "auto_ascii_files.h"
#include "dragdropmanager.h"
#include "drop_target_ex.h"
#if WITH_LIBDBUS
#include "../dbus/desktop_notification.h"
#elif defined(__WXGTK__) || defined(__WXMSW__)
#include <wx/notifmsg.h>
#endif

#ifdef __WXMSW__
#include <powrprof.h>
#endif

class CQueueViewDropTarget final : public CScrollableDropTarget<wxListCtrlEx>
{
public:
	CQueueViewDropTarget(CQueueView* pQueueView)
		: CScrollableDropTarget<wxListCtrlEx>(pQueueView)
		, m_pQueueView(pQueueView)
		, m_pFileDataObject(new wxFileDataObject())
		, m_pRemoteDataObject(new CRemoteDataObject())
	{
		m_pDataObject = new wxDataObjectComposite;
		m_pDataObject->Add(m_pRemoteDataObject, true);
		m_pDataObject->Add(m_pFileDataObject, false);
		SetDataObject(m_pDataObject);
	}

	virtual wxDragResult OnData(wxCoord, wxCoord, wxDragResult def)
	{
		def = FixupDragResult(def);
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
			return def;

		if (!GetData())
			return wxDragError;

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager)
			pDragDropManager->pDropTarget = m_pQueueView;

		if (m_pDataObject->GetReceivedFormat() == m_pFileDataObject->GetFormat()) {
			CState* const pState = CContextManager::Get()->GetCurrentContext();
			if (!pState)
				return wxDragNone;
			const CServer* const pServer = pState->GetServer();
			if (!pServer)
				return wxDragNone;

			const CServerPath& path = pState->GetRemotePath();
			if (path.empty())
				return wxDragNone;

			pState->UploadDroppedFiles(m_pFileDataObject, path, true);
		}
		else {
			if (m_pRemoteDataObject->GetProcessId() != (int)wxGetProcessId()) {
				wxMessageBoxEx(_("Drag&drop between different instances of FileZilla has not been implemented yet."));
				return wxDragNone;
			}

			CState* const pState = CContextManager::Get()->GetCurrentContext();
			if (!pState)
				return wxDragNone;
			const CServer* const pServer = pState->GetServer();
			if (!pServer)
				return wxDragNone;

			if (!pServer->EqualsNoPass(m_pRemoteDataObject->GetServer())) {
				wxMessageBoxEx(_("Drag&drop between different servers has not been implemented yet."));
				return wxDragNone;
			}

			const CLocalPath& target = pState->GetLocalDir();
			if (!target.IsWriteable()) {
				wxBell();
				return wxDragNone;
			}

			if (!pState->DownloadDroppedFiles(m_pRemoteDataObject, target, true))
				return wxDragNone;
		}

		return def;
	}

	virtual bool OnDrop(wxCoord, wxCoord)
	{
		return true;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxListCtrlEx>::OnDragOver(x, y, def);
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			return def;
		}

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager && !pDragDropManager->remoteParent.empty())
		{
			// Drag from remote to queue, check if local path is writeable
			CState* const pState = CContextManager::Get()->GetCurrentContext();
			if (!pState)
				return wxDragNone;
			if (!pState->GetLocalDir().IsWriteable())
				return wxDragNone;
		}

		def = wxDragCopy;

		return def;
	}

	virtual void OnLeave()
	{
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxListCtrlEx>::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

	int DisplayDropHighlight(wxPoint) { return -1; }
protected:
	CQueueView *m_pQueueView;
	wxFileDataObject* m_pFileDataObject;
	CRemoteDataObject* m_pRemoteDataObject;
	wxDataObjectComposite* m_pDataObject;
};

BEGIN_EVENT_TABLE(CQueueView, CQueueViewBase)
EVT_CONTEXT_MENU(CQueueView::OnContextMenu)
EVT_MENU(XRCID("ID_PROCESSQUEUE"), CQueueView::OnProcessQueue)
EVT_MENU(XRCID("ID_REMOVEALL"), CQueueView::OnStopAndClear)
EVT_MENU(XRCID("ID_REMOVE"), CQueueView::OnRemoveSelected)
EVT_MENU(XRCID("ID_DEFAULT_FILEEXISTSACTION"), CQueueView::OnSetDefaultFileExistsAction)
EVT_MENU(XRCID("ID_ACTIONAFTER_NONE"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_SHOW_NOTIFICATION_BUBBLE"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_REQUEST_ATTENTION"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_CLOSE"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_DISCONNECT"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_RUNCOMMAND"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_PLAYSOUND"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_REBOOT"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_SHUTDOWN"), CQueueView::OnActionAfter)
EVT_MENU(XRCID("ID_ACTIONAFTER_SLEEP"), CQueueView::OnActionAfter)

EVT_TIMER(wxID_ANY, CQueueView::OnTimer)
EVT_CHAR(CQueueView::OnChar)

EVT_MENU(XRCID("ID_PRIORITY_HIGHEST"), CQueueView::OnSetPriority)
EVT_MENU(XRCID("ID_PRIORITY_HIGH"), CQueueView::OnSetPriority)
EVT_MENU(XRCID("ID_PRIORITY_NORMAL"), CQueueView::OnSetPriority)
EVT_MENU(XRCID("ID_PRIORITY_LOW"), CQueueView::OnSetPriority)
EVT_MENU(XRCID("ID_PRIORITY_LOWEST"), CQueueView::OnSetPriority)

EVT_COMMAND(wxID_ANY, fzEVT_GRANTEXCLUSIVEENGINEACCESS, CQueueView::OnExclusiveEngineRequestGranted)

EVT_SIZE(CQueueView::OnSize)
END_EVENT_TABLE()

CQueueView::CQueueView(CQueue* parent, int index, CMainFrame* pMainFrame, CAsyncRequestQueue *pAsyncRequestQueue)
	: CQueueViewBase(parent, index, _("Queued files")),
	m_pMainFrame(pMainFrame),
	m_pAsyncRequestQueue(pAsyncRequestQueue)
{
	if (m_pAsyncRequestQueue)
		m_pAsyncRequestQueue->SetQueue(this);

	int action = COptions::Get()->GetOptionVal(OPTION_QUEUE_COMPLETION_ACTION);
	if (action < 0 || action >= ActionAfterState::Count) {
		action = 1;
	}
	else if (action == ActionAfterState::Reboot || action == ActionAfterState::Shutdown || action == ActionAfterState::Sleep) {
		action = 1;
	}
	m_actionAfterState = static_cast<ActionAfterState::type>(action);

	std::list<ColumnId> extraCols;
	extraCols.push_back(colTransferStatus);
	CreateColumns(extraCols);

	RegisterOption(OPTION_NUMTRANSFERS);
	RegisterOption(OPTION_CONCURRENTDOWNLOADLIMIT);
	RegisterOption(OPTION_CONCURRENTUPLOADLIMIT);

	SetDropTarget(new CQueueViewDropTarget(this));

	m_line_height = -1;
#ifdef __WXMSW__
	m_header_height = -1;
#endif

	m_resize_timer.SetOwner(this);
}

CQueueView::~CQueueView()
{
	DeleteEngines();

	m_resize_timer.Stop();
}

bool CQueueView::QueueFile(const bool queueOnly, const bool download,
						   std::wstring const& sourceFile, std::wstring const& targetFile,
						   const CLocalPath& localPath, const CServerPath& remotePath,
						   const CServer& server, int64_t size, CEditHandler::fileType edit,
						   QueuePriority priority)
{
	CServerItem* pServerItem = CreateServerItem(server);

	CFileItem* fileItem;
	if (sourceFile.empty()) {
		if (download) {
			CLocalPath p(localPath);
			p.AddSegment(targetFile);
			fileItem = new CFolderItem(pServerItem, queueOnly, p);
		}
		else {
			fileItem = new CFolderItem(pServerItem, queueOnly, remotePath, targetFile);
		}
		wxASSERT(edit == CEditHandler::none);
	}
	else {
		fileItem = new CFileItem(pServerItem, queueOnly, download, sourceFile, targetFile, localPath, remotePath, size);
		if (download) {
			fileItem->SetAscii(CAutoAsciiFiles::TransferRemoteAsAscii(sourceFile, remotePath.GetType()));
		}
		else {
			fileItem->SetAscii(CAutoAsciiFiles::TransferLocalAsAscii(sourceFile, remotePath.GetType()));
		}
		fileItem->m_edit = edit;
		if (edit != CEditHandler::none) {
			fileItem->m_onetime_action = CFileExistsNotification::overwrite;
		}
	}

	fileItem->SetPriorityRaw(priority);
	InsertItem(pServerItem, fileItem);

	return true;
}

void CQueueView::QueueFile_Finish(const bool start)
{
	bool need_refresh = false;
	if (m_insertionStart >= 0 && m_insertionStart <= GetTopItem() + GetCountPerPage() + 1) {
		need_refresh = true;
	}
	CommitChanges();

	if (!m_activeMode && start) {
		m_activeMode = 1;
		CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_QUEUEPROCESSING);
	}

	if (m_activeMode) {
		m_waitStatusLineUpdate = true;
		AdvanceQueue(false);
		m_waitStatusLineUpdate = false;
	}

	UpdateStatusLinePositions();

	if (need_refresh) {
		RefreshListOnly(false);
	}
}

// Defined in RemoteListView.cpp
std::wstring StripVMSRevision(std::wstring const& name);

bool CQueueView::QueueFiles(const bool queueOnly, const CLocalPath& localPath, const CRemoteDataObject& dataObject)
{
	CServerItem* pServerItem = CreateServerItem(dataObject.GetServer());

	const std::list<CRemoteDataObject::t_fileInfo>& files = dataObject.GetFiles();

	for (auto const& fileInfo : files) {
		if (fileInfo.dir)
			continue;

		std::wstring localFile = ReplaceInvalidCharacters(fileInfo.name);
		if (dataObject.GetServerPath().GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION)) {
			localFile = StripVMSRevision(localFile);
		}

		CFileItem* fileItem = new CFileItem(pServerItem, queueOnly, true,
			fileInfo.name, (fileInfo.name != localFile) ? localFile : std::wstring(),
			localPath, dataObject.GetServerPath(), fileInfo.size);
		fileItem->SetAscii(CAutoAsciiFiles::TransferRemoteAsAscii(fileInfo.name, dataObject.GetServerPath().GetType()));

		InsertItem(pServerItem, fileItem);
	}

	QueueFile_Finish(!queueOnly);

	return true;
}

bool CQueueView::QueueFiles(const bool queueOnly, CServer const& server, CLocalRecursiveOperation::listing const& listing)
{
	CServerItem* pServerItem = CreateServerItem(server);

	auto const& files = listing.files;
	if (files.empty() && listing.dirs.empty()) {
		// Empty directory
		CFileItem* fileItem = new CFolderItem(pServerItem, queueOnly, listing.remotePath, std::wstring());
		InsertItem(pServerItem, fileItem);
	}
	else {
		for (auto const& file : files) {
			CFileItem* fileItem = new CFileItem(pServerItem, queueOnly, false,
				file.name, std::wstring(),
				listing.localPath, listing.remotePath, file.size);
			fileItem->SetAscii(CAutoAsciiFiles::TransferLocalAsAscii(file.name, listing.remotePath.GetType()));

			InsertItem(pServerItem, fileItem);
		}

		// We do not look at dirs here, recursion takes care of it.
	}

	return true;
}

void CQueueView::OnEngineEvent(CFileZillaEngine* engine)
{
	CallAfter(&CQueueView::DoOnEngineEvent, engine);
}

void CQueueView::DoOnEngineEvent(CFileZillaEngine* engine)
{
	t_EngineData* const pEngineData = GetEngineData(engine);
	if (!pEngineData) {
		return;
	}

	std::unique_ptr<CNotification> pNotification = pEngineData->pEngine->GetNextNotification();
	while (pNotification) {
		ProcessNotification(pEngineData, std::move(pNotification));

		if (m_engineData.empty() || !pEngineData->pEngine) {
			break;
		}

		pNotification = pEngineData->pEngine->GetNextNotification();
	}
}

void CQueueView::ProcessNotification(CFileZillaEngine* pEngine, std::unique_ptr<CNotification> && pNotification)
{
	t_EngineData* pEngineData = GetEngineData(pEngine);
	if (pEngineData && pEngineData->active && pEngineData->transient) {
		ProcessNotification(pEngineData, std::move(pNotification));
	}
}

void CQueueView::ProcessNotification(t_EngineData* pEngineData, std::unique_ptr<CNotification> && pNotification)
{
	switch (pNotification->GetID())
	{
	case nId_logmsg:
		m_pMainFrame->GetStatusView()->AddToLog(static_cast<CLogmsgNotification&>(*pNotification.get()));
		if (COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION) == 2) {
			m_pQueue->Highlight(3);
		}
		break;
	case nId_operation:
		ProcessReply(pEngineData, static_cast<COperationNotification&>(*pNotification.get()));
		break;
	case nId_asyncrequest:
		if (pEngineData->pItem) {
			auto asyncRequestNotification = unique_static_cast<CAsyncRequestNotification>(std::move(pNotification));
			switch (asyncRequestNotification->GetRequestID()) {
				case reqId_fileexists:
				{
					CFileExistsNotification& fileExistsNotification = static_cast<CFileExistsNotification&>(*asyncRequestNotification.get());
					fileExistsNotification.overwriteAction = pEngineData->pItem->m_defaultFileExistsAction;

					if (pEngineData->pItem->GetType() == QueueItemType::File) {
						CFileItem* pFileItem = (CFileItem*)pEngineData->pItem;

						switch (pFileItem->m_onetime_action)
						{
						case CFileExistsNotification::resume:
							if (fileExistsNotification.canResume &&
								!pFileItem->Ascii())
							{
								fileExistsNotification.overwriteAction = CFileExistsNotification::resume;
							}
							break;
						case CFileExistsNotification::overwrite:
							fileExistsNotification.overwriteAction = CFileExistsNotification::overwrite;
							break;
						default:
							// Others are unused
							break;
						}
						pFileItem->m_onetime_action = CFileExistsNotification::unknown;
					}
				}
				break;
			default:
				break;
			}

			m_pAsyncRequestQueue->AddRequest(pEngineData->pEngine, std::move(asyncRequestNotification));
		}
		break;
	case nId_active:
		{
			auto const& activeNotification = static_cast<CActiveNotification const&>(*pNotification.get());
			m_pMainFrame->UpdateActivityLed(activeNotification.GetDirection());
		}
		break;
	case nId_transferstatus:
		if (pEngineData->pItem && pEngineData->pStatusLineCtrl) {
			auto const& transferStatusNotification = static_cast<CTransferStatusNotification const&>(*pNotification.get());
			CTransferStatus const& status = transferStatusNotification.GetStatus();
			if (pEngineData->active) {
				if (status && status.madeProgress && !status.list &&
					pEngineData->pItem->GetType() == QueueItemType::File)
				{
					CFileItem* pItem = (CFileItem*)pEngineData->pItem;
					pItem->set_made_progress(true);
				}
				pEngineData->pStatusLineCtrl->SetTransferStatus(status);
			}
		}
		break;
	case nId_local_dir_created:
		{
			auto const& localDirCreatedNotification = static_cast<CLocalDirCreatedNotification const&>(*pNotification.get());
			std::vector<CState*> const* pStates = CContextManager::Get()->GetAllStates();
			for (auto state : *pStates) {
				state->LocalDirCreated(localDirCreatedNotification.dir);
			}
		}
		break;
	case nId_listing:
		{
			auto const& listingNotification = static_cast<CDirectoryListingNotification const&>(*pNotification.get());
			if (!listingNotification.GetPath().empty() && !listingNotification.Failed() && pEngineData->pEngine) {
				std::shared_ptr<CDirectoryListing> pListing = std::make_shared<CDirectoryListing>();
				if (pEngineData->pEngine->CacheLookup(listingNotification.GetPath(), *pListing) == FZ_REPLY_OK) {
					CContextManager::Get()->ProcessDirectoryListing(pEngineData->lastServer, pListing, 0);
				}
			}
		}
		break;
	default:
		break;
	}
}

bool CQueueView::CanStartTransfer(const CServerItem& server_item, t_EngineData *&pEngineData)
{
	const CServer &server = server_item.GetServer();
	const int max_count = server.MaximumMultipleConnections();
	if (!max_count) {
		return true;
	}

	int active_count = server_item.m_activeCount;

	CState* browsingStateOnSameServer = 0;
	const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
	for (auto pState : *pStates) {
		const CServer* pBrowsingServer = pState->GetServer();
		if (!pBrowsingServer) {
			continue;
		}

		if (*pBrowsingServer == server) {
			active_count++;
			browsingStateOnSameServer = pState;
			break;
		}
	}

	if (active_count < max_count) {
		return true;
	}

	// Max count has been reached

	pEngineData = GetIdleEngine(&server, true);
	if (pEngineData) {
		// If we got an idle engine connected to this very server, start the
		// transfer anyhow. Let's not get this connection go to waste.
		if (pEngineData->lastServer == server && pEngineData->pEngine->IsConnected()) {
			return true;
		}
	}

	if (!browsingStateOnSameServer || active_count > 1) {
		return false;
	}

	// At this point the following holds:
	// max_count is limited to 1, only connection to server is held by the browsing connection

	pEngineData = GetEngineData(browsingStateOnSameServer->m_pEngine);
	if (pEngineData) {
		wxASSERT(pEngineData->transient);
		return pEngineData->transient && !pEngineData->active;
	}

	pEngineData = new t_EngineData;
	pEngineData->transient = true;
	pEngineData->state = t_EngineData::waitprimary;
	pEngineData->pEngine = browsingStateOnSameServer->m_pEngine;
	m_engineData.push_back(pEngineData);
	return true;
}

bool CQueueView::TryStartNextTransfer()
{
	if (m_quit || !m_activeMode) {
		return false;
	}

	// Check transfer limit
	if (m_activeCount >= COptions::Get()->GetOptionVal(OPTION_NUMTRANSFERS)) {
		return false;
	}

	// Check limits for concurrent up/downloads
	const int maxDownloads = COptions::Get()->GetOptionVal(OPTION_CONCURRENTDOWNLOADLIMIT);
	const int maxUploads = COptions::Get()->GetOptionVal(OPTION_CONCURRENTUPLOADLIMIT);
	TransferDirection wantedDirection;
	if (maxDownloads && m_activeCountDown >= maxDownloads) {
		if (maxUploads && m_activeCountUp >= maxUploads) {
			return false;
		}
		else {
			wantedDirection = TransferDirection::upload;
		}
	}
	else if (maxUploads && m_activeCountUp >= maxUploads) {
		wantedDirection = TransferDirection::download;
	}
	else {
		wantedDirection = TransferDirection::both;
	}

	struct t_bestMatch
	{
		t_bestMatch()
			: fileItem(), serverItem(), pEngineData()
		{
		}

		CFileItem* fileItem;
		CServerItem* serverItem;
		t_EngineData* pEngineData;
	} bestMatch;

	// Find inactive file. Check all servers for
	// the file with the highest priority
	for (auto const& currentServerItem : m_serverList) {
		t_EngineData* pEngineData = 0;

		if (!CanStartTransfer(*currentServerItem, pEngineData)) {
			continue;
		}

		CFileItem* newFileItem = currentServerItem->GetIdleChild(m_activeMode == 1, wantedDirection);

		while (newFileItem && newFileItem->Download() && newFileItem->GetType() == QueueItemType::Folder) {
			CLocalPath localPath(newFileItem->GetLocalPath());
			localPath.AddSegment(newFileItem->GetLocalFile());
			wxFileName::Mkdir(localPath.GetPath(), 0777, wxPATH_MKDIR_FULL);
			const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
			for (auto & state : *pStates) {
				state->RefreshLocalFile(localPath.GetPath());
			}
			if (RemoveItem(newFileItem, true)) {
				// Server got deleted. Unfortunately we have to start over now
				if (m_serverList.empty()) {
					return false;
				}

				return true;
			}
			newFileItem = currentServerItem->GetIdleChild(m_activeMode == 1, wantedDirection);
		}

		if (!newFileItem) {
			continue;
		}

		if (!bestMatch.fileItem || newFileItem->GetPriority() > bestMatch.fileItem->GetPriority()) {
			bestMatch.serverItem = currentServerItem;
			bestMatch.fileItem = newFileItem;
			bestMatch.pEngineData = pEngineData;
			if (newFileItem->GetPriority() == QueuePriority::highest) {
				break;
			}
		}
	}
	if (!bestMatch.fileItem) {
		return false;
	}

	// Find idle engine
	t_EngineData* pEngineData;
	if (bestMatch.pEngineData) {
		pEngineData = bestMatch.pEngineData;
	}
	else {
		pEngineData = GetIdleEngine(&bestMatch.serverItem->GetServer());
		if (!pEngineData) {
			return false;
		}
	}

	// Now we have both inactive engine and file.
	// Assign the file to the engine.

	bestMatch.fileItem->SetActive(true);

	pEngineData->pItem = bestMatch.fileItem;
	bestMatch.fileItem->m_pEngineData = pEngineData;
	pEngineData->active = true;
	delete pEngineData->m_idleDisconnectTimer;
	pEngineData->m_idleDisconnectTimer = 0;
	bestMatch.serverItem->m_activeCount++;
	m_activeCount++;
	if (bestMatch.fileItem->Download()) {
		m_activeCountDown++;
	}
	else {
		m_activeCountUp++;
	}

	CServer const oldServer = pEngineData->lastServer;
	pEngineData->lastServer = bestMatch.serverItem->GetServer();

	if (pEngineData->state != t_EngineData::waitprimary) {
		if (!pEngineData->pEngine->IsConnected()) {
			if (pEngineData->lastServer.GetLogonType() == ASK) {
				if (CLoginManager::Get().GetPassword(pEngineData->lastServer, true)) {
					pEngineData->state = t_EngineData::connect;
				}
				else {
					pEngineData->state = t_EngineData::askpassword;
				}
			}
			else {
				pEngineData->state = t_EngineData::connect;
			}
		}
		else if (oldServer != bestMatch.serverItem->GetServer()) {
			pEngineData->state = t_EngineData::disconnect;
		}
		else if (pEngineData->pItem->GetType() == QueueItemType::File) {
			pEngineData->state = t_EngineData::transfer;
		}
		else {
			pEngineData->state = t_EngineData::mkdir;
		}
	}

	if (bestMatch.fileItem->GetType() == QueueItemType::File) {
		// Create status line

		m_itemCount++;
		SetItemCount(m_itemCount);
		int lineIndex = GetItemIndex(bestMatch.fileItem);
		UpdateSelections_ItemAdded(lineIndex + 1);

		wxRect rect = GetClientRect();
		rect.y = GetLineHeight() * (lineIndex + 1 - GetTopItem());
#ifdef __WXMSW__
		rect.y += m_header_height;
#endif
		rect.SetHeight(GetLineHeight());
		m_allowBackgroundErase = false;
		if (!pEngineData->pStatusLineCtrl) {
			pEngineData->pStatusLineCtrl = new CStatusLineCtrl(this, pEngineData, rect);
		}
		else {
			pEngineData->pStatusLineCtrl->ClearTransferStatus();
			pEngineData->pStatusLineCtrl->SetSize(rect);
			pEngineData->pStatusLineCtrl->Show();
		}
		m_allowBackgroundErase = true;
		m_statusLineList.push_back(pEngineData->pStatusLineCtrl);
	}

	SendNextCommand(*pEngineData);

	return true;
}

void CQueueView::ProcessReply(t_EngineData* pEngineData, COperationNotification const& notification)
{
	if (notification.nReplyCode & FZ_REPLY_DISCONNECTED &&
		notification.commandId == ::Command::none)
	{
		// Queue is not interested in disconnect notifications
		return;
	}
	wxASSERT(notification.commandId != ::Command::none);

	// Cancel pending requests
	m_pAsyncRequestQueue->ClearPending(pEngineData->pEngine);

	// Process reply from the engine
	int replyCode = notification.nReplyCode;

	if ((replyCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
		ResetReason reason;
		if (pEngineData->pItem) {
			if (pEngineData->pItem->pending_remove()) {
				reason = remove;
			}
			else {
				if (pEngineData->pItem->GetType() == QueueItemType::File && ((CFileItem*)pEngineData->pItem)->made_progress()) {
					CFileItem* pItem = (CFileItem*)pEngineData->pItem;
					pItem->set_made_progress(false);
					pItem->m_onetime_action = CFileExistsNotification::resume;
				}
				reason = reset;
			}
			pEngineData->pItem->SetStatusMessage(CFileItem::interrupted);
		}
		else {
			reason = reset;
		}
		ResetEngine(*pEngineData, reason);
		return;
	}

	// Cycle through queue states
	switch (pEngineData->state)
	{
	case t_EngineData::disconnect:
		if (pEngineData->active) {
			pEngineData->state = t_EngineData::connect;
			if (pEngineData->pStatusLineCtrl) {
				pEngineData->pStatusLineCtrl->ClearTransferStatus();
			}
		}
		else
			pEngineData->state = t_EngineData::none;
		break;
	case t_EngineData::connect:
		if (!pEngineData->pItem) {
			ResetEngine(*pEngineData, reset);
			return;
		}
		else if (replyCode == FZ_REPLY_OK) {
			if (pEngineData->pItem->GetType() == QueueItemType::File) {
				pEngineData->state = t_EngineData::transfer;
			}
			else {
				pEngineData->state = t_EngineData::mkdir;
			}
			if (pEngineData->active && pEngineData->pStatusLineCtrl) {
				pEngineData->pStatusLineCtrl->ClearTransferStatus();
			}
		}
		else {
			if (replyCode & FZ_REPLY_PASSWORDFAILED) {
				CLoginManager::Get().CachedPasswordFailed(pEngineData->lastServer);
			}

			if ((replyCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				pEngineData->pItem->SetStatusMessage(CFileItem::none);
			}
			else if (replyCode & FZ_REPLY_PASSWORDFAILED) {
				pEngineData->pItem->SetStatusMessage(CFileItem::incorrect_password);
			}
			else if ((replyCode & FZ_REPLY_TIMEOUT) == FZ_REPLY_TIMEOUT) {
				pEngineData->pItem->SetStatusMessage(CFileItem::timeout);
			}
			else if (replyCode & FZ_REPLY_DISCONNECTED) {
				pEngineData->pItem->SetStatusMessage(CFileItem::disconnected);
			}
			else {
				pEngineData->pItem->SetStatusMessage(CFileItem::connection_failed);
			}

			if (replyCode != (FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED) ||
				!IsOtherEngineConnected(pEngineData))
			{
				if (!IncreaseErrorCount(*pEngineData)) {
					return;
				}
			}

			if (!pEngineData->transient) {
				SwitchEngine(&pEngineData);
			}
		}
		break;
	case t_EngineData::transfer:
		if (!pEngineData->pItem) {
			ResetEngine(*pEngineData, reset);
			return;
		}
		if (replyCode == FZ_REPLY_OK) {
			ResetEngine(*pEngineData, success);
			return;
		}
		// Increase error count only if item didn't make any progress. This keeps
		// user interaction at a minimum if connection is unstable.

		if (pEngineData->pItem->GetType() == QueueItemType::File && ((CFileItem*)pEngineData->pItem)->made_progress() &&
			(replyCode & FZ_REPLY_WRITEFAILED) != FZ_REPLY_WRITEFAILED)
		{
			// Don't increase error count if there has been progress
			CFileItem* pItem = (CFileItem*)pEngineData->pItem;
			pItem->set_made_progress(false);
			pItem->m_onetime_action = CFileExistsNotification::resume;
		}
		else {
			if ((replyCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				pEngineData->pItem->SetStatusMessage(CFileItem::none);
			}
			else if ((replyCode & FZ_REPLY_TIMEOUT) == FZ_REPLY_TIMEOUT) {
				pEngineData->pItem->SetStatusMessage(CFileItem::timeout);
			}
			else if (replyCode & FZ_REPLY_DISCONNECTED) {
				pEngineData->pItem->SetStatusMessage(CFileItem::disconnected);
			}
			else if ((replyCode & FZ_REPLY_WRITEFAILED) == FZ_REPLY_WRITEFAILED) {
				pEngineData->pItem->SetStatusMessage(CFileItem::local_file_unwriteable);
				ResetEngine(*pEngineData, failure);
				return;
			}
			else if ((replyCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR) {
				pEngineData->pItem->SetStatusMessage(CFileItem::could_not_start);
				ResetEngine(*pEngineData, failure);
				return;
			}
			else {
				pEngineData->pItem->SetStatusMessage(CFileItem::could_not_start);
			}
			if (!IncreaseErrorCount(*pEngineData)) {
				return;
			}

			if (replyCode & FZ_REPLY_DISCONNECTED && pEngineData->transient) {
				ResetEngine(*pEngineData, retry);
				return;
			}
		}
		if (replyCode & FZ_REPLY_DISCONNECTED) {
			if (!SwitchEngine(&pEngineData))
				pEngineData->state = t_EngineData::connect;
		}
		break;
	case t_EngineData::mkdir:
		if (replyCode == FZ_REPLY_OK) {
			ResetEngine(*pEngineData, success);
			return;
		}
		if (replyCode & FZ_REPLY_DISCONNECTED) {
			if (!IncreaseErrorCount(*pEngineData)) {
				return;
			}

			if (pEngineData->transient) {
				ResetEngine(*pEngineData, retry);
				return;
			}

			if (!SwitchEngine(&pEngineData)) {
				pEngineData->state = t_EngineData::connect;
			}
		}
		else {
			// Cannot retry
			ResetEngine(*pEngineData, failure);
			return;
		}

		break;
	case t_EngineData::list:
		ResetEngine(*pEngineData, remove);
		return;
	default:
		return;
	}

	if (pEngineData->state == t_EngineData::connect && pEngineData->lastServer.GetLogonType() == ASK) {
		if (!CLoginManager::Get().GetPassword(pEngineData->lastServer, true)) {
			pEngineData->state = t_EngineData::askpassword;
		}
	}

	if (!m_activeMode) {
		ResetReason reason;
		if (pEngineData->pItem && pEngineData->pItem->pending_remove()) {
			reason = remove;
		}
		else {
			reason = reset;
		}
		ResetEngine(*pEngineData, reason);
		return;
	}

	SendNextCommand(*pEngineData);
}

void CQueueView::ResetEngine(t_EngineData& data, const ResetReason reason)
{
	if (!data.active) {
		return;
	}

	m_waitStatusLineUpdate = true;

	if (data.pItem) {
		CServerItem* pServerItem = static_cast<CServerItem*>(data.pItem->GetTopLevelItem());
		if (pServerItem) {
			wxASSERT(pServerItem->m_activeCount > 0);
			if (pServerItem->m_activeCount > 0)
				pServerItem->m_activeCount--;
		}

		if (data.pItem->GetType() == QueueItemType::File) {
			wxASSERT(data.pStatusLineCtrl);
			for (auto iter = m_statusLineList.begin(); iter != m_statusLineList.end(); ++iter) {
				if (*iter == data.pStatusLineCtrl) {
					m_statusLineList.erase(iter);
					break;
				}
			}
			m_allowBackgroundErase = false;
			data.pStatusLineCtrl->Hide();
			m_allowBackgroundErase = true;

			UpdateSelections_ItemRemoved(GetItemIndex(data.pItem) + 1);

			m_itemCount--;
			SaveSetItemCount(m_itemCount);

			CFileItem* const pFileItem = (CFileItem*)data.pItem;
			if (pFileItem->Download()) {
				const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
				for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter)
					(*iter)->RefreshLocalFile(pFileItem->GetLocalPath().GetPath() + pFileItem->GetLocalFile());
			}

			if (pFileItem->m_edit != CEditHandler::none && reason != retry && reason != reset) {
				CEditHandler* pEditHandler = CEditHandler::Get();
				wxASSERT(pEditHandler);
				if (pFileItem->m_edit == CEditHandler::remote) {
					pEditHandler->FinishTransfer(reason == success, pFileItem->GetRemoteFile(), pFileItem->GetRemotePath(), pServerItem->GetServer());
				}
				else {
					pEditHandler->FinishTransfer(reason == success, pFileItem->GetLocalPath().GetPath() + pFileItem->GetLocalFile());
				}
				if (reason == success) {
					pFileItem->m_edit = CEditHandler::none;
				}
			}

			if (reason == failure) {
				pFileItem->m_onetime_action = CFileExistsNotification::unknown;
				pFileItem->set_made_progress(false);
			}
		}

		wxASSERT(data.pItem->IsActive());
		wxASSERT(data.pItem->m_pEngineData == &data);
		if (data.pItem->IsActive()) {
			data.pItem->SetActive(false);
		}
		if (data.pItem->Download()) {
			wxASSERT(m_activeCountDown > 0);
			if (m_activeCountDown > 0) {
				m_activeCountDown--;
			}
		}
		else {
			wxASSERT(m_activeCountUp > 0);
			if (m_activeCountUp > 0) {
				m_activeCountUp--;
			}
		}

		if (reason == reset) {
			if (!data.pItem->queued()) {
				static_cast<CServerItem*>(data.pItem->GetTopLevelItem())->QueueImmediateFile(data.pItem);
			}
		}
		else if (reason == failure) {
			if (data.pItem->GetType() == QueueItemType::File || data.pItem->GetType() == QueueItemType::Folder) {
				const CServer server = ((CServerItem*)data.pItem->GetTopLevelItem())->GetServer();

				RemoveItem(data.pItem, false);

				CQueueViewFailed* pQueueViewFailed = m_pQueue->GetQueueView_Failed();
				CServerItem* pNewServerItem = pQueueViewFailed->CreateServerItem(server);
				data.pItem->SetParent(pNewServerItem);
				data.pItem->UpdateTime();
				pQueueViewFailed->InsertItem(pNewServerItem, data.pItem);
				pQueueViewFailed->CommitChanges();
			}
		}
		else if (reason == success) {
			if (data.pItem->GetType() == QueueItemType::File || data.pItem->GetType() == QueueItemType::Folder) {
				CQueueViewSuccessful* pQueueViewSuccessful = m_pQueue->GetQueueView_Successful();
				if (pQueueViewSuccessful->AutoClear()) {
					RemoveItem(data.pItem, true);
				}
				else {
					const CServer server = ((CServerItem*)data.pItem->GetTopLevelItem())->GetServer();

					RemoveItem(data.pItem, false);

					CServerItem* pNewServerItem = pQueueViewSuccessful->CreateServerItem(server);
					data.pItem->UpdateTime();
					data.pItem->SetParent(pNewServerItem);
					data.pItem->SetStatusMessage(CFileItem::none);
					pQueueViewSuccessful->InsertItem(pNewServerItem, data.pItem);
					pQueueViewSuccessful->CommitChanges();
				}
			}
			else {
				RemoveItem(data.pItem, true);
			}
		}
		else if (reason == retry) {
		}
		else {
			RemoveItem(data.pItem, true);
		}
		data.pItem = 0;
	}
	wxASSERT(m_activeCount > 0);
	if (m_activeCount > 0) {
		m_activeCount--;
	}
	data.active = false;

	if (data.state == t_EngineData::waitprimary && data.pEngine) {
		const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
		for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter) {
			CState* pState = *iter;
			if (pState->m_pEngine != data.pEngine) {
				continue;
			}
			CCommandQueue* pCommandQueue = pState->m_pCommandQueue;
			if (pCommandQueue) {
				pCommandQueue->RequestExclusiveEngine(false);
			}
			break;
		}
	}

	data.state = t_EngineData::none;

	AdvanceQueue();

	m_waitStatusLineUpdate = false;
	UpdateStatusLinePositions();
}

bool CQueueView::RemoveItem(CQueueItem* item, bool destroy, bool updateItemCount, bool updateSelections, bool forward)
{
	// RemoveItem assumes that the item has already been removed from all engines

	if (item->GetType() == QueueItemType::File) {
		// Update size information
		const CFileItem* const pFileItem = (const CFileItem* const)item;
		int64_t size = pFileItem->GetSize();
		if (size < 0) {
			m_filesWithUnknownSize--;
			wxASSERT(m_filesWithUnknownSize >= 0);
			if (!m_filesWithUnknownSize && updateItemCount) {
				DisplayQueueSize();
			}
		}
		else if (size > 0) {
			m_totalQueueSize -= size;
			if (updateItemCount) {
				DisplayQueueSize();
			}
			wxASSERT(m_totalQueueSize >= 0);
		}
	}

	bool didRemoveParent = CQueueViewBase::RemoveItem(item, destroy, updateItemCount, updateSelections, forward);

	UpdateStatusLinePositions();

	return didRemoveParent;
}

void CQueueView::SendNextCommand(t_EngineData& engineData)
{
	for (;;) {
		if (engineData.state == t_EngineData::waitprimary) {
			engineData.pItem->SetStatusMessage(CFileItem::wait_browsing);

			wxASSERT(engineData.pEngine);
			if (!engineData.pEngine) {
				ResetEngine(engineData, retry);
				return;
			}

			CState* pState = 0;
			const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
			for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter) {
				if ((*iter)->m_pEngine != engineData.pEngine) {
					continue;
				}
				pState = *iter;
				break;
			}
			if (!pState) {
				ResetEngine(engineData, retry);
				return;
			}

			CCommandQueue* pCommandQueue = pState->m_pCommandQueue;
			pCommandQueue->RequestExclusiveEngine(true);
			return;
		}

		if (engineData.state == t_EngineData::disconnect) {
			engineData.pItem->SetStatusMessage(CFileItem::disconnecting);
			RefreshItem(engineData.pItem);
			if (engineData.pEngine->Execute(CDisconnectCommand()) == FZ_REPLY_WOULDBLOCK) {
				return;
			}

			if (engineData.lastServer.GetLogonType() == ASK && !CLoginManager::Get().GetPassword(engineData.lastServer, true)) {
				engineData.state = t_EngineData::askpassword;
			}
			else {
				engineData.state = t_EngineData::connect;
			}

			if (engineData.active && engineData.pStatusLineCtrl) {
				engineData.pStatusLineCtrl->ClearTransferStatus();
			}
		}

		if (engineData.state == t_EngineData::askpassword) {
			engineData.pItem->SetStatusMessage(CFileItem::wait_password);
			RefreshItem(engineData.pItem);
			if (m_waitingForPassword.empty()) {
				CallAfter(&CQueueView::OnAskPassword);
			}
			m_waitingForPassword.push_back(engineData.pEngine);
			return;
		}

		if (engineData.state == t_EngineData::connect) {
			engineData.pItem->SetStatusMessage(CFileItem::connecting);
			RefreshItem(engineData.pItem);

			int res = engineData.pEngine->Execute(CConnectCommand(engineData.lastServer, false));

			wxASSERT((res & FZ_REPLY_BUSY) != FZ_REPLY_BUSY);
			if (res == FZ_REPLY_WOULDBLOCK)
				return;

			if (res == FZ_REPLY_ALREADYCONNECTED) {
				engineData.state = t_EngineData::disconnect;
				continue;
			}

			if (res == FZ_REPLY_OK) {
				if (engineData.pItem->GetType() == QueueItemType::File) {
					engineData.state = t_EngineData::transfer;
					if (engineData.active && engineData.pStatusLineCtrl) {
						engineData.pStatusLineCtrl->ClearTransferStatus();
					}
				}
				else {
					engineData.state = t_EngineData::mkdir;
				}
				break;
			}

			if (!IncreaseErrorCount(engineData)) {
				return;
			}
			continue;
		}

		if (engineData.state == t_EngineData::transfer) {
			CFileItem* fileItem = engineData.pItem;

			fileItem->SetStatusMessage(CFileItem::transferring);
			RefreshItem(engineData.pItem);

			CFileTransferCommand::t_transferSettings transferSettings;
			transferSettings.binary = !fileItem->Ascii();
			int res = engineData.pEngine->Execute(CFileTransferCommand(fileItem->GetLocalPath().GetPath() + fileItem->GetLocalFile(), fileItem->GetRemotePath(),
												fileItem->GetRemoteFile(), fileItem->Download(), transferSettings));
			wxASSERT((res & FZ_REPLY_BUSY) != FZ_REPLY_BUSY);
			if (res == FZ_REPLY_WOULDBLOCK) {
				return;
			}

			if (res == FZ_REPLY_NOTCONNECTED) {
				if (engineData.transient) {
					ResetEngine(engineData, retry);
					return;
				}

				engineData.state = t_EngineData::connect;
				continue;
			}

			if (res == FZ_REPLY_OK) {
				ResetEngine(engineData, success);
				return;
			}

			if (!IncreaseErrorCount(engineData)) {
				return;
			}
			continue;
		}

		if (engineData.state == t_EngineData::mkdir) {
			CFileItem* fileItem = engineData.pItem;

			fileItem->SetStatusMessage(CFileItem::creating_dir);
			RefreshItem(engineData.pItem);

			int res = engineData.pEngine->Execute(CMkdirCommand(fileItem->GetRemotePath()));

			wxASSERT((res & FZ_REPLY_BUSY) != FZ_REPLY_BUSY);
			if (res == FZ_REPLY_WOULDBLOCK) {
				return;
			}

			if (res == FZ_REPLY_NOTCONNECTED) {
				if (engineData.transient) {
					ResetEngine(engineData, retry);
					return;
				}

				engineData.state = t_EngineData::connect;
				continue;
			}

			if (res == FZ_REPLY_OK) {
				ResetEngine(engineData, success);
				return;
			}

			// Pointless to retry
			ResetEngine(engineData, failure);
			return;
		}
	}
}

bool CQueueView::SetActive(bool active)
{
	if (!active) {
		m_activeMode = 0;
		for (auto const& serverItem : m_serverList) {
			serverItem->QueueImmediateFiles();
		}

		const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
		for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter) {
			CState* pState = *iter;

			CLocalRecursiveOperation* pLocalRecursiveOperation = pState->GetLocalRecursiveOperation();
			if (pLocalRecursiveOperation) {
				pLocalRecursiveOperation->SetImmediate(false);
			}
			CRemoteRecursiveOperation* pRemoteRecursiveOperation = pState->GetRemoteRecursiveOperation();
			if (pRemoteRecursiveOperation) {
				pRemoteRecursiveOperation->SetImmediate(false);
			}
		}

		auto blocker = m_actionAfterBlocker.lock();
		if (blocker) {
			blocker->trigger_ = false;
		}

		UpdateStatusLinePositions();

		// Send active engines the cancel command
		for (unsigned int engineIndex = 0; engineIndex < m_engineData.size(); ++engineIndex) {
			t_EngineData* const pEngineData = m_engineData[engineIndex];
			if (!pEngineData->active) {
				continue;
			}

			if (pEngineData->state == t_EngineData::waitprimary) {
				if (pEngineData->pItem) {
					pEngineData->pItem->SetStatusMessage(CFileItem::interrupted);
				}
				ResetEngine(*pEngineData, reset);
			}
			else {
				wxASSERT(pEngineData->pEngine);
				if (!pEngineData->pEngine) {
					continue;
				}
				pEngineData->pEngine->Cancel();
			}
		}

		CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_QUEUEPROCESSING);

		return m_activeCount == 0;
	}
	else {
		if (!m_serverList.empty()) {
			m_activeMode = 2;

			m_waitStatusLineUpdate = true;
			AdvanceQueue();
			m_waitStatusLineUpdate = false;
			UpdateStatusLinePositions();
		}
	}

	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_QUEUEPROCESSING);

	return true;
}

bool CQueueView::Quit()
{
	if (!m_quit) {
		m_quit = 1;
	}

#if defined(__WXMSW__) || defined(__WXMAC__)
	if (m_actionAfterWarnDialog) {
		m_actionAfterWarnDialog->Destroy();
		m_actionAfterWarnDialog = 0;
	}
	delete m_actionAfterTimer;
	m_actionAfterTimer = 0;
#endif

	bool canQuit = true;
	if (!SetActive(false))
		canQuit = false;

	if (!canQuit)
		return false;

	DeleteEngines();

	if (m_quit == 1) {
		SaveQueue();
		m_quit = 2;
	}

	SaveColumnSettings(OPTION_QUEUE_COLUMN_WIDTHS, -1, -1);

	m_resize_timer.Stop();

	return true;
}

void CQueueView::CheckQueueState()
{
	for (unsigned int i = 0; i < m_engineData.size(); ) {
		t_EngineData* data = m_engineData[i];
		if (!data->active && data->transient) {
			if (data->pEngine)
				ReleaseExclusiveEngineLock(data->pEngine);
			delete data;
			m_engineData.erase(m_engineData.begin() + i);
		}
		else {
			++i;
		}
	}

	if (m_activeCount)
		return;

	if (m_activeMode) {
		m_activeMode = 0;
		/* Users don't seem to like this, so comment it out for now.
		 * maybe make it configureable in future?
		if (!m_pQueue->GetSelection())
		{
			CQueueViewBase* pFailed = m_pQueue->GetQueueView_Failed();
			CQueueViewBase* pSuccessful = m_pQueue->GetQueueView_Successful();
			if (pFailed->GetItemCount())
				m_pQueue->SetSelection(1);
			else if (pSuccessful->GetItemCount())
				m_pQueue->SetSelection(2);
		}
		*/

		TryRefreshListings();

		CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_QUEUEPROCESSING);

		ActionAfter();
	}

	if (m_quit) {
		m_pMainFrame->Close();
	}
}

bool CQueueView::IncreaseErrorCount(t_EngineData& engineData)
{
	++engineData.pItem->m_errorCount;
	if (engineData.pItem->m_errorCount <= COptions::Get()->GetOptionVal(OPTION_RECONNECTCOUNT)) {
		return true;
	}

	ResetEngine(engineData, failure);

	return false;
}

void CQueueView::UpdateStatusLinePositions()
{
	if (m_waitStatusLineUpdate) {
		return;
	}

	m_lastTopItem = GetTopItem();
	int bottomItem = m_lastTopItem + GetCountPerPage();

	wxRect lineRect = GetClientRect();
	lineRect.SetHeight(GetLineHeight());
#ifdef __WXMSW__
	lineRect.y += m_header_height;
#endif

	for (auto pCtrl : m_statusLineList) {
		int index = GetItemIndex(pCtrl->GetItem()) + 1;
		if (index < m_lastTopItem || index > bottomItem) {
			pCtrl->Show(false);
			continue;
		}

		wxRect rect = lineRect;
		rect.y += GetLineHeight() * (index - m_lastTopItem);

		m_allowBackgroundErase = bottomItem + 1 >= m_itemCount;
		pCtrl->SetSize(rect);
		m_allowBackgroundErase = false;
		pCtrl->Show();
		m_allowBackgroundErase = true;
	}
}

void CQueueView::CalculateQueueSize()
{
	// Collect total queue size
	m_totalQueueSize = 0;
	m_fileCount = 0;

	m_filesWithUnknownSize = 0;
	for (auto const& serverItem : m_serverList) {
		m_totalQueueSize += serverItem->GetTotalSize(m_filesWithUnknownSize, m_fileCount);
	}

	DisplayQueueSize();
	DisplayNumberQueuedFiles();
}

void CQueueView::DisplayQueueSize()
{
	CStatusBar* pStatusBar = dynamic_cast<CStatusBar*>(m_pMainFrame->GetStatusBar());
	if (!pStatusBar)
		return;
	pStatusBar->DisplayQueueSize(m_totalQueueSize, m_filesWithUnknownSize != 0);
}

void CQueueView::SaveQueue()
{
	// Kiosk mode 2 doesn't save queue
	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2)
		return;

	// While not really needed anymore using sqlite3, we still take the mutex
	// just as extra precaution. Better 'save' than sorry.
	CInterProcessMutex mutex(MUTEX_QUEUE);

	if (!m_queue_storage.SaveQueue(m_serverList)) {
		wxString msg = wxString::Format(_("An error occurred saving the transfer queue to \"%s\".\nSome queue items might not have been saved."), m_queue_storage.GetDatabaseFilename());
		wxMessageBoxEx(msg, _("Error saving queue"), wxICON_ERROR);
	}
}

void CQueueView::LoadQueueFromXML()
{
	CXmlFile xml(wxGetApp().GetSettingsFile(_T("queue")));
	auto document = xml.Load();
	if (!document) {
		if (!xml.GetError().empty()) {
			wxString msg = xml.GetError() + _T("\n\n") + _("The queue will not be saved.");
			wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);
		}
		return;
	}

	auto queue = document.child("Queue");
	if (!queue)
		return;

	ImportQueue(queue, false);

	document.remove_child(queue);

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2)
		return;

	if (!xml.Save(false)) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the queue could not be saved.\n%s"), xml.GetFileName(), xml.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}
}

void CQueueView::LoadQueue()
{
	// We have to synchronize access to queue.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_QUEUE);

	LoadQueueFromXML();

	bool error = false;

	if (!m_queue_storage.BeginTransaction())
		error = true;
	else {
		CServer server;
		int64_t const first_id = m_queue_storage.GetServer(server, true);
		auto id = first_id;
		for (; id > 0; id = m_queue_storage.GetServer(server, false)) {
			m_insertionStart = -1;
			m_insertionCount = 0;
			CServerItem *pServerItem = CreateServerItem(server);

			CFileItem* fileItem = 0;
			int64_t fileId;
			for (fileId = m_queue_storage.GetFile(&fileItem, id); fileItem; fileId = m_queue_storage.GetFile(&fileItem, 0)) {
				fileItem->SetParent(pServerItem);
				fileItem->SetPriority(fileItem->GetPriority());
				InsertItem(pServerItem, fileItem);
			}
			if (fileId < 0)
				error = true;

			if (!pServerItem->GetChild(0)) {
				m_itemCount--;
				m_serverList.pop_back();
				delete pServerItem;
			}
		}
		if (id < 0)
			error = true;

		if (error || first_id > 0) {
			if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 2) {
				if (!m_queue_storage.Clear()) {
					error = true;
				}
			}

			if (!m_queue_storage.EndTransaction()) {
				error = true;
			}

			if (!m_queue_storage.Vacuum()) {
				error = true;
			}
		}
		else {
			// Queue was already empty. No need to commit
			if (!m_queue_storage.EndTransaction(true)) {
				error = true;
			}
		}
	}

	m_insertionStart = -1;
	m_insertionCount = 0;
	CommitChanges();
	if (error) {
		wxString file = CQueueStorage::GetDatabaseFilename();
		wxString msg = wxString::Format(_("An error occurred loading the transfer queue from \"%s\".\nSome queue items might not have been restored."), file);
		wxMessageBoxEx(msg, _("Error loading queue"), wxICON_ERROR);
	}
}

void CQueueView::ImportQueue(pugi::xml_node element, bool updateSelections)
{
	auto xServer = element.child("Server");
	while (xServer) {
		CServer server;
		if (GetServer(xServer, server)) {
			m_insertionStart = -1;
			m_insertionCount = 0;
			CServerItem *pServerItem = CreateServerItem(server);

			CLocalPath previousLocalPath;
			CServerPath previousRemotePath;

			for (auto file = xServer.child("File"); file; file = file.next_sibling("File")) {
				std::wstring localFile = GetTextElement(file, "LocalFile");
				std::wstring remoteFile = GetTextElement(file, "RemoteFile");
				std::wstring safeRemotePath = GetTextElement(file, "RemotePath");
				bool download = GetTextElementInt(file, "Download") != 0;
				int64_t size = GetTextElementInt(file, "Size", -1);
				unsigned char errorCount = static_cast<unsigned char>(GetTextElementInt(file, "ErrorCount"));
				unsigned int priority = GetTextElementInt(file, "Priority", static_cast<unsigned int>(QueuePriority::normal));

				int dataType = GetTextElementInt(file, "DataType", -1);
				if (dataType == -1) {
					dataType = GetTextElementInt(file, "TransferMode", 1);
				}
				bool binary = dataType != 0;
				int overwrite_action = GetTextElementInt(file, "OverwriteAction", CFileExistsNotification::unknown);

				CServerPath remotePath;
				if (!localFile.empty() && !remoteFile.empty() && remotePath.SetSafePath(safeRemotePath) &&
					size >= -1 && priority < static_cast<int>(QueuePriority::count))
				{
					std::wstring localFileName;
					CLocalPath localPath(localFile, &localFileName);

					if (localFileName.empty()) {
						continue;
					}

					// CServerPath and wxString are reference counted.
					// Save some memory here by re-using the old copy
					if (localPath != previousLocalPath) {
						previousLocalPath = localPath;
					}
					if (previousRemotePath != remotePath) {
						previousRemotePath = remotePath;
					}

					CFileItem* fileItem = new CFileItem(pServerItem, true, download,
						download ? remoteFile : localFileName,
						(remoteFile != localFileName) ? (download ? localFileName : remoteFile) : std::wstring(),
						previousLocalPath, previousRemotePath, size);
					fileItem->SetAscii(!binary);
					fileItem->SetPriorityRaw(QueuePriority(priority));
					fileItem->m_errorCount = errorCount;
					InsertItem(pServerItem, fileItem);

					if (overwrite_action > 0 && overwrite_action < CFileExistsNotification::ACTION_COUNT) {
						fileItem->m_defaultFileExistsAction = (CFileExistsNotification::OverwriteAction)overwrite_action;
					}
				}
			}
			for (auto folder = xServer.child("Folder"); folder; folder = folder.next_sibling("Folder")) {
				CFolderItem* folderItem;

				bool download = GetTextElementInt(folder, "Download") != 0;
				if (download) {
					std::wstring localFile = GetTextElement(folder, "LocalFile");
					if (localFile.empty()) {
						continue;
					}
					folderItem = new CFolderItem(pServerItem, true, CLocalPath(localFile));
				}
				else {
					std::wstring remoteFile = GetTextElement(folder, "RemoteFile");
					std::wstring safeRemotePath = GetTextElement(folder, "RemotePath");
					if (safeRemotePath.empty()) {
						continue;
					}

					CServerPath remotePath;
					if (!remotePath.SetSafePath(safeRemotePath)) {
						continue;
					}
					folderItem = new CFolderItem(pServerItem, true, remotePath, remoteFile);
				}

				unsigned int priority = GetTextElementInt(folder, "Priority", static_cast<int>(QueuePriority::normal));
				if (priority >= static_cast<int>(QueuePriority::count)) {
					delete folderItem;
					continue;
				}
				folderItem->SetPriority(QueuePriority(priority));

				InsertItem(pServerItem, folderItem);
			}

			if (!pServerItem->GetChild(0)) {
				m_itemCount--;
				m_serverList.pop_back();
				delete pServerItem;
			}
			else if (updateSelections) {
				CommitChanges();
			}
		}

		xServer = xServer.next_sibling("Server");
	}

	if (!updateSelections) {
		m_insertionStart = -1;
		m_insertionCount = 0;
		CommitChanges();
	}
	else {
		RefreshListOnly();
	}
}

void CQueueView::OnPostScroll()
{
	if (GetTopItem() != m_lastTopItem) {
		UpdateStatusLinePositions();
	}
}

void CQueueView::OnContextMenu(wxContextMenuEvent&)
{
	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_QUEUE"));
	if (!pMenu) {
		return;
	}

	bool has_selection = HasSelection();

	pMenu->Check(XRCID("ID_PROCESSQUEUE"), IsActive() ? true : false);
	pMenu->Check(XRCID("ID_ACTIONAFTER_NONE"), IsActionAfter(ActionAfterState::None));
	pMenu->Check(XRCID("ID_ACTIONAFTER_SHOW_NOTIFICATION_BUBBLE"), IsActionAfter(ActionAfterState::ShowNotification));
	pMenu->Check(XRCID("ID_ACTIONAFTER_REQUEST_ATTENTION"), IsActionAfter(ActionAfterState::RequestAttention));
	pMenu->Check(XRCID("ID_ACTIONAFTER_CLOSE"), IsActionAfter(ActionAfterState::Close));
	pMenu->Check(XRCID("ID_ACTIONAFTER_RUNCOMMAND"), IsActionAfter(ActionAfterState::RunCommand));
	pMenu->Check(XRCID("ID_ACTIONAFTER_PLAYSOUND"), IsActionAfter(ActionAfterState::PlaySound));
#if defined(__WXMSW__) || defined(__WXMAC__)
	pMenu->Check(XRCID("ID_ACTIONAFTER_REBOOT"), IsActionAfter(ActionAfterState::Reboot));
	pMenu->Check(XRCID("ID_ACTIONAFTER_SHUTDOWN"), IsActionAfter(ActionAfterState::Shutdown));
	pMenu->Check(XRCID("ID_ACTIONAFTER_SLEEP"), IsActionAfter(ActionAfterState::Sleep));
#endif
	pMenu->Enable(XRCID("ID_REMOVE"), has_selection);

	pMenu->Enable(XRCID("ID_PRIORITY"), has_selection);
	pMenu->Enable(XRCID("ID_DEFAULT_FILEEXISTSACTION"), has_selection);
#if defined(__WXMSW__) || defined(__WXMAC__)
	pMenu->Enable(XRCID("ID_ACTIONAFTER"), m_actionAfterWarnDialog == NULL);
#endif

	PopupMenu(pMenu);
	delete pMenu;
}

void CQueueView::OnProcessQueue(wxCommandEvent& event)
{
	SetActive(event.IsChecked());
}

void CQueueView::OnStopAndClear(wxCommandEvent&)
{
	SetActive(false);
	RemoveAll();
}

void CQueueView::OnActionAfter(wxCommandEvent& event)
{
	if (event.GetId() == XRCID("ID_ACTIONAFTER_NONE")) {
		m_actionAfterState = ActionAfterState::None;

#if defined(__WXMSW__) || defined(__WXMAC__)
		if (m_actionAfterWarnDialog) {
			m_actionAfterWarnDialog->Destroy();
			m_actionAfterWarnDialog = 0;
		}
		delete m_actionAfterTimer;
		m_actionAfterTimer = 0;
#endif
	}
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_NONE")) {
		m_actionAfterState = ActionAfterState::None;
	}
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_SHOW_NOTIFICATION_BUBBLE")) {
		m_actionAfterState = ActionAfterState::ShowNotification;
	}
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_REQUEST_ATTENTION")) {
		m_actionAfterState = ActionAfterState::RequestAttention;
	}
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_CLOSE")) {
		m_actionAfterState = ActionAfterState::Close;
	}
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_CLOSE")) {
		m_actionAfterState = ActionAfterState::Close;
	}
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_PLAYSOUND")) {
		m_actionAfterState = ActionAfterState::PlaySound;
	}
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_RUNCOMMAND")) {
		wxTextEntryDialog dlg(m_pMainFrame, _("Please enter the complete path of a program and its arguments. This command will be executed when the queue has finished processing.\nE.g. c:\\somePath\\file.exe under MS Windows or /somePath/file under Unix.\nYou need to properly quote commands and their arguments if they contain spaces."), _("Enter command"));
		dlg.SetValue(COptions::Get()->GetOption(OPTION_QUEUE_COMPLETION_COMMAND));

		if (dlg.ShowModal() == wxID_OK) {
			const wxString &command = dlg.GetValue();
			if (command.empty()) {
				wxMessageBoxEx(_("No command given, aborting."), _("Empty command"), wxICON_ERROR, m_pMainFrame);
			}
			else {
				m_actionAfterState = ActionAfterState::RunCommand;
				COptions::Get()->SetOption(OPTION_QUEUE_COMPLETION_COMMAND, command.ToStdWstring());
			}
		}
	}
#if defined(__WXMSW__) || defined(__WXMAC__)
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_REBOOT"))
		m_actionAfterState = ActionAfterState::Reboot;
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_SHUTDOWN"))
		m_actionAfterState = ActionAfterState::Shutdown;
	else if (event.GetId() == XRCID("ID_ACTIONAFTER_SLEEP"))
		m_actionAfterState = ActionAfterState::Sleep;
#endif

	if (m_actionAfterState != ActionAfterState::Reboot && m_actionAfterState != ActionAfterState::Shutdown && m_actionAfterState != ActionAfterState::Sleep) {
		COptions::Get()->SetOption(OPTION_QUEUE_COMPLETION_ACTION, m_actionAfterState);
	}
}

void CQueueView::RemoveAll()
{
	// This function removes all inactive items and queues active items
	// for removal

	// First, clear all selections
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (GetSelectedItemCount())
#endif
	{
		int item;
		while ((item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
			SetItemState(item, 0, wxLIST_STATE_SELECTED);
		}
	}

	std::vector<CServerItem*> newServerList;
	m_itemCount = 0;
	for (auto iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
		if ((*iter)->TryRemoveAll()) {
			delete *iter;
		}
		else {
			newServerList.push_back(*iter);
			m_itemCount += 1 + (*iter)->GetChildrenCount(true);
		}
	}

	SaveSetItemCount(m_itemCount);

	if (newServerList.empty() && (m_actionAfterState == ActionAfterState::Reboot || m_actionAfterState == ActionAfterState::Shutdown || m_actionAfterState == ActionAfterState::Sleep)) {
		m_actionAfterState = ActionAfterState::None;
	}

	m_serverList = newServerList;
	UpdateStatusLinePositions();

	CalculateQueueSize();

	CheckQueueState();
	RefreshListOnly();
}

void CQueueView::OnRemoveSelected(wxCommandEvent&)
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (!GetSelectedItemCount()) {
		return;
	}
#endif

	std::list<std::pair<long, CQueueItem*>> selectedItems;
	long item = -1;
	long skipTo = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}
		SetItemState(item, 0, wxLIST_STATE_SELECTED);

		if (item <= skipTo) {
			continue;
		}

		CQueueItem* pItem = GetQueueItem(item);
		if (!pItem) {
			continue;
		}

		selectedItems.push_front(std::make_pair(item, pItem));

		if (pItem->GetType() == QueueItemType::Server) {
			// Server selected. Don't process individual files, continue with the next server
			skipTo = item + pItem->GetChildrenCount(true);
		}
	}

	m_waitStatusLineUpdate = true;

	while (!selectedItems.empty()) {
		auto selectedItem = selectedItems.front();
		CQueueItem* pItem = selectedItem.second;
		selectedItems.pop_front();

		if (pItem->GetType() == QueueItemType::Status) {
			continue;
		}
		else if (pItem->GetType() == QueueItemType::Server) {
			CServerItem* pServer = (CServerItem*)pItem;
			StopItem(pServer, false);

			// Server items get deleted automatically if all children are gone
			continue;
		}
		else if (pItem->GetType() == QueueItemType::File ||
				 pItem->GetType() == QueueItemType::Folder)
		{
			CFileItem* pFile = (CFileItem*)pItem;
			if (pFile->IsActive()) {
				pFile->set_pending_remove(true);
				StopItem(pFile);
				continue;
			}
		}

		CQueueItem* pTopLevelItem = pItem->GetTopLevelItem();
		if (!pTopLevelItem->GetChild(1)) {
			// Parent will get deleted
			// If next selected item is parent, remove it from list
			if (!selectedItems.empty() && selectedItems.front().second == pTopLevelItem) {
				selectedItems.pop_front();
			}
		}

		int topItemIndex = GetItemIndex(pTopLevelItem);

		// Finding the child position is O(n) in the worst case, making deletion quadratic. However we already know item's displayed position, use it as guide.
		// Remark: I suppose this could be further improved by using the displayed position directly, but probably isn't worth the effort.
		bool forward = selectedItem.first < (topItemIndex + static_cast<int>(pTopLevelItem->GetChildrenCount(false)) / 2);
		RemoveItem(pItem, true, false, false, forward);
	}
	DisplayNumberQueuedFiles();
	DisplayQueueSize();
	SaveSetItemCount(m_itemCount);

	m_waitStatusLineUpdate = false;
	UpdateStatusLinePositions();

	RefreshListOnly();
}

bool CQueueView::StopItem(CFileItem* item)
{
	if (!item->IsActive()) {
		return true;
	}

	((CServerItem*)item->GetTopLevelItem())->QueueImmediateFile(item);

	if (item->m_pEngineData->state == t_EngineData::waitprimary) {
		ResetReason reason;
		if (item->m_pEngineData->pItem && item->m_pEngineData->pItem->pending_remove()) {
			reason = remove;
		}
		else {
			reason = reset;
		}
		if (item->m_pEngineData->pItem) {
			item->m_pEngineData->pItem->SetStatusMessage(CFileItem::none);
		}
		ResetEngine(*item->m_pEngineData, reason);
		return true;
	}
	else {
		item->m_pEngineData->pEngine->Cancel();
		return false;
	}
}

bool CQueueView::StopItem(CServerItem* pServerItem, bool updateSelections)
{
	std::vector<CQueueItem*> const items = pServerItem->GetChildren();
	int const removedAtFront = pServerItem->GetRemovedAtFront();

	for (int i = static_cast<int>(items.size()) - 1; i >= removedAtFront; --i) {
		CQueueItem* pItem = items[i];
		if (pItem->GetType() == QueueItemType::File ||
			 pItem->GetType() == QueueItemType::Folder)
		{
			CFileItem* pFile = (CFileItem*)pItem;
			if (pFile->IsActive()) {
				pFile->set_pending_remove(true);
				StopItem(pFile);
				continue;
			}
		}
		else {
			// Unknown type, shouldn't be here.
			wxASSERT(false);
		}

		if (RemoveItem(pItem, true, false, updateSelections, false)) {
			DisplayNumberQueuedFiles();
			SaveSetItemCount(m_itemCount);
			return true;
		}
	}
	DisplayNumberQueuedFiles();
	SaveSetItemCount(m_itemCount);

	return false;
}

void CQueueView::SetDefaultFileExistsAction(CFileExistsNotification::OverwriteAction action, const TransferDirection direction)
{
	for (auto iter = m_serverList.begin(); iter != m_serverList.end(); ++iter)
		(*iter)->SetDefaultFileExistsAction(action, direction);
}

void CQueueView::OnSetDefaultFileExistsAction(wxCommandEvent &)
{
	if (!HasSelection())
		return;

	CDefaultFileExistsDlg dlg;
	if (!dlg.Load(this, true))
		return;

	// Get current default action for the item
	CFileExistsNotification::OverwriteAction downloadAction = CFileExistsNotification::unknown;
	CFileExistsNotification::OverwriteAction uploadAction = CFileExistsNotification::unknown;
	bool has_upload = false;
	bool has_download = false;
	bool download_unknown = false;
	bool upload_unknown = false;

	long item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		CQueueItem* pItem = GetQueueItem(item);
		if (!pItem)
			continue;

		switch (pItem->GetType())
		{
		case QueueItemType::File:
			{
				CFileItem *pFileItem = (CFileItem*)pItem;
				if (pFileItem->Download())
				{
					if (downloadAction == CFileExistsNotification::unknown)
						downloadAction = pFileItem->m_defaultFileExistsAction;
					else if (pFileItem->m_defaultFileExistsAction != downloadAction)
						download_unknown = true;
					has_download = true;
				}
				else
				{
					if (uploadAction == CFileExistsNotification::unknown)
						uploadAction = pFileItem->m_defaultFileExistsAction;
					else if (pFileItem->m_defaultFileExistsAction != uploadAction)
						upload_unknown = true;
					has_upload = true;
				}
			}
			break;
		case QueueItemType::Server:
			{
				download_unknown = true;
				upload_unknown = true;
				has_download = true;
				has_upload = true;
			}
			break;
		default:
			break;
		}
	}
	if (download_unknown)
		downloadAction = CFileExistsNotification::unknown;
	if (upload_unknown)
		uploadAction = CFileExistsNotification::unknown;

	if (!dlg.Run(has_download ? &downloadAction : 0, has_upload ? &uploadAction : 0))
		return;

	item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		CQueueItem* pItem = GetQueueItem(item);
		if (!pItem)
			continue;

		switch (pItem->GetType())
		{
		case QueueItemType::File:
			{
				CFileItem *pFileItem = (CFileItem*)pItem;
				if (pFileItem->Download())
				{
					if (!has_download)
						break;
					pFileItem->m_defaultFileExistsAction = downloadAction;
				}
				else
				{
					if (!has_upload)
						break;
					pFileItem->m_defaultFileExistsAction = uploadAction;
				}
			}
			break;
		case QueueItemType::Server:
			{
				CServerItem *pServerItem = (CServerItem*)pItem;
				if (has_download)
					pServerItem->SetDefaultFileExistsAction(downloadAction, TransferDirection::download);
				if (has_upload)
					pServerItem->SetDefaultFileExistsAction(uploadAction, TransferDirection::upload);
			}
			break;
		default:
			break;
		}
	}
}

t_EngineData* CQueueView::GetIdleEngine(const CServer* pServer, bool allowTransient)
{
	wxASSERT(!allowTransient || pServer);

	t_EngineData* pFirstIdle = 0;

	int transient = 0;
	for( unsigned int i = 0; i < m_engineData.size(); i++) {
		if (m_engineData[i]->active)
			continue;

		if (m_engineData[i]->transient) {
			++transient;
			if( !allowTransient )
				continue;
		}

		if (!pServer)
			return m_engineData[i];

		if (m_engineData[i]->pEngine->IsConnected() && m_engineData[i]->lastServer == *pServer)
			return m_engineData[i];

		if (!pFirstIdle)
			pFirstIdle = m_engineData[i];
	}

	if( !pFirstIdle ) {
		// Check whether we can create another engine
		const int newEngineCount = COptions::Get()->GetOptionVal(OPTION_NUMTRANSFERS);
		if (newEngineCount > static_cast<int>(m_engineData.size()) - transient) {
			pFirstIdle = new t_EngineData;
			pFirstIdle->pEngine = new CFileZillaEngine(m_pMainFrame->GetEngineContext(), *this);

			m_engineData.push_back(pFirstIdle);
		}
	}

	return pFirstIdle;
}


t_EngineData* CQueueView::GetEngineData(const CFileZillaEngine* pEngine)
{
	for (unsigned int i = 0; i < m_engineData.size(); ++i)
		if (m_engineData[i]->pEngine == pEngine)
			return m_engineData[i];

	return 0;
}


void CQueueView::TryRefreshListings()
{
	if (m_quit) {
		return;
	}

	const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
	for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter) {
		CState* pState = *iter;

		const CServer* const pServer = pState->GetServer();
		if (!pServer) {
			continue;
		}

		const CDirectoryListing* const pListing = pState->GetRemoteDir().get();
		if (!pListing) {
			continue;
		}

		// See if there's an engine that is already listing
		unsigned int i;
		for (i = 0; i < m_engineData.size(); ++i) {
			if (!m_engineData[i]->active || m_engineData[i]->state != t_EngineData::list) {
				continue;
			}

			if (m_engineData[i]->lastServer != *pServer) {
				continue;
			}

			// This engine is already listing a directory on the current server
			break;
		}
		if (i != m_engineData.size()) {
			continue;
		}

		if (m_last_refresh_server == *pServer && m_last_refresh_path == pListing->path &&
			m_last_refresh_listing_time == pListing->m_firstListTime)
		{
			// Do not try to refresh same directory multiple times
			continue;
		}

		t_EngineData* pEngineData = GetIdleEngine(pServer);
		if (!pEngineData) {
			continue;
		}

		if (!pEngineData->pEngine->IsConnected() || pEngineData->lastServer != *pServer) {
			continue;
		}

		m_last_refresh_server = *pServer;
		m_last_refresh_path = pListing->path;
		m_last_refresh_listing_time = pListing->m_firstListTime;

		CListCommand command(pListing->path, _T(""), LIST_FLAG_AVOID);
		int res = pEngineData->pEngine->Execute(command);
		if (res != FZ_REPLY_WOULDBLOCK) {
			continue;
		}

		pEngineData->active = true;
		pEngineData->state = t_EngineData::list;
		m_activeCount++;

		break;
	}
}

void CQueueView::OnAskPassword()
{
	while (!m_waitingForPassword.empty()) {
		const CFileZillaEngine* const pEngine = m_waitingForPassword.front();

		t_EngineData* pEngineData = GetEngineData(pEngine);
		if (!pEngineData) {
			m_waitingForPassword.pop_front();
			continue;
		}

		if (pEngineData->state != t_EngineData::askpassword) {
			m_waitingForPassword.pop_front();
			continue;
		}

		if (CLoginManager::Get().GetPassword(pEngineData->lastServer, false)) {
			pEngineData->state = t_EngineData::connect;
			SendNextCommand(*pEngineData);
		}
		else
			ResetEngine(*pEngineData, remove);

		m_waitingForPassword.pop_front();
	}
}

void CQueueView::UpdateItemSize(CFileItem* pItem, int64_t size)
{
	wxASSERT(pItem);

	int64_t const oldSize = pItem->GetSize();
	if (size == oldSize)
		return;

	if (oldSize < 0) {
		wxASSERT(m_filesWithUnknownSize);
		if (m_filesWithUnknownSize)
			--m_filesWithUnknownSize;
	}
	else {
		wxASSERT(m_totalQueueSize >= oldSize);
		if (m_totalQueueSize > oldSize)
			m_totalQueueSize -= oldSize;
		else
			m_totalQueueSize = 0;
	}

	if (size < 0)
		++m_filesWithUnknownSize;
	else
		m_totalQueueSize += size;

	pItem->SetSize(size);

	DisplayQueueSize();
}

void CQueueView::AdvanceQueue(bool refresh /*=true*/)
{
	static bool insideAdvanceQueue = false;
	if (insideAdvanceQueue)
		return;

	insideAdvanceQueue = true;
	while (TryStartNextTransfer()) {
	}

	// Set timer for connected, idle engines
	for (unsigned int i = 0; i < m_engineData.size(); ++i) {
		if (m_engineData[i]->active || m_engineData[i]->transient)
			continue;

		if (m_engineData[i]->m_idleDisconnectTimer) {
			if (m_engineData[i]->pEngine->IsConnected())
				continue;

			delete m_engineData[i]->m_idleDisconnectTimer;
			m_engineData[i]->m_idleDisconnectTimer = 0;
		}
		else {
			if (!m_engineData[i]->pEngine->IsConnected())
				continue;

			m_engineData[i]->m_idleDisconnectTimer = new wxTimer(this);
			m_engineData[i]->m_idleDisconnectTimer->Start(60000, true);
		}
	}

	if (refresh)
		RefreshListOnly(false);

	insideAdvanceQueue = false;

	CheckQueueState();
}

void CQueueView::InsertItem(CServerItem* pServerItem, CQueueItem* pItem)
{
	CQueueViewBase::InsertItem(pServerItem, pItem);

	if (pItem->GetType() == QueueItemType::File) {
		CFileItem* pFileItem = (CFileItem*)pItem;

		int64_t const size = pFileItem->GetSize();
		if (size < 0)
			m_filesWithUnknownSize++;
		else if (size > 0)
			m_totalQueueSize += size;
	}
}

void CQueueView::CommitChanges()
{
	CQueueViewBase::CommitChanges();

	DisplayQueueSize();
}

void CQueueView::OnTimer(wxTimerEvent& event)
{
	const int id = event.GetId();
	if (id == -1) {
		return;
	}
#if defined(__WXMSW__) || defined(__WXMAC__)
	if (id == m_actionAfterTimerId) {
		OnActionAfterTimerTick();
		return;
	}
#endif

	if (id == m_resize_timer.GetId()) {
		UpdateStatusLinePositions();
		return;
	}

	for (auto & pData : m_engineData) {
		if (pData->m_idleDisconnectTimer && !pData->m_idleDisconnectTimer->IsRunning()) {
			delete pData->m_idleDisconnectTimer;
			pData->m_idleDisconnectTimer = 0;

			if (pData->pEngine->IsConnected()) {
				pData->pEngine->Execute(CDisconnectCommand());
			}
		}
	}

	event.Skip();
}

void CQueueView::DeleteEngines()
{
	for (auto & engineData : m_engineData) {
		delete engineData;
	}
	m_engineData.clear();
}

void CQueueView::OnSetPriority(wxCommandEvent& event)
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (!GetSelectedItemCount())
		return;
#endif

	QueuePriority priority;

	const int id = event.GetId();
	if (id == XRCID("ID_PRIORITY_LOWEST"))
		priority = QueuePriority::lowest;
	else if (id == XRCID("ID_PRIORITY_LOW"))
		priority = QueuePriority::low;
	else if (id == XRCID("ID_PRIORITY_HIGH"))
		priority = QueuePriority::high;
	else if (id == XRCID("ID_PRIORITY_HIGHEST"))
		priority = QueuePriority::highest;
	else
		priority = QueuePriority::normal;


	CQueueItem* pSkip = 0;
	long item = -1;
	while (-1 != (item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED))) {
		CQueueItem* pItem = GetQueueItem(item);
		if (!pItem)
			continue;

		if (pItem->GetType() == QueueItemType::Server)
			pSkip = pItem;
		else if (pItem->GetTopLevelItem() == pSkip)
			continue;
		else
			pSkip = 0;

		pItem->SetPriority(priority);
	}

	RefreshListOnly();
}

void CQueueView::OnExclusiveEngineRequestGranted(wxCommandEvent& event)
{
	CFileZillaEngine* pEngine = 0;
	CState* pState = 0;
	CCommandQueue* pCommandQueue = 0;
	const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
	for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter)
	{
		pState = *iter;
		pCommandQueue = pState->m_pCommandQueue;
		if (!pCommandQueue)
			continue;

		pEngine = pCommandQueue->GetEngineExclusive(event.GetId());
		if (!pEngine)
			continue;

		break;
	}

	if (!pState || !pCommandQueue || !pEngine)
		return;

	t_EngineData* pEngineData = GetEngineData(pEngine);
	wxASSERT(!pEngineData || pEngineData->transient);
	if (!pEngineData || !pEngineData->transient || !pEngineData->active)
	{
		pCommandQueue->ReleaseEngine();
		return;
	}

	wxASSERT(pEngineData->state == t_EngineData::waitprimary);
	if (pEngineData->state != t_EngineData::waitprimary)
		return;

	CServerItem* pServerItem = (CServerItem*)pEngineData->pItem->GetParent();

	const CServer* pCurrentServer = pState->GetServer();

	wxASSERT(pServerItem);

	if (!pCurrentServer || *pCurrentServer != pServerItem->GetServer())
	{
		if (pState->m_pCommandQueue)
			pState->m_pCommandQueue->ReleaseEngine();
		ResetEngine(*pEngineData, retry);
		return;
	}

	if (pEngineData->pItem->GetType() == QueueItemType::File)
		pEngineData->state = t_EngineData::transfer;
	else
		pEngineData->state = t_EngineData::mkdir;

	pEngineData->pEngine = pEngine;

	SendNextCommand(*pEngineData);
}

bool CQueueView::IsActionAfter(ActionAfterState::type state)
{
	return m_actionAfterState == state;
}

void CQueueView::ActionAfter(bool warned)
{
	if (m_quit) {
		return;
	}

	auto blocker = m_actionAfterBlocker.lock();
	if (blocker) {
		blocker->trigger_ = true;
		return;
	}

	switch (m_actionAfterState) {
		case ActionAfterState::ShowNotification:
		{
			wxString const title = _("Transfers finished");
			wxString msg;
			int const failed_count = m_pQueue->GetQueueView_Failed()->GetFileCount();
			if (failed_count != 0) {
				wxString fmt = wxPLURAL("All transfers have finished. %d file could not be transferred.", "All transfers have finished. %d files could not be transferred.", failed_count);
				msg = wxString::Format(fmt, failed_count);
			}
			else
				msg = _("All files have been successfully transferred");

#if WITH_LIBDBUS
			if (!m_desktop_notification)
				m_desktop_notification = std::make_unique<CDesktopNotification>();
			m_desktop_notification->Notify(title, msg, (failed_count > 0) ? _T("transfer.error") : _T("transfer.complete"));
#elif defined(__WXGTK__) || defined(__WXMSW__)
			m_desktop_notification = std::make_unique<wxNotificationMessage>();
			m_desktop_notification->SetTitle(title);
			m_desktop_notification->SetMessage(msg);
			m_desktop_notification->Show(5);
#endif
			break;
		}
		case ActionAfterState::RequestAttention:
		{
			int const failed_count = m_pQueue->GetQueueView_Failed()->GetFileCount();
			m_pMainFrame->RequestUserAttention(failed_count ? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO);
			break;
		}
		case ActionAfterState::Close:
		{
			m_pMainFrame->Close();
			break;
		}
		case ActionAfterState::RunCommand:
		{
			wxString cmd = COptions::Get()->GetOption(OPTION_QUEUE_COMPLETION_COMMAND);
			if (!cmd.empty()) {
				wxExecute(cmd);
			}
			break;
		}
		case ActionAfterState::PlaySound:
		{
			wxSound sound(wxGetApp().GetResourceDir().GetPath() + _T("finished.wav"));
			sound.Play(wxSOUND_ASYNC);
			break;
		}
#ifdef __WXMSW__
		case ActionAfterState::Reboot:
		case ActionAfterState::Shutdown:
			if (!warned) {
				ActionAfterWarnUser(m_actionAfterState);
				return;
			}
			else {
				wxShutdown((m_actionAfterState == ActionAfterState::Reboot) ? wxSHUTDOWN_REBOOT : wxSHUTDOWN_POWEROFF);
				m_actionAfterState = ActionAfterState::None;
			}
			break;
		case ActionAfterState::Sleep:
			if (!warned) {
				ActionAfterWarnUser(m_actionAfterState);
				return;
			}
			else {
				SetSuspendState(false, false, true);
				m_actionAfterState = ActionAfterState::None;
			}
			break;
#elif defined(__WXMAC__)
		case ActionAfterState::Reboot:
		case ActionAfterState::Shutdown:
		case ActionAfterState::Sleep:
			if (!warned) {
				ActionAfterWarnUser(m_actionAfterState);
				return;
			}
			else {
				wxString action;
				if( m_actionAfterState == ActionAfterState::Reboot )
					action = _T("restart");
				else if( m_actionAfterState == ActionAfterState::Shutdown )
					action = _T("shut down");
				else
					action = _T("sleep");
				wxExecute(_T("osascript -e 'tell application \"System Events\" to ") + action + _T("'"));
				m_actionAfterState = ActionAfterState::None;
			}
			break;
#else
		(void)warned;
#endif
		default:
			break;

	}
}

#if defined(__WXMSW__) || defined(__WXMAC__)
void CQueueView::ActionAfterWarnUser(ActionAfterState::type s)
{
	if (m_actionAfterWarnDialog != NULL)
		return;

	wxString message;
	wxString label;
	if(s == ActionAfterState::Shutdown) {
		message = _("The system will soon shut down unless you click Cancel.");
		label = _("Shutdown now");
	}
	else if(s == ActionAfterState::Reboot) {
		message = _("The system will soon reboot unless you click Cancel.");
		label = _("Reboot now");
	}
	else {
		message = _("Your computer will suspend unless you click Cancel.");
		label = _("Suspend now");
	}

	m_actionAfterWarnDialog = new wxProgressDialog(_("Queue has been fully processed"), message, 150, m_pMainFrame, wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_CAN_SKIP | wxPD_APP_MODAL);

	// Magic id from wxWidgets' src/generic/propdlgg.cpp
	wxWindow* pSkip = m_actionAfterWarnDialog->FindWindow(32000);
	if (pSkip) {
		pSkip->SetLabel(label);
	}

	CWrapEngine engine;
	engine.WrapRecursive(m_actionAfterWarnDialog, 2);
	m_actionAfterWarnDialog->CentreOnParent();
	m_actionAfterWarnDialog->SetFocus();
	m_pMainFrame->RequestUserAttention(wxUSER_ATTENTION_ERROR);

	wxASSERT(!m_actionAfterTimer);
	m_actionAfterTimer = new wxTimer(this, m_actionAfterTimerId);
	m_actionAfterTimerId = m_actionAfterTimer->GetId();
	m_actionAfterTimer->Start(100, wxTIMER_CONTINUOUS);
}

void CQueueView::OnActionAfterTimerTick()
{
	if (!m_actionAfterWarnDialog) {
		delete m_actionAfterTimer;
		m_actionAfterTimer = 0;
		return;
	}

	bool skipped = false;
	if (m_actionAfterTimerCount > 150) {
		m_actionAfterWarnDialog->Destroy();
		m_actionAfterWarnDialog = 0;
		delete m_actionAfterTimer;
		m_actionAfterTimer = 0;
		ActionAfter(true);
	}
	else if (!m_actionAfterWarnDialog->Update(m_actionAfterTimerCount++, _T(""), &skipped)) {
		// User has pressed cancel!
		m_actionAfterState = ActionAfterState::None; // resetting to disabled
		m_actionAfterWarnDialog->Destroy();
		m_actionAfterWarnDialog = 0;
		delete m_actionAfterTimer;
		m_actionAfterTimer = 0;
	}
	else if (skipped) {
		m_actionAfterWarnDialog->Destroy();
		m_actionAfterWarnDialog = 0;
		delete m_actionAfterTimer;
		m_actionAfterTimer = 0;
		ActionAfter(true);
	}
}
#endif

bool CQueueView::SwitchEngine(t_EngineData** ppEngineData)
{
	if (m_engineData.size() < 2) {
		return false;
	}

	t_EngineData* pEngineData = *ppEngineData;
	for (auto & pNewEngineData : m_engineData) {
		if (pNewEngineData == pEngineData) {
			continue;
		}

		if (pNewEngineData->active || pNewEngineData->transient) {
			continue;
		}

		if (pNewEngineData->lastServer != pEngineData->lastServer) {
			continue;
		}

		if (!pNewEngineData->pEngine->IsConnected()) {
			continue;
		}

		wxASSERT(!pNewEngineData->pItem);
		pNewEngineData->pItem = pEngineData->pItem;
		pNewEngineData->pItem->m_pEngineData = pNewEngineData;
		pEngineData->pItem = 0;

		pNewEngineData->active = true;
		pEngineData->active = false;

		delete pNewEngineData->m_idleDisconnectTimer;
		pNewEngineData->m_idleDisconnectTimer = 0;

		// Swap status line
		CStatusLineCtrl* pOldStatusLineCtrl = pNewEngineData->pStatusLineCtrl;
		pNewEngineData->pStatusLineCtrl = pEngineData->pStatusLineCtrl;
		if (pNewEngineData->pStatusLineCtrl) {
			pNewEngineData->pStatusLineCtrl->SetEngineData(pNewEngineData);
		}
		if (pOldStatusLineCtrl) {
			pEngineData->pStatusLineCtrl = pOldStatusLineCtrl;
			pEngineData->pStatusLineCtrl->SetEngineData(pEngineData);
		}

		// Set new state
		if (pNewEngineData->pItem->GetType() == QueueItemType::File) {
			pNewEngineData->state = t_EngineData::transfer;
		}
		else {
			pNewEngineData->state = t_EngineData::mkdir;
		}
		if (pNewEngineData->pStatusLineCtrl) {
			pNewEngineData->pStatusLineCtrl->ClearTransferStatus();
		}

		pEngineData->state = t_EngineData::none;

		*ppEngineData = pNewEngineData;
		return true;
	}

	return false;
}

bool CQueueView::IsOtherEngineConnected(t_EngineData* pEngineData)
{
	for (auto iter = m_engineData.begin(); iter != m_engineData.end(); ++iter)
	{
		t_EngineData* current = *iter;

		if (current == pEngineData)
			continue;

		if (!current->pEngine)
			continue;

		if (current->lastServer != pEngineData->lastServer)
			continue;

		if (current->pEngine->IsConnected())
			return true;
	}

	return false;
}

void CQueueView::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_NUMPAD_DELETE)
	{
		wxCommandEvent cmdEvt;
		OnRemoveSelected(cmdEvt);
	}
	else
		event.Skip();
}

int CQueueView::GetLineHeight()
{
	if (m_line_height != -1)
		return m_line_height;

	if (!GetItemCount())
		return 20;

	wxRect rect;
	if (!GetItemRect(0, rect))
		return 20;

	m_line_height = rect.GetHeight();

#ifdef __WXMSW__
	m_header_height = rect.y + GetScrollPos(wxVERTICAL) * m_line_height;
#endif

	return m_line_height;
}

void CQueueView::OnSize(wxSizeEvent& event)
{
	if (!m_resize_timer.IsRunning())
		m_resize_timer.Start(250, true);

	event.Skip();
}

void CQueueView::RenameFileInTransfer(CFileZillaEngine *pEngine, const wxString& newName, bool local)
{
	t_EngineData* const pEngineData = GetEngineData(pEngine);
	if (!pEngineData || !pEngineData->pItem)
		return;

	if (pEngineData->pItem->GetType() != QueueItemType::File)
		return;

	CFileItem* pFile = (CFileItem*)pEngineData->pItem;
	if (local)
	{
		wxFileName fn(pFile->GetLocalPath().GetPath(), pFile->GetLocalFile());
		fn.SetFullName(newName);
		pFile->SetTargetFile(fn.GetFullName());
	}
	else
		pFile->SetTargetFile(newName);

	RefreshItem(pFile);
}

std::wstring CQueueView::ReplaceInvalidCharacters(std::wstring const& filename)
{
	if (!COptions::Get()->GetOptionVal(OPTION_INVALID_CHAR_REPLACE_ENABLE)) {
		return filename;
	}

	const wxChar replace = COptions::Get()->GetOption(OPTION_INVALID_CHAR_REPLACE)[0];

	wxString result;
	{
		wxStringBuffer start(result, filename.size() + 1);
		wxChar* buf = start;

		const wxChar* p = filename.c_str();
		while (*p) {
			const wxChar c = *p;
			switch (c)
			{
			case '/':
	#ifdef __WXMSW__
			case '\\':
			case ':':
			case '*':
			case '?':
			case '"':
			case '<':
			case '>':
			case '|':
	#endif
				if (replace)
					*buf++ = replace;
				break;
			default:
	#ifdef __WXMSW__
				if (c < 0x20)
					*buf++ = replace;
				else
	#endif
				{
					*buf++ = c;
				}
			}
			p++;
		}
		*buf = 0;
	}

	return result.ToStdWstring();
}

wxFileOffset CQueueView::GetCurrentDownloadSpeed()
{
	wxFileOffset speed = GetCurrentSpeed(true, false);
	return speed;
}

wxFileOffset CQueueView::GetCurrentUploadSpeed()
{
	wxFileOffset speed = GetCurrentSpeed(false, true);
	return speed;
}

wxFileOffset CQueueView::GetCurrentSpeed(bool countDownload, bool countUpload)
{
	wxFileOffset totalSpeed = 0;

	for (auto pCtrl : m_statusLineList) {
		const CFileItem *pItem = pCtrl->GetItem();
		bool isDownload = pItem->Download();

		if ((isDownload && countDownload) || (!isDownload && countUpload)) {
			wxFileOffset speed = pCtrl->GetMomentarySpeed();
			if (speed > 0) {
				totalSpeed += speed;
			}
		}
	}

	return totalSpeed;
}

void CQueueView::ReleaseExclusiveEngineLock(CFileZillaEngine* pEngine)
{
	wxASSERT(pEngine);
	if (!pEngine) {
		return;
	}

	const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
	for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter) {
		CState* pState = *iter;
		if (pState->m_pEngine != pEngine) {
			continue;
		}
		CCommandQueue *pCommandQueue = pState->m_pCommandQueue;
		if (pCommandQueue) {
			pCommandQueue->ReleaseEngine();
		}

		break;
	}
}

#ifdef __WXMSW__

#ifndef WM_DWMCOMPOSITIONCHANGED
#define WM_DWMCOMPOSITIONCHANGED		0x031E
#endif // WM_DWMCOMPOSITIONCHANGED

WXLRESULT CQueueView::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
	if (nMsg == WM_DWMCOMPOSITIONCHANGED || nMsg == WM_THEMECHANGED) {
		m_line_height = -1;
		if (!m_resize_timer.IsRunning()) {
			m_resize_timer.Start(250, true);
		}
	}
	else if (nMsg == WM_LBUTTONDOWN) {
		// If clicking a partially selected item, Windows starts an internal timer with the double-click interval (as seen in the
		// disassembly). After the timer expires, the given item is selected. But there's a huge bug in Windows: We don't get
		// notified about this change in scroll position in any way (verified using Spy++), so on left button down, start our
		// own timer with a slightly higher interval.
		if (!m_resize_timer.IsRunning()) {
			m_resize_timer.Start(GetDoubleClickTime() + 5, true);
		}
	}
	return CQueueViewBase::MSWWindowProc(nMsg, wParam, lParam);
}
#endif

void CQueueView::OnOptionsChanged(changed_options_t const&)
{
	if (m_activeMode) {
		AdvanceQueue();
	}
}

std::shared_ptr<CActionAfterBlocker> CQueueView::GetActionAfterBlocker()
{
	auto ret = m_actionAfterBlocker.lock();
	if (!ret) {
		ret = std::make_shared<CActionAfterBlocker>(*this);
		m_actionAfterBlocker = ret;
	}

	return ret;
}


CActionAfterBlocker::~CActionAfterBlocker()
{
	if (trigger_ && !queueView_.IsActive()) {
		queueView_.ActionAfter();
	}
}
