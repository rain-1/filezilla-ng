#ifndef FILEZILLA_REMOTE_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_REMOTE_RECURSIVE_OPERATION_HEADER

#include "state.h"
#include <set>
#include "filter.h"
#include <libfilezilla/optional.hpp>

class CChmodDialog;
class CQueueView;

class recursion_root final
{
public:
	recursion_root() = default;
	recursion_root(CServerPath const& start_dir, bool allow_parent);

	void add_dir_to_visit(CServerPath const& path, wxString const& subdir, CLocalPath const& localDir = CLocalPath(), bool is_link = false);
	void add_dir_to_visit_restricted(CServerPath const& path, wxString const& restrict, bool recurse);

	bool empty() const { return m_dirsToVisit.empty(); }

private:
	friend class CRecursiveOperation;

	class new_dir final
	{
	public:
		CServerPath parent;
		wxString subdir;
		CLocalPath localDir;
		fz::sparse_optional<wxString> restrict;

		// Symlink target might be outside actual start dir. Yet
		// sometimes user wants to download symlink target contents
		CServerPath start_dir;

		// 0 = not a link
		// 1 = link, added by class during the operation
		// 2 = link, added by user of class
		int link{};

		bool doVisit{ true };
		bool recurse{ true };
		bool second_try{};
	};

	CServerPath m_remoteStartDir;
	CLocalPath m_localStartDir;
	std::set<CServerPath> m_visitedDirs;
	std::deque<new_dir> m_dirsToVisit;
	bool m_allowParent{};
};

class CRecursiveOperation final : public CStateEventHandler
{
public:
	CRecursiveOperation(CState* pState);
	~CRecursiveOperation();

	enum OperationMode
	{
		recursive_none,
		recursive_transfer,
		recursive_addtoqueue,
		recursive_transfer_flatten,
		recursive_addtoqueue_flatten,
		recursive_delete,
		recursive_chmod,
		recursive_list
	};

	void AddRecursionRoot(recursion_root && root);
	void StartRecursiveOperation(OperationMode mode, std::vector<CFilter> const& filters, CServerPath const& finalDir);

	void StopRecursiveOperation();

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

	bool BelowRecursionRoot(const CServerPath& path, recursion_root::new_dir &dir);

	std::deque<recursion_root> recursion_roots_;

	CServerPath m_finalDir;

	// Needed for recursive_chmod
	CChmodDialog* m_pChmodDlg{};

	CQueueView* m_pQueue{};

	std::vector<CFilter> m_filters;

	friend class CCommandQueue;

	uint64_t m_processedFiles{};
	uint64_t m_processedDirectories{};
};

#endif
