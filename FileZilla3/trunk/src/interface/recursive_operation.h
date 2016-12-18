#ifndef FILEZILLA_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_RECURSIVE_OPERATION_HEADER

#include "filter.h"
#include "state.h"

class CActionAfterBlocker;
class CQueueView;
class CRecursiveOperation : public CStateEventHandler
{
public:
	CRecursiveOperation(CState& state)
		: CStateEventHandler(state)
		, m_operationMode(recursive_none)
	{}

	virtual ~CRecursiveOperation() = default;

	enum OperationMode {
		recursive_none,
		recursive_transfer,
		recursive_transfer_flatten,
		recursive_delete,
		recursive_chmod,
		recursive_list,
		recursive_synchronize_download,
		recursive_synchronize_upload
	};

	bool IsActive() const { return GetOperationMode() != recursive_none; }
	OperationMode GetOperationMode() const { return m_operationMode; }
	int64_t GetProcessedFiles() const { return m_processedFiles; }
	int64_t GetProcessedDirectories() const { return m_processedDirectories; }

	uint64_t m_processedFiles{};
	uint64_t m_processedDirectories{};

	virtual void StopRecursiveOperation() = 0;

	void SetQueue(CQueueView* pQueue);

	void SetImmediate(bool immediate);

protected:
	bool m_immediate{true};

	OperationMode m_operationMode;

	CQueueView* m_pQueue{};

	ActiveFilters m_filters;

	std::shared_ptr<CActionAfterBlocker> m_actionAfterBlocker;
};

#endif