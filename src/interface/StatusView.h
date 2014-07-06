#ifndef __STATUSVIEW_H__
#define __STATUSVIEW_H__

#ifdef __WXMSW__
#include "richedit.h"
#endif

class CFastTextCtrl;
class CStatusView : public wxWindow
{
public:
	CStatusView(wxWindow* parent, wxWindowID id);
	virtual ~CStatusView();

	void AddToLog(CLogmsgNotification *pNotification);
	void AddToLog(MessageType messagetype, const wxString& message, const wxDateTime& time);

	void InitDefAttr();

	virtual void SetFocus();

	virtual bool Show(bool show = true);

protected:

	int m_nLineCount;
	wxString m_Content;
	CFastTextCtrl *m_pTextCtrl;

	void OnSize(wxSizeEvent &event);

	DECLARE_EVENT_TABLE()
	void OnContextMenu(wxContextMenuEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);

	std::list<int> m_lineLengths;

	struct t_attributeCache
	{
		wxString prefix;
		int len;
		wxTextAttr attr;
#ifdef __WXMSW__
		CHARFORMAT2 cf;
#endif
	} m_attributeCache[static_cast<int>(MessageType::count)];

	bool m_rtl;

	bool m_shown;

	// Don't update actual log window if not shown,
	// do it later when showing the window.
	struct t_line
	{
		MessageType messagetype;
		wxString message;
		wxDateTime time;
	};
	std::list<t_line> m_hiddenLines;

	bool m_showTimestamps;
	wxDateTime m_lastTime;
	wxString m_lastTimeString;
};

#endif

