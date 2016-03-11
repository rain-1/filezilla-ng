#include <filezilla.h>
#include "local_recursive_operation.h"

#include <libfilezilla/local_filesys.hpp>

#include "QueueView.h"

DEFINE_EVENT_TYPE(fzEVT_FOLDERTHREAD_COMPLETE)
DEFINE_EVENT_TYPE(fzEVT_FOLDERTHREAD_FILES)

CFolderProcessingThread::CFolderProcessingThread(CQueueView* pOwner, CLocalPath const& localPath, CServerPath const& remotePath)
{
	m_pOwner = pOwner;
	
	if (!localPath.empty() && !remotePath.empty()) {
		t_internalDirPair* pair = new t_internalDirPair;
		pair->localPath = localPath;
		pair->remotePath = remotePath;
		m_dirsToCheck.push_back(pair);
	}
}

CFolderProcessingThread::~CFolderProcessingThread()
{
	Quit();
	for (auto iter = m_entryList.begin(); iter != m_entryList.end(); ++iter)
		delete *iter;
	for (auto iter = m_dirsToCheck.begin(); iter != m_dirsToCheck.end(); ++iter)
		delete *iter;
}

void CFolderProcessingThread::GetFiles(std::list<CFolderProcessingEntry*> &entryList)
{
	wxASSERT(entryList.empty());
	fz::scoped_lock locker(m_sync);
	entryList.swap(m_entryList);

	m_didSendEvent = false;
	m_processing_entries = true;

	if (m_throttleWait) {
		m_throttleWait = false;
		m_condition.signal(locker);
	}
}

void CFolderProcessingThread::ProcessDirectory(const CLocalPath& localPath, CServerPath const& remotePath, const wxString& name)
{
	fz::scoped_lock locker(m_sync);

	t_internalDirPair* pair = new t_internalDirPair;

	{
		pair->localPath = localPath;
		pair->localPath.AddSegment(name);

		pair->remotePath = remotePath;
		pair->remotePath.AddSegment(name);
	}

	m_dirsToCheck.push_back(pair);

	if (m_threadWaiting) {
		m_threadWaiting = false;
		m_condition.signal(locker);
	}
}

void CFolderProcessingThread::CheckFinished()
{
	fz::scoped_lock locker(m_sync);
	wxASSERT(m_processing_entries);

	m_processing_entries = false;

	if (m_threadWaiting && (!m_dirsToCheck.empty() || m_entryList.empty())) {
		m_threadWaiting = false;
		m_condition.signal(locker);
	}
}

void CFolderProcessingThread::AddEntry(CFolderProcessingEntry* entry)
{
	fz::scoped_lock l(m_sync);
	m_entryList.push_back(entry);

	// Wait if there are more than 100 items to queue,
	// don't send notification if there are less than 10.
	// This reduces overhead
	bool send;

	if (m_didSendEvent) {
		send = false;
		if (m_entryList.size() >= 100) {
			m_throttleWait = true;
			m_condition.wait(l);
		}
	}
	else if (m_entryList.size() < 20)
		send = false;
	else
		send = true;

	if (send)
		m_didSendEvent = true;

	l.unlock();

	if (send) {
		// We send the notification after leaving the critical section, else we
		// could get into a deadlock. wxWidgets event system does internal
		// locking.
		//m_pOwner->QueueEvent(new wxCommandEvent(fzEVT_FOLDERTHREAD_FILES, wxID_ANY));
	}
}

void CFolderProcessingThread::entry()
{
	fz::local_filesys localFileSystem;

	while (true) {
		fz::scoped_lock l(m_sync);
		if (m_quit) {
			break;
		}

		if (m_dirsToCheck.empty()) {
			if (!m_didSendEvent && !m_entryList.empty()) {
				m_didSendEvent = true;
				l.unlock();
				//m_pOwner->QueueEvent(new wxCommandEvent(fzEVT_FOLDERTHREAD_FILES, wxID_ANY));
				continue;
			}

			if (!m_didSendEvent && !m_processing_entries) {
				break;
			}
			m_threadWaiting = true;
			m_condition.wait(l);
			if (m_dirsToCheck.empty()) {
				break;
			}
			continue;
		}

		const t_internalDirPair *pair = m_dirsToCheck.front();
		m_dirsToCheck.pop_front();

		l.unlock();

		if (!localFileSystem.begin_find_files(fz::to_native(pair->localPath.GetPath()), false)) {
			delete pair;
			continue;
		}

		t_dirPair* pair2 = new t_dirPair;
		pair2->localPath = pair->localPath;
		pair2->remotePath = pair->remotePath;
		AddEntry(pair2);

		t_newEntry* entry = new t_newEntry;

		fz::native_string name;
		bool is_link;
		bool is_dir;
		while (localFileSystem.get_next_file(name, is_link, is_dir, &entry->size, &entry->time, &entry->attributes)) {
			if (is_link)
				continue;

			entry->name = name;
			entry->dir = is_dir;

			AddEntry(entry);

			entry = new t_newEntry;
		}
		delete entry;

		delete pair;
	}

	//m_pOwner->QueueEvent(new wxCommandEvent(fzEVT_FOLDERTHREAD_COMPLETE, wxID_ANY));
}

void CFolderProcessingThread::Quit()
{
	{
		fz::scoped_lock locker(m_sync);
		m_quit = true;
	}
	join();
}
