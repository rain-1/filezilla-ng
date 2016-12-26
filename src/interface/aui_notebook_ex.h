#ifndef __AUI_NOTEBOOK_EX_H__
#define __AUI_NOTEBOOK_EX_H__

#include <wx/aui/aui.h>

#include <map>

class wxAuiTabArtEx;
class wxAuiNotebookEx : public wxAuiNotebook
{
public:
	wxAuiNotebookEx();
	virtual ~wxAuiNotebookEx();

	void RemoveExtraBorders();

	void SetExArtProvider();

	// Basically identical to the AUI one, but not calling Update
	virtual bool SetPageText(size_t page_idx, const wxString& text) final;

	void Highlight(size_t page, bool highlight = true);
	bool Highlighted(size_t page) const;

	void AdvanceTab(bool forward);

	virtual bool AddPage(wxWindow *page, const wxString &text, bool select = false, int imageId = -1) final;

	virtual bool RemovePage(size_t page) final;

	void SetTabColour(size_t page, wxColour const& c);
	wxColour GetTabColour(wxWindow* page);

protected:
	std::vector<bool> m_highlighted;

	std::map<wxWindow*, wxColour> m_colourMap;

	DECLARE_EVENT_TABLE()
	void OnPageChanged(wxAuiNotebookEvent& event);
	void OnTabDragMotion(wxAuiNotebookEvent& evt);
};

#endif //__AUI_NOTEBOOK_EX_H__
