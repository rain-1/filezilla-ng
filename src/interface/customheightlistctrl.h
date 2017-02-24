#ifndef FILEZILLA_INTERFACE_CUSTOM_HEIGHT_LISTCTRL_HEADER
#define FILEZILLA_INTERFACE_CUSTOM_HEIGHT_LISTCTRL_HEADER

#include <wx/scrolwin.h>

#include <set>
#include <vector>

class wxCustomHeightListCtrl final : public wxScrolledWindow
{
public:
	wxCustomHeightListCtrl() = default;

	wxCustomHeightListCtrl(wxWindow* parent, wxWindowID id = -1, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxHSCROLL | wxVSCROLL, const wxString& name = _T("scrolledWindow"));

	void SetLineHeight(int height);

	virtual void SetFocus();

	void ClearSelection();

	std::set<size_t> GetSelection() const;
	size_t GetRowCount() const;
	void SelectLine(size_t line);

	void AllowSelection(bool allow_selection);

	void InsertRow(wxSizer* sizer, size_t pos);
	void DeleteRow(size_t pos);
	void DeleteRow(wxSizer *sizer);
	void ClearRows();

protected:
	void AdjustView();

	virtual void OnDraw(wxDC& dc);

	DECLARE_EVENT_TABLE()
	void OnMouseEvent(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);

	int m_lineHeight{20};

	std::vector<wxSizer*> m_rows;

	std::set<size_t> m_selectedLines;

	static size_t const npos{static_cast<size_t>(-1)};

	size_t m_focusedLine{npos};

	bool m_allow_selection{true};

	DECLARE_DYNAMIC_CLASS(wxCustomHeightListCtrl)
};

#endif
