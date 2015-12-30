#ifndef __RECURSIVE_OPERATION_H__
#define __RECURSIVE_OPERATION_H__

#include "state.h"
#include <set>
#include "filter.h"
#include <optional.h>

class CChmodDialog;
class CQueueView;

class CRecursiveOperation final : public CStateEventHandler
{
public:
	CRecursiveOperation(CState* pState);
	~CRecursiveOperation();

	enum OperationMode
	{
		recursive_none,
		recursive_download,
		recursive_addtoqueue,
		recursive_download_flatten,
		recursive_addtoqueue_flatten,
		recursive_delete,
		recursive_chmod,
		recursive_list
	};

	void AddRecursionRoot(CServerPath const& startDir, bool allowParent);
	void StartRecursiveOperation(OperationMode mode, std::vector<CFilter> const& filters, CServerPath const& finalDir);

	void StopRecursiveOperation();

	void AddDirectoryToVisit(const CServerPath& path, const wxString& subdir, const CLocalPath& localDir = CLocalPath(), bool is_link = false);
	void AddDirectoryToVisitRestricted(const CServerPath& path, const wxString& restrict, bool recurse);

	bool IsActive() const { return GetOperationMode() != recursive_none; }
	OperationMode GetOperationMode() const { return m_operationMode; }
	int64_t GetProcessedFiles() const { return m_processedFiles; }
	int64_t GetProcessedDirectories() const { return m_processedDirectories; }

	// Needed for recursive_chmod
	void SetChmodDialog(CChmodDialog* pChmodDialog);

	void SetQueue(CQueueView* pQueue);

	bool ChangeOperationMode(OperationMode mode);

protected:
	void LinkIsNotDir();
	void ListingFailed(int error);

	// Processes the directory listing in case of a recursive operation
	void ProcessDirectoryListing(const CDirectoryListing* pDirectoryListing);

	bool NextOperation();

	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, const wxString&, const void* data2);

	OperationMode m_operationMode;

	class CNewDir final
	{
	public:
		CServerPath parent;
		wxString subdir;
		CLocalPath localDir;
		CSparseOptional<wxString> restrict;

		// Symlink target might be outside actual start dir. Yet
		// sometimes user wants to download symlink target contents
		CServerPath start_dir;

		// 0 = not a link
		// 1 = link, added by class during the operation
		// 2 = link, added by user of class
		int link{};

		bool doVisit{true};
		bool recurse{true};
		bool second_try{};
	};

	bool BelowRecursionRoot(const CServerPath& path, CNewDir &dir);

	CServerPath m_startDir;
	CServerPath m_finalDir;
	std::set<CServerPath> m_visitedDirs;
	std::deque<CNewDir> m_dirsToVisit;

	bool m_allowParent{};

	// Needed for recursive_chmod
	CChmodDialog* m_pChmodDlg{};

	CQueueView* m_pQueue{};

	std::vector<CFilter> m_filters;

	friend class CCommandQueue;

	uint64_t m_processedFiles{};
	uint64_t m_processedDirectories{};
};

#endif //__RECURSIVE_OPERATION_H__
