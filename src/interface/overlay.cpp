#include <filezilla.h>
#include "overlay.h"

void OverlayWindow::SetAnchor(wxWindow* anchor, wxPoint const& offset)
{
	anchor_ = anchor;
	offset_ = offset;

	wxGetTopLevelParent(GetParent())->Bind(wxEVT_MOVE, &OverlayWindow::OnTLWMove, this);
	anchor_->Bind(wxEVT_SIZE, &OverlayWindow::OnAnchorSize, this);

	wxGetTopLevelParent(GetParent())->Bind(wxEVT_SHOW, &OverlayWindow::OnTLWShow, this);
	wxGetTopLevelParent(GetParent())->Bind(wxEVT_ACTIVATE, &OverlayWindow::OnTLWActivate, this);

	Reposition();
	UpdateShown();
}

void OverlayWindow::OnTLWMove(wxMoveEvent & evt)
{
	evt.Skip();

	Reposition();
}

void OverlayWindow::UpdateShown()
{
	auto tlw = static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(GetParent()));
	Show(tlw->IsShown() && tlw->IsActive());
}

void OverlayWindow::OnTLWShow(wxShowEvent & evt)
{
	evt.Skip();
	CallAfter([this]() { UpdateShown(); });
}

void OverlayWindow::OnTLWActivate(wxActivateEvent & evt)
{
	evt.Skip();
	CallAfter([this]() { UpdateShown(); });
}

void OverlayWindow::Reposition()
{
	auto rect = anchor_->GetScreenRect();

	wxPoint pos = rect.GetTopLeft() + offset_;

	if (offset_.x < 0) {
		pos.x += rect.width - GetSize().GetWidth();
	}

	if (offset_.y < 0) {
		pos.y += rect.height - GetSize().GetHeight();
	}

	SetPosition(pos);
}

void OverlayWindow::OnAnchorSize(wxSizeEvent & evt)
{
	evt.Skip();

	Reposition();
}
