#ifndef FILEZILLA_INTERFACE_OVERLAY_HEADER
#define FILEZILLA_INTERFACE_OVERLAY_HEADER

#include <wx/popupwin.h>

class OverlayWindow : public wxPopupWindow
{
public:
	OverlayWindow(wxWindow* parent)
		: wxPopupWindow(parent)
	{}

	void SetAnchor(wxWindow* anchor, wxPoint const& offset);

private:
	void OnTLWMove(wxMoveEvent & evt);
	void OnTLWShow(wxShowEvent & evt);
	void OnTLWActivate(wxActivateEvent & evt);
	void OnAnchorSize(wxSizeEvent & evt);

	void UpdateShown();
	void Reposition();

	wxWindow* anchor_{};
	wxPoint offset_;
};

#endif
