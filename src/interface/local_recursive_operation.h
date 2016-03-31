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
		class entry final
		{
		public:
			wxString name;
			int64_t size{};
			fz::datetime time;
			int attributes{};
			bool dir{};
		};

		std::vector<entry> files;
		CLocalPath localPath;
		CServerPath remotePath;
	};

	CLocalRecursiveOperation(CState* pState);
	virtual ~CLocalRecursiveOperation();

	void AddRecursionRoot(local_recursion_root && root);
	void StartRecursiveOperation(OperationMode mode, std::vector<CFilter> const& filters);

	virtual void StopRecursiveOperation();

protected:
	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, const wxString&, const void* data2);

	virtual void entry();

	std::deque<local_recursion_root> recursion_roots_;

	fz::mutex mutex_;

	std::deque<listing> m_listedDirectories;

	void OnListedDirectory();

	DECLARE_EVENT_TABLE()
};

#endif
