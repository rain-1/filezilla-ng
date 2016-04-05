#include <filezilla.h>
#include "local_recursive_operation.h"

#include <libfilezilla/local_filesys.hpp>

#include "QueueView.h"

BEGIN_EVENT_TABLE(CLocalRecursiveOperation, wxEvtHandler)
END_EVENT_TABLE()

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
	if (!DoStartRecursiveOperation(mode, filters)) {
		StopRecursiveOperation();
	}
}

bool CLocalRecursiveOperation::DoStartRecursiveOperation(OperationMode mode, std::vector<CFilter> const& filters)
{
	if (!m_pState || !m_pQueue) {
		return false;
	}
	
	auto const server = m_pState->GetServer();
	if (!server) {
		return false;
	}

	server_ = *server;
	
	{
		fz::scoped_lock l(mutex_);

		wxCHECK_MSG(m_operationMode == recursive_none, false, _T("StartRecursiveOperation called with m_operationMode != recursive_none"));
		wxCHECK_MSG(m_pState->IsRemoteConnected(), false, _T("StartRecursiveOperation while disconnected"));

		if (mode == recursive_chmod) {
			return false;
		}

		if (recursion_roots_.empty()) {
			// Nothing to do in this case
			return false;
		}

		m_processedFiles = 0;
		m_processedDirectories = 0;

		m_operationMode = mode;

		m_filters = filters;

		if (!run()) {
			m_operationMode = recursive_none;
			return false;
		}
	}

	m_pState->NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);

	return true;
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

		m_processedFiles = 0;
		m_processedDirectories = 0;

	}

	join();
	m_listedDirectories.clear();

	m_pState->NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);
}

void CLocalRecursiveOperation::OnStateChange(CState* pState, t_statechange_notifications notification, const wxString&, const void* data2)
{
}

void CLocalRecursiveOperation::EnqueueEnumeratedListing(fz::scoped_lock& l, listing&& d)
{
	if (recursion_roots_.empty()) {
		return;
	}

	auto& root = recursion_roots_.front();

	// Queue for recursion
	for (auto const& entry : d.dirs) {
		local_recursion_root::new_dir dir;
		CLocalPath localSub = d.localPath;
		localSub.AddSegment(entry.name);

		CServerPath remoteSub = d.remotePath;
		if (!remoteSub.empty()) {
			remoteSub.AddSegment(entry.name);
		}
		root.add_dir_to_visit(localSub, remoteSub);
	}

	m_listedDirectories.emplace_back(std::move(d));

	// Hand off to GUI thread
	if (m_listedDirectories.size() == 1) {
		l.unlock();
		CallAfter(&CLocalRecursiveOperation::OnListedDirectory);
		l.lock();
	}
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

			bool sentPartial = false;
			fz::local_filesys fs;
			fz::native_string localPath = fz::to_native(d.localPath.GetPath());

			if (fs.begin_find_files(localPath)) {
				listing::entry entry;
				bool isLink{};
				fz::native_string name;
				bool isDir{};
				while (fs.get_next_file(name, isLink, isDir, &entry.size, &entry.time, &entry.attributes)) {
					if (isLink) {
						continue;
					}
					entry.name = name;

					if (!filterManager.FilenameFiltered(filters, entry.name, d.localPath.GetPath(), isDir, entry.size, entry.attributes, entry.time)) {
						if (isDir) {
							d.dirs.emplace_back(std::move(entry));
						}
						else {
							d.files.emplace_back(std::move(entry));
						}

						// If having queued 5k items, hand off to main thread.
						if (d.files.size() + d.dirs.size() >= 5000) {
							sentPartial = true;

							listing next;
							next.localPath = d.localPath;
							next.remotePath = d.remotePath;

							l.lock();
							// Check for cancellation
							if (recursion_roots_.empty()) {
								l.unlock();
								break;
							}
							EnqueueEnumeratedListing(l, std::move(d));
							l.unlock();
							d = next;
						}
					}
				}
			}

			l.lock();
			// Check for cancellation
			if (recursion_roots_.empty()) {
				break;
			}
			if (!sentPartial || !d.files.empty() || !d.dirs.empty()) {
				EnqueueEnumeratedListing(l, std::move(d));
			}
		}

		listing d;
		m_listedDirectories.emplace_back(std::move(d));
	}

	CallAfter(&CLocalRecursiveOperation::OnListedDirectory);
}

void CLocalRecursiveOperation::OnListedDirectory()
{
	if (m_operationMode == recursive_none) {
		return;
	}

	bool const queueOnly = m_operationMode == recursive_addtoqueue || m_operationMode == recursive_addtoqueue_flatten;

	listing d;

	bool stop = false;
	int64_t processed = 0;
	while (processed < 5000) {
		{
			fz::scoped_lock l(mutex_);
			if (m_listedDirectories.empty()) {
				break;
			}

			d = std::move(m_listedDirectories.front());
			m_listedDirectories.pop_front();
		}

		if (d.localPath.empty()) {
			StopRecursiveOperation();
		}
		else {
			m_pQueue->QueueFiles(queueOnly, server_, d);
			++m_processedDirectories;
			processed += d.files.size();
		}
	}

	m_pQueue->QueueFile_Finish(!queueOnly);
	
	m_processedFiles += processed;
	if (stop) {
		StopRecursiveOperation();
	}
	else if (processed) {
		m_pState->NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);

		if (processed >= 5000) {
			CallAfter(&CLocalRecursiveOperation::OnListedDirectory);
		}
	}
}
