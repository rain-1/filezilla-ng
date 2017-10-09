#ifndef FILEZILLA_INTERFACE_VIEW_HEADER
#define FILEZILLA_INTERFACE_VIEW_HEADER

class CViewHeader;
class CView final : public wxNavigationEnabled<wxWindow>
{
public:
	CView(wxWindow* pParent);

	void SetWindow(wxWindow* pWnd);
	void SetHeader(CViewHeader* pWnd);
	CViewHeader* GetHeader() { return m_pHeader; }
	CViewHeader* DetachHeader();
	void SetStatusBar(wxStatusBar* pStatusBar);
	wxStatusBar* GetStatusBar() { return m_pStatusBar; }

	void SetFooter(wxWindow* footer);
	void SetSearchPanel(wxWindow* panel);

	void ShowSearchPanel();

protected:
	void Arrange(wxWindow* child, wxRect& clientRect, bool top);

	void FixTabOrder();

	wxWindow* m_pWnd{};
	CViewHeader* m_pHeader{};
	wxStatusBar* m_pStatusBar{};

	wxWindow* m_pFooter{};
	wxWindow* m_pSearchPanel{};

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent&);
};

#endif
