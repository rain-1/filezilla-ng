#include <filezilla.h>
#include "recursive_operation.h"

void CRecursiveOperation::SetQueue(CQueueView * pQueue)
{
	m_pQueue = pQueue;
}

bool CRecursiveOperation::ChangeOperationMode(enum OperationMode mode)
{
	if (mode != recursive_addtoqueue && m_operationMode != recursive_transfer && mode != recursive_addtoqueue_flatten && m_operationMode != recursive_transfer_flatten)
		return false;

	m_operationMode = mode;

	return true;
}