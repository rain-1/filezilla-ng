#ifndef FILEZILLA_LOCAL_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_LOCAL_RECURSIVE_OPERATION_HEADER

#include "recursive_operation.h"

#include <libfilezilla/thread.hpp>

class CLocalRecursiveOperation final : public CRecursiveOperation
{
public:
	CLocalRecursiveOperation(CState* pState);
	virtual ~CLocalRecursiveOperation();

	virtual void StopRecursiveOperation();

protected:
	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, const wxString&, const void* data2);
};

/*
DECLARE_EVENT_TYPE(fzEVT_FOLDERTHREAD_COMPLETE, -1)
DECLARE_EVENT_TYPE(fzEVT_FOLDERTHREAD_FILES, -1)

class CQueueView;

class CFolderProcessingEntry
{
public:
	enum t_type
	{
		dir,
		file
	};
	const t_type m_type;

	CFolderProcessingEntry(t_type type) : m_type(type) {}
	virtual ~CFolderProcessingEntry() {}
};

class t_newEntry : public CFolderProcessingEntry
{
public:
	t_newEntry()
		: CFolderProcessingEntry(CFolderProcessingEntry::file)
	{}

	wxString name;
	int64_t size{};
	fz::datetime time;
	int attributes{};
	bool dir{};
};

class CFolderProcessingThread final : public fz::thread
{
	struct t_internalDirPair
	{
		CLocalPath localPath;
		CServerPath remotePath;
	};
public:
	CFolderProcessingThread(CQueueView* pOwner, CLocalPath const& localPath, CServerPath const& remotePath);

	virtual ~CFolderProcessingThread();

	void GetFiles(std::list<CFolderProcessingEntry*> &entryList);

	class t_dirPair : public CFolderProcessingEntry
	{
	public:
		t_dirPair() : CFolderProcessingEntry(CFolderProcessingEntry::dir) {}
		CLocalPath localPath;
		CServerPath remotePath;
	};

	void ProcessDirectory(const CLocalPath& localPath, CServerPath const& remotePath, const wxString& name);

	void CheckFinished();

	void Quit();

private:

	void AddEntry(CFolderProcessingEntry* entry);

	void entry();

	std::list<t_internalDirPair*> m_dirsToCheck;

	// Access has to be guarded by m_sync
	std::list<CFolderProcessingEntry*> m_entryList;

	CQueueView* m_pOwner;

	fz::mutex m_sync;
	fz::condition m_condition;
	bool m_threadWaiting{};
	bool m_throttleWait{};
	bool m_didSendEvent{};
	bool m_processing_entries{};
	bool m_quit{};
};
*/
#endif
