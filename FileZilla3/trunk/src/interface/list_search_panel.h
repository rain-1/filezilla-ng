#ifndef FILEZILLA_INTERFACE_LIST_SEARCH_PANEL_HEADER
#define FILEZILLA_INTERFACE_LIST_SEARCH_PANEL_HEADER

#include <filter.h>

class wxBitmapButton;

class CListSearchPanel final : public wxWindow
{
public:
	CListSearchPanel(wxWindow* pParent, wxWindow* pListView, CState* pState, bool local);
	CListSearchPanel(CListSearchPanel const&) = delete;

	virtual bool Show(bool show);
	void Close();
	
private:
	wxDECLARE_EVENT_TABLE();
	void OnPaint(wxPaintEvent& ev);
	void OnText(wxCommandEvent& ev);
	void OnOptions(wxCommandEvent& ev);
	void OnClose(wxCommandEvent& ev);
	void OnCaseInsensitive(wxCommandEvent& ev);
	void OnUseRegex(wxCommandEvent& ev);
	void OnInvertFilter(wxCommandEvent& ev);
	void OnTextKeyDown(wxKeyEvent& event);

	void ApplyFilter();
	void ResetFilter();

	wxTextCtrl* m_textCtrl{};
	wxBitmapButton* m_optionsButton{};
	wxMenu* m_optionsMenu{};

	wxWindow* m_listView{};
	CState* m_pState{};

	bool m_local{};		// or remote
	wxString m_text;
	bool m_caseInsensitive{true};
	bool m_useRegex{};
	bool m_invertFilter{};
};

#endif
