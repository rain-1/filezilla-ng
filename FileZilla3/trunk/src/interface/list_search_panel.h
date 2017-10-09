#ifndef FILEZILLA_INTERFACE_LIST_SEARCH_PANEL_HEADER
#define FILEZILLA_INTERFACE_LIST_SEARCH_PANEL_HEADER

#include <filter.h>

class wxBitmapButton;

class CListSearchPanel final : public wxWindow
{
public:
	CListSearchPanel(wxWindow* pParent, wxWindow* pListView, CStateFilterManager* pStateFilterManager, bool local);
	CListSearchPanel(CListSearchPanel const&) = delete;

	virtual bool Show(bool show);
	
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

	void Close();

	wxTextCtrl* m_textCtrl{};
	wxBitmapButton* m_optionsButton{};
	wxMenu* m_optionsMenu{};

	wxWindow* m_listView{};
	CStateFilterManager* m_filterManager{};

	bool m_local{};		// or remote
	wxString m_text;
	bool m_caseInsensitive{};
	bool m_useRegex{};
	bool m_invertFilter{};
	bool m_filters_disabled_sav{};
};

#endif
