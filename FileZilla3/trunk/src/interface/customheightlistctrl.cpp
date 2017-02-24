#include <filezilla.h>
#include "customheightlistctrl.h"

#include <algorithm>

IMPLEMENT_DYNAMIC_CLASS(wxCustomHeightListCtrl, wxScrolledWindow)

BEGIN_EVENT_TABLE(wxCustomHeightListCtrl, wxScrolledWindow)
EVT_MOUSE_EVENTS(wxCustomHeightListCtrl::OnMouseEvent)
EVT_SIZE(wxCustomHeightListCtrl::OnSize)
END_EVENT_TABLE()

wxCustomHeightListCtrl::wxCustomHeightListCtrl(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxScrolledWindow(parent, id, pos, size, style, name)
{
}

void wxCustomHeightListCtrl::SetLineHeight(int height)
{
	m_lineHeight = height;

	int posx, posy;
	GetViewStart(&posx, &posy);
	SetScrollbars(0, m_lineHeight, 0, m_rows.size(), 0, posy);

	Refresh();
}

void wxCustomHeightListCtrl::AdjustView()
{
	int posx, posy;
	GetViewStart(&posx, &posy);

#ifdef __WXGTK__
	// When decreasing scrollbar range, wxGTK does not seem to adjust child position
	// if viewport gets moved
	wxPoint old_view;
	GetViewStart(&old_view.x, &old_view.y);
#endif

	SetScrollbars(0, m_lineHeight, 0, m_rows.size(), 0, posy);

#ifdef __WXGTK__
	wxPoint new_view;
	GetViewStart(&new_view.x, &new_view.y);
	int delta_y = m_lineHeight *(old_view.y - new_view.y);

	if (delta_y) {
		wxWindowList::compatibility_iterator iter = GetChildren().GetFirst();
		while (iter) {
			wxWindow* child = iter->GetData();
			wxPoint pos = child->GetPosition();
			pos.y -= delta_y;
			child->SetPosition(pos);

			iter = iter->GetNext();
		}
	}
#endif
}

void wxCustomHeightListCtrl::SetFocus()
{
	wxWindow::SetFocus();
}

void wxCustomHeightListCtrl::OnDraw(wxDC& dc)
{
	wxSize size = GetClientSize();

	dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
	dc.SetPen(*wxTRANSPARENT_PEN);

	for (auto const& selected : m_selectedLines) {
		if (selected == m_focusedLine) {
			dc.SetPen(wxPen(wxColour(0, 0, 0), 1, wxPENSTYLE_DOT));
		}
		else {
			dc.SetPen(*wxTRANSPARENT_PEN);
		}
		dc.DrawRectangle(0, m_lineHeight * selected, size.GetWidth(), m_lineHeight);
	}
	if (m_focusedLine != npos && m_selectedLines.find(m_focusedLine) == m_selectedLines.end()) {
		dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
		dc.SetPen(wxPen(wxColour(0, 0, 0), 1, wxPENSTYLE_DOT));
		dc.DrawRectangle(0, m_lineHeight * m_focusedLine, size.GetWidth(), m_lineHeight);
	}
}

void wxCustomHeightListCtrl::OnMouseEvent(wxMouseEvent& event)
{
	bool changed = false;
	if (event.ButtonDown() && m_allow_selection) {
		wxPoint pos = event.GetPosition();
		int x, y;
		CalcUnscrolledPosition(pos.x, pos.y, &x, &y);
		if (y < 0 || y > static_cast<int>(m_lineHeight * m_rows.size())) {
			m_focusedLine = npos;
			m_selectedLines.clear();
			changed = true;
		}
		else {
			size_t line = static_cast<size_t>(y / m_lineHeight);

			if (event.ShiftDown()) {
				if (m_focusedLine == npos) {
					changed |= m_selectedLines.insert(line).second;
				}
				else if (line < m_focusedLine) {
					for (size_t i = line; i <= m_focusedLine; ++i) {
						changed |= m_selectedLines.insert(i).second;
					}
				}
				else {
					for (size_t i = line; i >= m_focusedLine && i != npos; --i) {
						changed |= m_selectedLines.insert(i).second;
					}
				}
			}
			else if (event.ControlDown()) {
				if (m_selectedLines.find(line) == m_selectedLines.end()) {
					m_selectedLines.insert(line);
				}
				else {
					m_selectedLines.erase(line);
				}
				changed = true;
			}
			else {
				m_selectedLines.clear();
				m_selectedLines.insert(line);
				changed = true;
			}

			m_focusedLine = line;
		}
		Refresh();
	}

	event.Skip();

	if (changed) {
		wxCommandEvent evt(wxEVT_COMMAND_LISTBOX_SELECTED, GetId());
		ProcessEvent(evt);
	}
}

void wxCustomHeightListCtrl::ClearSelection()
{
	m_selectedLines.clear();
	m_focusedLine = npos;

	AdjustView();
	Refresh();
}

std::set<size_t> wxCustomHeightListCtrl::GetSelection() const
{
	return m_selectedLines;
}

size_t wxCustomHeightListCtrl::GetRowCount() const
{
	return m_rows.size();
}

void wxCustomHeightListCtrl::SelectLine(size_t line)
{
	if (!m_allow_selection) {
		return;
	}

	m_selectedLines.clear();
	m_focusedLine = line;
	if (line != npos) {
		m_selectedLines.insert(line);
	}

	Refresh();
}

void wxCustomHeightListCtrl::AllowSelection(bool allow_selection)
{
	m_allow_selection = allow_selection;
	if (!allow_selection) {
		ClearSelection();
	}
}

void wxCustomHeightListCtrl::InsertRow(wxSizer* sizer, size_t pos)
{
	assert(sizer);
	assert(pos <= m_rows.size());
	m_rows.insert(m_rows.begin() + pos, sizer);

	sizer->SetContainingWindow(this);

	AdjustView();

	int left = 0;
	int top = 0;
	CalcScrolledPosition(0, 0, &left, &top);

	int const width = GetClientSize().GetWidth();
	for (size_t i = pos; i < m_rows.size(); ++i) {
		m_rows[i]->SetDimension(left, m_lineHeight * i + top, width, m_lineHeight);
	}

	Refresh();

#ifdef __WXGTK__
	// Needed for Ubuntu's shitty overlay scrollbar, it changes the layout asynchronously...
	CallAfter([this](){
		wxSizeEvent ev;
		OnSize(ev);
	});
#endif
}

void wxCustomHeightListCtrl::DeleteRow(wxSizer *row)
{
	auto it = std::find(m_rows.begin(), m_rows.end(), row);
	if (it != m_rows.end()) {
		DeleteRow(it - m_rows.begin());
	}
}

void wxCustomHeightListCtrl::DeleteRow(size_t pos)
{
	assert(pos < m_rows.size());
	m_rows[pos]->SetContainingWindow(0);
	m_rows.erase(m_rows.begin() + pos);

	std::set<size_t> selectedLines;
	m_selectedLines.swap(selectedLines);
	for (auto const& selected : selectedLines) {
		if (selected < m_rows.size()) {
			m_selectedLines.insert(selected);
		}
	}

	AdjustView();

	if (m_focusedLine >= m_rows.size()) {
		m_focusedLine = npos;
	}

	int left = 0;
	int top = 0;
	CalcScrolledPosition(0, 0, &left, &top);

	int const width = GetClientSize().GetWidth();

	// Intentionally update all: if y position changes, by the time OnSize is called in
	// response to AdjustView, internal state isn't quite correct
	for (size_t i = 0; i < m_rows.size(); ++i) {
		m_rows[i]->SetDimension(left, m_lineHeight * i + top, width, m_lineHeight);
	}

	Refresh();
}

void wxCustomHeightListCtrl::ClearRows()
{
	m_rows.clear();
}

void wxCustomHeightListCtrl::OnSize(wxSizeEvent& event)
{
	event.Skip();

	int const width = GetClientSize().GetWidth();

	int left = 0;
	int top = 0;
	CalcScrolledPosition(0, 0, &left, &top);
	for (size_t i = 0; i < m_rows.size(); ++i) {
		m_rows[i]->SetDimension(left, m_lineHeight * i + top, width, m_lineHeight);
	}
}
