#ifndef FILEZILLA_LOCAL_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_LOCAL_RECURSIVE_OPERATION_HEADER

#include "recursive_operation.h"

#include <libfilezilla/thread.hpp>

#include <set>

class local_recursion_root final
{
public:
	local_recursion_root() = default;

	void add_dir_to_visit(CLocalPath const& localPath, CServerPath const& remotePath = CServerPath());

	bool empty() const { return m_dirsToVisit.empty(); }

private:
	friend class CLocalRecursiveOperation;

	class new_dir final
	{
	public:
		CLocalPath localPath;
		CServerPath remotePath;
	};

	std::set<CLocalPath> m_visitedDirs;
	std::deque<new_dir> m_dirsToVisit;
};

class CLocalRecursiveOperation final : public CRecursiveOperation, private fz::thread, public wxEvtHandler
{
public:
	class listing final
	{
	public:
		class entry
		{
		public:
			std::wstring name;
			int64_t size{};
			fz::datetime time;
			int attributes{};
		};

		std::vector<entry> files;
		std::vector<entry> dirs;
		CLocalPath localPath;
		CServerPath remotePath;
	};

	CLocalRecursiveOperation(CState& state);
	virtual ~CLocalRecursiveOperation();

	void AddRecursionRoot(local_recursion_root && root);
	void StartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate = true);

	virtual void StopRecursiveOperation();

protected:
	bool DoStartRecursiveOperation(OperationMode mode, ActiveFilters const& filters, bool immediate);

	virtual void OnStateChange(t_statechange_notifications notification, const wxString&, const void* data2);

	virtual void entry();

	void EnqueueEnumeratedListing(fz::scoped_lock& l, listing&& d);

	std::deque<local_recursion_root> recursion_roots_;

	fz::mutex mutex_;

	std::deque<listing> m_listedDirectories;

	CServer server_;

	void OnListedDirectory();

	DECLARE_EVENT_TABLE()
};

#endif
