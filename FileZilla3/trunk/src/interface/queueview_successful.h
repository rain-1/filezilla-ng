#ifndef FILEZILLA_INTERFACE_QUEUEVIEW_SUCCESSFUL_HEADER
#define FILEZILLA_INTERFACE_QUEUEVIEW_SUCCESSFUL_HEADER

#include "queueview_failed.h"

class CQueueViewSuccessful final : public CQueueViewFailed
{
public:
	CQueueViewSuccessful(CQueue* parent, int index);

	bool AutoClear() const { return m_autoClear; }

protected:

	bool m_autoClear{};

	DECLARE_EVENT_TABLE()
	void OnContextMenu(wxContextMenuEvent& event);
	void OnMenuAutoClear(wxCommandEvent& event);
};

#endif
