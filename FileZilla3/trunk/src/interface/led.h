#ifndef __LED_H__
#define __LED_H__

#include <wx/event.h>
#include <wx/timer.h>

DECLARE_EVENT_TYPE(fzEVT_UPDATE_LED_TOOLTIP, -1)

class CLed final : public wxWindow
{
public:
	CLed(wxWindow *parent, unsigned int index);

	void Ping();

protected:
	void Set();
	void Unset();

	int const m_index;
	int m_ledState;

	wxBitmap m_leds[2];
	bool m_loaded{};

	wxTimer m_timer;

	DECLARE_EVENT_TABLE()
	void OnPaint(wxPaintEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnEnterWindow(wxMouseEvent& event);
};

#endif //__LED_H__
