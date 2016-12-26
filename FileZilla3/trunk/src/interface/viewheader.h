#ifndef __VIEWHEADER_H__
#define __VIEWHEADER_H__

#include "state.h"

class CComboBoxEx;
class CWindowTinter;
class CViewHeader : public wxNavigationEnabled<wxWindow>
{
	friend class CComboBoxEx;
public:
	CViewHeader(wxWindow* pParent, const wxString& label);

	// Reparents the control.
	// Calls wxWindow::Reparent under WXMSW and WXGTK. On other systems,
	// a new instance is created with the same content (but with different
	// parent) and the provided pointer to pViewHeader will be modified.
	static void Reparent(CViewHeader** pViewHeader, wxWindow* parent);

	void SetLabel(const wxString& label);
	wxString GetLabel() const;

	virtual void SetFocus();

protected:
	CComboBoxEx* m_pComboBox{};
	wxStaticText* m_pLabel{};
	bool m_alreadyInPaint{};

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent& event);

	void OnPaint(wxPaintEvent& event);
#ifdef __WXMSW__
	void OnComboPaint(wxPaintEvent& event);
	void OnComboMouseEvent(wxMouseEvent& event);
	bool m_bLeftMousePressed{};
#endif //__WXMSW__

	void AddRecentDirectory(const wxString &directory);
	std::deque<wxString> m_recentDirectories; // ordered by age
	std::list<wxString> m_sortedRecentDirectories; // ordered by sort order

	int m_cbOffset{};
	int m_labelHeight{};

	std::unique_ptr<CWindowTinter> m_windowTinter;
};

class CLocalViewHeader final : public CViewHeader, CStateEventHandler
{
public:
	CLocalViewHeader(wxWindow* pParent, CState& state);

protected:
	virtual void OnStateChange(t_statechange_notifications notification, const wxString& data, const void* data2);

	wxString m_oldValue;

	DECLARE_EVENT_TABLE()
	void OnTextChanged(wxCommandEvent& event);
	void OnTextEnter(wxCommandEvent& event);
	void OnSelectionChanged(wxCommandEvent& event);
#ifdef __WXGTK__
	void OnSelectTextEvent(wxCommandEvent& event);

	wxString m_autoCompletionText;
#endif
};

class CRemoteViewHeader final : public CViewHeader, CStateEventHandler
{
public:
	CRemoteViewHeader(wxWindow* pParent, CState& state);

protected:
	virtual void OnStateChange(t_statechange_notifications notification, const wxString& data, const void* data2);

	CServer m_lastServer;
	CServerPath m_path;

	DECLARE_EVENT_TABLE()
	void OnTextChanged(wxCommandEvent& event);
	void OnTextEnter(wxCommandEvent& event);
	void OnSelectionChanged(wxCommandEvent& event);
};

#endif //__VIEWHEADER_H__
