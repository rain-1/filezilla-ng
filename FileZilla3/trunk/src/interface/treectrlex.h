#ifndef __TREECTRLEX_H__
#define __TREECTRLEX_H__

#include <wx/dnd.h>

class wxTreeCtrlEx : public wxNavigationEnabled<wxTreeCtrl>
{
	DECLARE_CLASS(wxTreeCtrlEx)

public:
	typedef wxTreeItemId Item;

	wxTreeCtrlEx(wxWindow *parent, wxWindowID id = wxID_ANY,
			   const wxPoint& pos = wxDefaultPosition,
			   const wxSize& size = wxDefaultSize,
			   long style = wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT);
	void SafeSelectItem(const wxTreeItemId& item);

	// Small wrappers to make wxTreeCtrl(Ex) API more similar to wxListCtrl(ex).
	int GetItemCount() const { return GetCount(); }
	wxTreeItemId GetTopItem() const { return GetFirstVisibleItem(); }
	bool GetItemRect(wxTreeItemId const& item, wxRect &rect) const { return GetBoundingRect(item, rect); }
	
	wxRect GetActualClientRect() const { return GetClientRect(); }

	bool Valid(wxTreeItemId const& i) const { return i.IsOk(); }

	wxWindow* GetMainWindow() { return this; }

	// Items with a collapsed ancestor are not included
	wxTreeItemId GetFirstItem() const;
	wxTreeItemId GetLastItem() const;
	wxTreeItemId GetBottomItem() const;

	wxTreeItemId GetNextItemSimple(wxTreeItemId const& item) const;
	wxTreeItemId GetPrevItemSimple(wxTreeItemId const& item) const;

protected:

	bool m_setSelection{};

#ifdef __WXMAC__
	DECLARE_EVENT_TABLE()
	void OnChar(wxKeyEvent& event);
#endif
};

#endif //__TREECTRLEX_H__
