#include <filezilla.h>
#include "recursive_operation.h"

void CRecursiveOperation::SetQueue(CQueueView * pQueue)
{
	m_pQueue = pQueue;
}

void CRecursiveOperation::SetImmediate(bool immediate)
{
	if (m_operationMode == recursive_transfer || m_operationMode == recursive_transfer_flatten) {
		m_immediate = immediate;
		if (!immediate) {
			m_actionAfterBlocker.reset();
		}
	}
}
