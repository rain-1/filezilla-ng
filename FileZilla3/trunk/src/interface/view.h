#ifndef __VIEW_H__
#define __VIEW_H__

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

protected:
	void Arrange(wxWindow* child, wxRect& clientRect, bool top);

	void FixTabOrder();

	wxWindow* m_pWnd{};
	CViewHeader* m_pHeader{};
	wxStatusBar* m_pStatusBar{};

	wxWindow* m_pFooter{};

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent&);
};

#endif //__VIEW_H__
