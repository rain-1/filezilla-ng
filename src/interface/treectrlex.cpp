#include <filezilla.h>
#include "treectrlex.h"

IMPLEMENT_CLASS(wxTreeCtrlEx, wxTreeCtrl)

wxTreeCtrlEx::wxTreeCtrlEx(wxWindow *parent, wxWindowID id /*=wxID_ANY*/,
			   const wxPoint& pos /*=wxDefaultPosition*/,
			   const wxSize& size /*=wxDefaultSize*/,
			   long style /*=wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT*/)
	: wxTreeCtrl(parent, id, pos, size, style), m_setSelection(false)
{
}

void wxTreeCtrlEx::SafeSelectItem(const wxTreeItemId& item)
{
	if( !item ) {
		m_setSelection = true;
		UnselectAll();
		m_setSelection = false;
	}
	else {
		const wxTreeItemId old_selection = GetSelection();

		m_setSelection = true;
		SelectItem(item);
		m_setSelection = false;
		if (item != old_selection)
			EnsureVisible(item);
	}
}
