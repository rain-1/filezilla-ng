#ifndef FILEZILLA_INTERFACE_QUEUEVIEW_FAILED_HEADER
#define FILEZILLA_INTERFACE_QUEUEVIEW_FAILED_HEADER

class CQueueViewFailed : public CQueueViewBase
{
public:
	CQueueViewFailed(CQueue* parent, int index);
	CQueueViewFailed(CQueue* parent, int index, const wxString& title);

protected:

	bool RequeueFileItem(CFileItem* pItem, CServerItem* pServerItem);
	bool RequeueServerItem(CServerItem* pServerItem);

	DECLARE_EVENT_TABLE()
	void OnContextMenu(wxContextMenuEvent& event);
	void OnRemoveAll(wxCommandEvent& event);
	void OnRemoveSelected(wxCommandEvent& event);
	void OnRequeueSelected(wxCommandEvent& event);
	void OnRequeueAll(wxCommandEvent& event);
	void OnChar(wxKeyEvent& event);
};

#endif
