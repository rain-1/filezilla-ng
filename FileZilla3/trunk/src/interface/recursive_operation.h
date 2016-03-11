#ifndef FILEZILLA_RECURSIVE_OPERATION_HEADER
#define FILEZILLA_RECURSIVE_OPERATION_HEADER

#include "filter.h"
#include "state.h"

class CQueueView;
class CRecursiveOperation : public CStateEventHandler
{
public:
	CRecursiveOperation(CState* pState)
		: CStateEventHandler(pState)
		, m_operationMode(recursive_none)
	{}

	virtual ~CRecursiveOperation() = default;

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

	bool IsActive() const { return GetOperationMode() != recursive_none; }
	OperationMode GetOperationMode() const { return m_operationMode; }
	int64_t GetProcessedFiles() const { return m_processedFiles; }
	int64_t GetProcessedDirectories() const { return m_processedDirectories; }

	uint64_t m_processedFiles{};
	uint64_t m_processedDirectories{};

	virtual void StopRecursiveOperation() = 0;

	void SetQueue(CQueueView* pQueue);

	bool ChangeOperationMode(OperationMode mode);

protected:
	OperationMode m_operationMode;

	CQueueView* m_pQueue{};

	std::vector<CFilter> m_filters;
};

#endif