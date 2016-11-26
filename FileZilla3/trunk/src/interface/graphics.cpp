#include <filezilla.h>

#include "graphics.h"

CWindowTinter::CWindowTinter(wxWindow& wnd)
	: m_wnd(wnd)
{
}

void CWindowTinter::SetBackgroundTint(wxColour const& tint)
{
	if (!m_originalColor.IsOk()) {
		m_originalColor = m_wnd.GetBackgroundColour();
	}

	wxColour const newColour = AlphaComposite_Over(m_originalColor, tint);
	if (newColour != m_wnd.GetBackgroundColour()) {
		if (m_wnd.SetBackgroundColour(newColour)) {
			m_wnd.Refresh();
		}
	}
}

void Overlay(wxBitmap& bg, wxBitmap const& fg)
{
	if (!bg.IsOk() || !fg.IsOk()) {
		return;
	}

	wxImage foreground = fg.ConvertToImage();
	if (!foreground.HasAlpha()) {
		foreground.InitAlpha();
	}

	wxImage background = bg.ConvertToImage();
	if (!background.HasAlpha()) {
		background.InitAlpha();
	}

	if (foreground.GetSize() != background.GetSize()) {
		foreground.Rescale(background.GetSize().x, background.GetSize().y, wxIMAGE_QUALITY_HIGH);
	}

	unsigned char* bg_data = background.GetData();
	unsigned char* bg_alpha = background.GetAlpha();
	unsigned char* fg_data = foreground.GetData();
	unsigned char* fg_alpha = foreground.GetAlpha();
	unsigned char* bg_end = bg_data + background.GetWidth() * background.GetHeight() * 3;
	while (bg_data != bg_end) {
		AlphaComposite_Over_Inplace(
			*bg_data, *(bg_data + 1), *(bg_data + 2), *bg_alpha,
			*fg_data, *(fg_data + 1), *(fg_data + 2), *fg_alpha);
		bg_data += 3;
		fg_data += 3;
		++bg_alpha;
		++fg_alpha;
	}

#ifdef __WXMAC__
	bg = wxBitmap(background, -1, bg.GetScaleFactor());
#else
	bg = wxBitmap(background, -1);
#endif
}

