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


CLocalRecursiveOperation::CLocalRecursiveOperation(CState& state)
	: CRecursiveOperation(state)
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

void CLocalRecursiveOperation::StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate)
{
	if (!DoStartRecursiveOperation(mode, filters, immediate)) {
		StopRecursiveOperation();
	}
}

bool CLocalRecursiveOperation::DoStartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate)
{
	if (!m_pQueue) {
		return false;
	}

	auto const server = m_state.GetServer();
	if (server) {
		server_ = *server;
	}
	else {
		if (mode != OperationMode::recursive_list) {
			return false;
		}

		server_ = CServer();
	}

	{
		fz::scoped_lock l(mutex_);

		wxCHECK_MSG(m_operationMode == recursive_none, false, _T("StartRecursiveOperation called with m_operationMode != recursive_none"));

		if (mode == recursive_chmod) {
			return false;
		}

		if (recursion_roots_.empty()) {
			// Nothing to do in this case
			return false;
		}

		m_processedFiles = 0;
		m_processedDirectories = 0;

		m_immediate = immediate;
		m_operationMode = mode;

		m_filters = filters;

		if (!run()) {
			m_operationMode = recursive_none;
			return false;
		}
	}

	if ((mode == CRecursiveOperation::recursive_transfer || mode == CRecursiveOperation::recursive_transfer_flatten) && immediate) {
		m_actionAfterBlocker = m_pQueue->GetActionAfterBlocker();
	}

	m_state.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);

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

	m_state.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);

	m_actionAfterBlocker.reset();
}

void CLocalRecursiveOperation::OnStateChange(t_statechange_notifications, const wxString&, const void*)
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
			if (m_operationMode == recursive_transfer) {
				// Non-flatten case
				remoteSub.AddSegment(entry.name);
			}
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

		auto filters = m_filters.first;

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
					entry.name = fz::to_wstring(name);

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

	bool const queue = m_operationMode == recursive_transfer || m_operationMode == recursive_transfer_flatten;

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
			stop = true;
		}
		else {
			if (queue) {
				m_pQueue->QueueFiles(!m_immediate, server_, d);
			}
			++m_processedDirectories;
			processed += d.files.size();
			m_state.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_LISTING, wxString(), &d);
		}
	}

	if (queue) {
		m_pQueue->QueueFile_Finish(m_immediate);
	}

	m_processedFiles += processed;
	if (stop) {
		StopRecursiveOperation();
	}
	else if (processed) {
		m_state.NotifyHandlers(STATECHANGE_LOCAL_RECURSION_STATUS);

		if (processed >= 5000) {
			CallAfter(&CLocalRecursiveOperation::OnListedDirectory);
		}
	}
}
