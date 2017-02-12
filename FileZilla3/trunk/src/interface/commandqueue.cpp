#include <filezilla.h>
#include "commandqueue.h"
#include "Mainfrm.h"
#include "state.h"
#include "remote_recursive_operation.h"
#include "loginmanager.h"
#include "queue.h"
#include "RemoteListView.h"

#include <algorithm>

DEFINE_EVENT_TYPE(fzEVT_GRANTEXCLUSIVEENGINEACCESS)

int CCommandQueue::m_requestIdCounter = 0;

CCommandQueue::CCommandQueue(CFileZillaEngine *pEngine, CMainFrame* pMainFrame, CState& state)
	: m_state(state)
{
	m_pEngine = pEngine;
	m_pMainFrame = pMainFrame;
	m_exclusiveEngineRequest = false;
	m_exclusiveEngineLock = false;
	m_requestId = 0;
}

CCommandQueue::~CCommandQueue()
{
}

bool CCommandQueue::Idle(command_origin origin) const
{
	if (m_exclusiveEngineLock) {
		return false;
	}

	if (origin == any) {
		return m_CommandList.empty();
	}

	return std::find_if(m_CommandList.begin(), m_CommandList.end(), [origin](CommandInfo const& c) { return c.origin == origin; }) == m_CommandList.end();
}

void CCommandQueue::ProcessCommand(CCommand *pCommand, CCommandQueue::command_origin origin)
{
	wxASSERT(origin != any);
	if (m_quit) {
		delete pCommand;
		return;
	}

	m_CommandList.emplace_back(origin, std::unique_ptr<CCommand>(pCommand));
	if (m_CommandList.size() == 1) {
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		ProcessNextCommand();
	}
}

void CCommandQueue::ProcessNextCommand()
{
	if (m_inside_commandqueue)
		return;

	if (m_exclusiveEngineLock)
		return;

	if (m_pEngine->IsBusy())
		return;

	++m_inside_commandqueue;

	if (m_CommandList.empty()) {
		// Possible sequence of events:
		// - Engine emits listing and operation finished
		// - Connection gets terminated
		// - Interface cannot obtain listing since not connected
		// - Yet getting operation successful
		// To keep things flowing, we need to advance the recursive operation.
		m_state.GetRemoteRecursiveOperation()->NextOperation();
	}

	while (!m_CommandList.empty()) {
		auto const& commandInfo = m_CommandList.front();

		int res = m_pEngine->Execute(*commandInfo.command);
		ProcessReply(res, commandInfo.command->GetId());
		if (res == FZ_REPLY_WOULDBLOCK) {
			break;
		}
	}

	--m_inside_commandqueue;

	if (m_CommandList.empty()) {
		if (m_exclusiveEngineRequest)
			GrantExclusiveEngineRequest();
		else
			m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);

		if (!m_state.SuccessfulConnect()) {
			m_state.SetSite(Site());
		}
	}
}

bool CCommandQueue::Cancel()
{
	if (m_exclusiveEngineLock)
		return false;

	if (m_CommandList.empty())
		return true;

	m_CommandList.erase(++m_CommandList.begin(), m_CommandList.end());

	if (!m_pEngine)	{
		m_CommandList.clear();
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		return true;
	}

	int res = m_pEngine->Cancel();
	if (res == FZ_REPLY_WOULDBLOCK)
		return false;
	else {
		m_CommandList.clear();
		m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
		return true;
	}
}

void CCommandQueue::Finish(std::unique_ptr<COperationNotification> && pNotification)
{
	if (m_exclusiveEngineLock) {
		m_pMainFrame->GetQueue()->ProcessNotification(m_pEngine, std::move(pNotification));
		return;
	}

	ProcessReply(pNotification->nReplyCode, pNotification->commandId);
}

