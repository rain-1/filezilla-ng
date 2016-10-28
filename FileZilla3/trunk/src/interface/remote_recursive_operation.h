#ifndef FILEZILLA_REMOTE_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_REMOTE_RECURSIVE_OPERATION_HEADER

#include <set>
#include "recursive_operation.h"
#include <libfilezilla/optional.hpp>

class CChmodDialog;

class recursion_root final
{
public:
	recursion_root() = default;
	recursion_root(CServerPath const& start_dir, bool allow_parent);

	void add_dir_to_visit(CServerPath const& path, std::wstring const& subdir, CLocalPath const& localDir = CLocalPath(), bool is_link = false);

	// Queue a directory but restrict processing to the named subdirectory
	void add_dir_to_visit_restricted(CServerPath const& path, std::wstring const& restrict, bool recurse);

	bool empty() const { return m_dirsToVisit.empty(); }

private:
	friend class CRemoteRecursiveOperation;

	class new_dir final
	{
	public:
		CServerPath parent;
		std::wstring subdir;
		CLocalPath localDir;
		fz::sparse_optional<std::wstring> restrict;

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

	CServerPath m_remoteStartDir;
	std::set<CServerPath> m_visitedDirs;
	std::deque<new_dir> m_dirsToVisit;
	bool m_allowParent{};
};

class CRemoteRecursiveOperation final : public CRecursiveOperation
{
public:
	CRemoteRecursiveOperation(CState& state);
	virtual ~CRemoteRecursiveOperation();

	void AddRecursionRoot(recursion_root && root);
	void StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, CServerPath const& finalDir, bool immediate = true);

	// Needed for recursive_chmod
	void SetChmodDialog(CChmodDialog* pChmodDialog);

	virtual void StopRecursiveOperation();

protected:
	void LinkIsNotDir();
	void ListingFailed(int error);

	// Processes the directory listing in case of a recursive operation
	void ProcessDirectoryListing(const CDirectoryListing* pDirectoryListing);

	bool NextOperation();

	virtual void OnStateChange(t_statechange_notifications notification, const wxString&, const void* data2);

	bool BelowRecursionRoot(const CServerPath& path, recursion_root::new_dir &dir);

	std::deque<recursion_root> recursion_roots_;

	CServerPath m_finalDir;

	// Needed for recursive_chmod
	CChmodDialog* m_pChmodDlg{};

	friend class CCommandQueue;
};

#endif
