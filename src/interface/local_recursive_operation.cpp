#include <filezilla.h>
#include "local_recursive_operation.h"

#include <libfilezilla/local_filesys.hpp>

#include "QueueView.h"

DEFINE_EVENT_TYPE(fzEVT_LOCALRECURSION_DIR)

BEGIN_EVENT_TABLE(CLocalRecursiveOperation, wxEvtHandler)
EVT_COMMAND(wxID_ANY, fzEVT_LOCALRECURSION_DIR, CLocalRecursiveOperation::OnListedDirectory)
END_EVENT_TABLE()

local_recursion_root::local_recursion_root(CLocalPath const& rootPath)
	: m_rootPath(rootPath)
{}


void local_recursion_root::add_dir_to_visit(CLocalPath const& localPath, CServerPath const& remotePath)
{
	new_dir dirToVisit;
	dirToVisit.localPath = localPath;
	dirToVisit.remotePath = remotePath;
	m_dirsToVisit.push_back(dirToVisit);
}


CLocalRecursiveOperation::CLocalRecursiveOperation(CState* pState)
	: CRecursiveOperation(pState)
{
}

CLocalRecursiveOperation::~CLocalRecursiveOperation()
{
	join();
}

void CLocalRecursiveOperation::AddRecursionRoot(local_recursion_root && root)
{
	if (!root.empty()) {
		fz::scoped_lock l(mutex_);
		recursion_roots_.push_back(std::forward<local_recursion_root>(root));
	}
}

void CLocalRecursiveOperation::StartRecursiveOperation(OperationMode mode, std::vector<CFilter> const& filters)
{
	{
		fz::scoped_lock l(mutex_);

		wxCHECK_RET(m_operationMode == recursive_none, _T("StartRecursiveOperation called with m_operationMode != recursive_none"));
		wxCHECK_RET(m_pState->IsRemoteConnected(), _T("StartRecursiveOperation while disconnected"));

		if (mode == recursive_chmod)
			return;

		if ((mode == recursive_transfer || mode == recursive_addtoqueue || mode == recursive_transfer_flatten || mode == recursive_addtoqueue_flatten) && !m_pQueue)
			return;

		if (recursion_roots_.empty()) {
			// Nothing to do in this case
			return;
		}

		m_processedFiles = 0;
		m_processedDirectories = 0;

		m_operationMode = mode;

		m_filters = filters;

		if (!run()) {
			m_operationMode = recursive_none;
			return;
		}
	}

	m_pState->NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);
}

void CLocalRecursiveOperation::StopRecursiveOperation()
{
	{
		fz::scoped_lock l(mutex_);
		if (m_operationMode == recursive_none) {
			return;
		}

		m_operationMode = recursive_none;
		recursion_roots_.clear();
	}

	join();

	m_pState->NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);
}

void CLocalRecursiveOperation::OnStateChange(CState* pState, t_statechange_notifications notification, const wxString&, const void* data2)
{
}

void CLocalRecursiveOperation::entry()
{
	{
		fz::scoped_lock l(mutex_);

		auto filters = m_filters;

		CFilterManager filterManager;
		filterManager.CompileRegexes(filters);

		while (!recursion_roots_.empty()) {
			listing d;

			{
				auto& root = recursion_roots_.front();
				if (root.m_dirsToVisit.empty()) {
					recursion_roots_.pop_front();
					continue;
				}

				auto const& dir = root.m_dirsToVisit.front();
				d.localPath = dir.localPath;
				d.remotePath = dir.remotePath;

				root.m_dirsToVisit.pop_front();
			}

			// Do the slow part without holding mutex
			l.unlock();

			fz::local_filesys fs;
			fz::native_string localPath = fz::to_native(d.localPath.GetPath());
			if (fs.begin_find_files(localPath)) {

				listing::entry entry;
				bool isLink{};
				fz::native_string name;
				while (fs.get_next_file(name, isLink, entry.dir, &entry.size, &entry.time, &entry.attributes)) {
					entry.name = name;
					if (isLink) {
						continue;
					}

					if (!filterManager.FilenameFiltered(filters, entry.name, d.localPath.GetPath(), entry.dir, entry.size, entry.attributes, entry.time)) {
						d.files.push_back(entry);
					}
				}
			}

			l.lock();

			// Check for cancellation
			if (recursion_roots_.empty()) {
				break;
			}

			auto& root = recursion_roots_.front();

			// Queue for recursion
			for (auto const& entry : d.files) {
				if (entry.dir) {
					local_recursion_root::new_dir dir;
					CLocalPath localSub = d.localPath;
					localSub.AddSegment(entry.name);

					CServerPath remoteSub = d.remotePath;
					if (!remoteSub.empty()) {
						remoteSub.AddSegment(entry.name);
					}
					root.add_dir_to_visit(localSub, remoteSub);
				}
			}

			m_listedDirectories.emplace_back(std::move(d));

			// Hand off to GUI thread
			//TODO : once
			l.unlock();
			QueueEvent(new wxCommandEvent(fzEVT_LOCALRECURSION_DIR, wxID_ANY));
			l.lock();
		}
	}

	listing d;
	m_listedDirectories.emplace_back(std::move(d));
	QueueEvent(new wxCommandEvent(fzEVT_LOCALRECURSION_DIR, wxID_ANY));
}

void CLocalRecursiveOperation::OnListedDirectory(wxCommandEvent &)
{
	CServer const* const server = m_pState->GetServer();
	if (!server)
		return;

	listing d;

	{
		fz::scoped_lock l(mutex_);
		if (m_operationMode == recursive_none) {
			return;
		}

		if (m_listedDirectories.empty()) {
			return;
		}

		d = std::move(m_listedDirectories.front());
		m_listedDirectories.pop_front();
	}

	if (d.localPath.empty()) {
		StopRecursiveOperation();
	}
	else {
		bool const queueOnly = m_operationMode == recursive_addtoqueue || m_operationMode == recursive_addtoqueue_flatten;
		m_pQueue->QueueFiles(queueOnly, *server, d);
		++m_processedDirectories;
		m_processedFiles += d.files.size();
		m_pState->NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);
	}
}
