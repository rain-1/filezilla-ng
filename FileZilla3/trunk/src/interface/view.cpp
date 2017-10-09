#include <filezilla.h>
#include "view.h"
#include "viewheader.h"

#include "state.h"
#include "listingcomparison.h"
#include "conditionaldialog.h"

#include <algorithm>

BEGIN_EVENT_TABLE(CView, wxNavigationEnabled<wxWindow>)
EVT_SIZE(CView::OnSize)
END_EVENT_TABLE()

CView::CView(wxWindow* pParent)
{
	Create(pParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSUNKEN_BORDER);
}

void CView::SetStatusBar(wxStatusBar* pStatusBar)
{
	m_pStatusBar = pStatusBar;
}

void CView::SetFooter(wxWindow* footer)
{
	m_pFooter = footer;
}

void CView::SetSearchPanel(wxWindow* panel)
{
	m_pSearchPanel = panel;
}

void CView::Arrange(wxWindow* child, wxRect& clientRect, bool top)
{
	if (child && child->IsShown()) {
		int const childHeight = child->GetSize().GetHeight();

		wxRect childRect = clientRect;
		childRect.SetHeight(childHeight);

		if (!top) {
			childRect.SetTop(clientRect.GetBottom() - childHeight + 1);
		}
		else {
			clientRect.SetTop(childHeight);
		}
		clientRect.SetHeight(std::max(0, (clientRect.GetHeight() - childHeight)));

		child->SetSize(childRect);
#ifdef __WXMSW__
		child->Refresh();
#endif
	}
}

void CView::OnSize(wxSizeEvent&)
{
	wxSize size = GetClientSize();
	wxRect rect(size);

	Arrange(m_pHeader, rect, true);
	Arrange(m_pFooter, rect, false);
	Arrange(m_pStatusBar, rect, false);
	Arrange(m_pSearchPanel, rect, false);

	if (m_pWnd) {
		m_pWnd->SetSize(rect);
	}
}

void CView::SetHeader(CViewHeader* pWnd)
{
	m_pHeader = pWnd;
	if (m_pHeader && m_pHeader->GetParent() != this)
		CViewHeader::Reparent(&m_pHeader, this);
	FixTabOrder();
}

void CView::SetWindow(wxWindow* pWnd)
{
	m_pWnd = pWnd;
	FixTabOrder();
}

void CView::FixTabOrder()
{
	if (m_pHeader && m_pWnd && m_pWnd->GetParent() == this) {
		m_pWnd->MoveAfterInTabOrder(m_pHeader);
	}
}

CViewHeader* CView::DetachHeader()
{
	CViewHeader* pHeader = m_pHeader;
	m_pHeader = 0;
	return pHeader;
}

void CView::ShowSearchPanel()
{
	if (m_pSearchPanel) {
		CState* pState = CContextManager::Get()->GetCurrentContext();
		if (pState) {
			CComparisonManager* pComparisonManager = pState->GetComparisonManager();
			if (pComparisonManager && pComparisonManager->IsComparing()) {
				CConditionalDialog dlg(this, CConditionalDialog::quick_search, CConditionalDialog::yesno);
				dlg.SetTitle(_("Directory comparison"));
				dlg.AddText(_("Quick search cannot be opened if comparing directories."));
				dlg.AddText(_("End comparison and open quick search?"));
				if (!dlg.Run())
					return;
				pComparisonManager->ExitComparisonMode();
			}		
		}		

		m_pSearchPanel->Show();
	}
}
