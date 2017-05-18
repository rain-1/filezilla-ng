#ifndef FILEZILLA_INTERFACE_EXPORT_HEADER
#define FILEZILLA_INTERFACE_EXPORT_HEADER

#include "dialogex.h"

class CQueueView;
class CExportDialog final : protected wxDialogEx
{
public:
	CExportDialog(wxWindow* parent, CQueueView* pQueueView);

	void Run();

protected:
	wxWindow* const m_parent;
	const CQueueView* const m_pQueueView;
};

#endif