void CCommandQueue::ProcessReply(int nReplyCode, Command commandId)
{
	if (nReplyCode == FZ_REPLY_WOULDBLOCK) {
		return;
	}
	if (nReplyCode & FZ_REPLY_DISCONNECTED) {
		if (commandId == Command::none && !m_CommandList.empty()) {
			// Pending event, has no relevance during command execution
			return;
		}
		if (nReplyCode & FZ_REPLY_PASSWORDFAILED)
			CLoginManager::Get().CachedPasswordFailed(*m_state.GetServer());
	}

	if (m_CommandList.empty()) {
		return;
	}

	if (commandId != Command::connect &&
		commandId != Command::disconnect &&
		(nReplyCode & FZ_REPLY_CANCELED) != FZ_REPLY_CANCELED)
	{
		bool reconnect = false;
		if (nReplyCode == FZ_REPLY_NOTCONNECTED) {
			reconnect = true;
		}
		else if (nReplyCode & FZ_REPLY_DISCONNECTED) {
			auto & info = m_CommandList.front();
			if (!info.didReconnect) {
				info.didReconnect = true;
				reconnect = true;
			}
		}

		if (reconnect) {
			// Try automatic reconnect
			const CServer* pServer = m_state.GetServer();
			if (pServer) {
				m_CommandList.emplace_front(normal, std::make_unique<CConnectCommand>(*pServer));
				ProcessNextCommand();
				return;
			}
		}
	}

	++m_inside_commandqueue;

	auto const& commandInfo = m_CommandList.front();

	if (commandInfo.command->GetId() == Command::list && nReplyCode != FZ_REPLY_OK) {
		if ((nReplyCode & FZ_REPLY_LINKNOTDIR) == FZ_REPLY_LINKNOTDIR) {
			// Symbolic link does not point to a directory. Either points to file
			// or is completely invalid
			CListCommand* pListCommand = static_cast<CListCommand*>(commandInfo.command.get());
			wxASSERT(pListCommand->GetFlags() & LIST_FLAG_LINK);

			m_state.LinkIsNotDir(pListCommand->GetPath(), pListCommand->GetSubDir());
		}
		else {
			if (commandInfo.origin == recursiveOperation) {
				// Let the recursive operation handler know if a LIST command failed,
				// so that it may issue the next command in recursive operations.
				m_state.GetRemoteRecursiveOperation()->ListingFailed(nReplyCode);
			}
			else {
				m_state.ListingFailed(nReplyCode);
			}
		}
		m_CommandList.pop_front();
	}
	else if (nReplyCode == FZ_REPLY_ALREADYCONNECTED && commandInfo.command->GetId() == Command::connect) {
		m_CommandList.emplace_front(normal, std::make_unique<CDisconnectCommand>());
	}
	else if (commandInfo.command->GetId() == Command::connect && nReplyCode != FZ_REPLY_OK) {
		// Remove pending events
		auto it = ++m_CommandList.begin();
		while (it != m_CommandList.end() && it->command->GetId() != Command::connect) {
			++it;
		}
		m_CommandList.erase(m_CommandList.begin(), it);

		// If this was an automatic reconnect during a recursive
		// operation, stop the recursive operation
		m_state.GetRemoteRecursiveOperation()->StopRecursiveOperation();
	}
	else if (commandInfo.command->GetId() == Command::connect && nReplyCode == FZ_REPLY_OK) {
		m_state.SetSuccessfulConnect();
		m_CommandList.pop_front();
	}
	else
		m_CommandList.pop_front();

	--m_inside_commandqueue;

	ProcessNextCommand();
}

void CCommandQueue::RequestExclusiveEngine(bool requestExclusive)
{
	wxASSERT(!m_exclusiveEngineLock || !requestExclusive);

	if (!m_exclusiveEngineRequest && requestExclusive)
	{
		m_requestId = ++m_requestIdCounter;
		if (m_requestId < 0)
		{
			m_requestIdCounter = 0;
			m_requestId = 0;
		}
		if (m_CommandList.empty())
		{
			m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
			GrantExclusiveEngineRequest();
			return;
		}
	}
	if (!requestExclusive)
		m_exclusiveEngineLock = false;
	m_exclusiveEngineRequest = requestExclusive;
	m_state.NotifyHandlers(STATECHANGE_REMOTE_IDLE);
}

void CCommandQueue::GrantExclusiveEngineRequest()
{
	wxASSERT(!m_exclusiveEngineLock);
	m_exclusiveEngineLock = true;
	m_exclusiveEngineRequest = false;

	wxCommandEvent *evt = new wxCommandEvent(fzEVT_GRANTEXCLUSIVEENGINEACCESS);
	evt->SetId(m_requestId);
	m_pMainFrame->GetQueue()->GetEventHandler()->QueueEvent(evt);
}

CFileZillaEngine* CCommandQueue::GetEngineExclusive(int requestId)
{
	if (!m_exclusiveEngineLock)
		return 0;

	if (requestId != m_requestId)
		return 0;

	return m_pEngine;
}


void CCommandQueue::ReleaseEngine()
{
	m_exclusiveEngineLock = false;

	ProcessNextCommand();
}

bool CCommandQueue::Quit()
{
	m_quit = true;
	return Cancel();
}

void CCommandQueue::ProcessDirectoryListing(CDirectoryListingNotification const& listingNotification)
{
	auto const firstListing = std::find_if(m_CommandList.begin(), m_CommandList.end(), [](CommandInfo const& v) { return v.command->GetId() == Command::list; });
	bool const listingIsRecursive = firstListing != m_CommandList.end() && firstListing->origin == recursiveOperation;

	std::shared_ptr<CDirectoryListing> pListing;
	if (!listingNotification.GetPath().empty()) {
		pListing = std::make_shared<CDirectoryListing>();
		if (listingNotification.Failed() ||
			m_state.m_pEngine->CacheLookup(listingNotification.GetPath(), *pListing) != FZ_REPLY_OK)
		{
			pListing = std::make_shared<CDirectoryListing>();
			pListing->path = listingNotification.GetPath();
			pListing->m_flags |= CDirectoryListing::listing_failed;
			pListing->m_firstListTime = fz::monotonic_clock::now();
		}
	}

	if (listingIsRecursive) {
		if (!listingNotification.Modified() && m_state.GetRemoteRecursiveOperation()->IsActive()) {
			m_state.NotifyHandlers(STATECHANGE_REMOTE_DIR_OTHER, wxString(), &pListing);
		}
	}
	else {
		m_state.SetRemoteDir(pListing, listingNotification.Modified());
	}

	if (pListing && !listingNotification.Failed() && m_state.GetServer()) {
		CContextManager::Get()->ProcessDirectoryListing(*m_state.GetServer(), pListing, listingIsRecursive ? 0 : &m_state);
	}
}
