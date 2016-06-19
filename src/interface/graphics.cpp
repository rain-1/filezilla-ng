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
