#ifndef __TREECTRLEX_H__
#define __TREECTRLEX_H__

class wxTreeCtrlEx : public wxNavigationEnabled<wxTreeCtrl>
{
	DECLARE_CLASS(wxTreeCtrlEx)

public:
	wxTreeCtrlEx(wxWindow *parent, wxWindowID id = wxID_ANY,
			   const wxPoint& pos = wxDefaultPosition,
			   const wxSize& size = wxDefaultSize,
			   long style = wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT);
	void SafeSelectItem(const wxTreeItemId& item);

protected:
	bool m_setSelection{};

#ifdef __WXMAC__
	DECLARE_EVENT_TABLE()
	void OnChar(wxKeyEvent& event);
#endif
};

#endif //__TREECTRLEX_H__
