#include <filezilla.h>

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_themes.h"
#include "../themeprovider.h"

#include <wx/dcclient.h>
#include <wx/scrolwin.h>

#include "xrc_helper.h"

BEGIN_EVENT_TABLE(COptionsPageThemes, COptionsPage)
EVT_CHOICE(XRCID("ID_THEME"), COptionsPageThemes::OnThemeChange)
END_EVENT_TABLE()

const int BORDER = 5;

class CIconPreview final : public wxScrolledWindow
{
public:
	CIconPreview() = default;

	CIconPreview(wxWindow* pParent)
		: wxScrolledWindow(pParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSTATIC_BORDER | wxVSCROLL)
	{
	}

	void LoadIcons(std::wstring const& theme, wxSize const& size)
	{
		m_iconSize = size;

		m_icons = CThemeProvider::Get()->GetAllImages(theme, size);

		m_sizeInitialized = false;
		Refresh();
	}

	void CalcSize()
	{
		if (m_sizeInitialized) {
			return;
		}
		m_sizeInitialized = true;

		wxSize size = GetClientSize();

		if (!m_icons.empty()) {
			int icons_per_line = wxMax(1, (size.GetWidth() - BORDER) / (m_iconSize.GetWidth() + BORDER));

			// Number of lines and line height
			int lines = (m_icons.size() - 1) / icons_per_line + 1;
			int vheight = lines * (m_iconSize.GetHeight() + BORDER) + BORDER;
			if (vheight > size.GetHeight()) {
				// Scroll bar would appear, need to adjust width
				size.SetHeight(vheight);
				SetVirtualSize(size);
				SetScrollRate(0, m_iconSize.GetHeight() + BORDER);

				wxSize size2 = GetClientSize();
				size.SetWidth(size2.GetWidth());

				icons_per_line = wxMax(1, (size.GetWidth() - BORDER) / (m_iconSize.GetWidth() + BORDER));
				lines = (m_icons.size() - 1) / icons_per_line + 1;
				vheight = lines * (m_iconSize.GetHeight() + BORDER) + BORDER;
				if (vheight > size.GetHeight())
					size.SetHeight(vheight);
			}

			// Calculate extra padding
			if (icons_per_line > 1) {
				int extra = size.GetWidth() - BORDER - icons_per_line * (m_iconSize.GetWidth() + BORDER);
				m_extra_padding = extra / (icons_per_line - 1);
			}
		}
		SetVirtualSize(size);
		SetScrollRate(0, m_iconSize.GetHeight() + BORDER);
	}

protected:
	DECLARE_EVENT_TABLE()
	virtual void OnPaint(wxPaintEvent&)
	{
		CalcSize();

		wxPaintDC dc(this);
		PrepareDC(dc);

		wxSize size = GetClientSize();

		if (m_icons.empty()) {
			dc.SetFont(GetFont());
			wxString text = _("No images available");
			wxCoord w, h;
			dc.GetTextExtent(text, &w, &h);
			dc.DrawText(text, (size.GetWidth() - w) / 2, (size.GetHeight() - h) / 2);
			return;
		}

		int x = BORDER;
		int y = BORDER;

		for (auto const& bmp : m_icons) {
			dc.DrawBitmap(bmp, x, y, true);
			x += m_iconSize.GetWidth() + BORDER + m_extra_padding;
			if ((x + m_iconSize.GetWidth() + BORDER) > size.GetWidth()) {
				x = BORDER;
				y += m_iconSize.GetHeight() + BORDER;
			}
		}
	}

	std::vector<wxBitmap> m_icons;
	wxSize m_iconSize;
	bool m_sizeInitialized{};
	int m_extra_padding{};

	DECLARE_DYNAMIC_CLASS(CIconPreview)
};

IMPLEMENT_DYNAMIC_CLASS(CIconPreview, wxScrolledWindow)

BEGIN_EVENT_TABLE(CIconPreview, wxScrolledWindow)
EVT_PAINT(CIconPreview::OnPaint)
END_EVENT_TABLE()

COptionsPageThemes::~COptionsPageThemes()
{
}

bool COptionsPageThemes::CreatePage(COptions* pOptions, CSettingsDialog* pOwner, wxWindow* parent, wxSize& maxSize)
{
	bool success = COptionsPage::CreatePage(pOptions, pOwner, parent, maxSize);
	if (success) {
		auto dummy = XRCCTRL(*this, "ID_SCALE", wxWindow);
		auto sizer = dummy ? dummy->GetContainingSizer() : 0;
		if (!sizer) {
			return false;
		}

		wxWindow* sizerParent = dummy->GetParent();
		sizer->Detach(dummy);
		delete dummy;

		auto scale = new wxSpinCtrlDouble(sizerParent, XRCID("ID_SCALE"));
		scale->SetRange(0.5, 4);
		scale->SetIncrement(0.25);
		scale->SetValue(1.25);
		scale->SetDigits(2);
		sizer->Add(scale, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
		sizer->Layout();
		GetSizer()->Layout();
		GetSizer()->Fit(this);

		scale->Connect(wxEVT_SPINCTRLDOUBLE, wxCommandEventHandler(COptionsPageThemes::OnThemeChange), 0, this);
	}

	return success;
}

bool COptionsPageThemes::LoadPage()
{
	return true;
}

bool COptionsPageThemes::SavePage()
{
	if (!m_was_selected) {
		return true;
	}

	wxChoice* pTheme = XRCCTRL(*this, "ID_THEME", wxChoice);

	const int sel = pTheme->GetSelection();
	const wxString theme = ((wxStringClientData*)pTheme->GetClientObject(sel))->GetData();

	m_pOptions->SetOption(OPTION_ICONS_THEME, theme.ToStdWstring());

	m_pOptions->SetOption(OPTION_ICONS_SCALE, static_cast<int>(100 * xrc_call(*this, "ID_SCALE", &wxSpinCtrlDouble::GetValue)));

	return true;
}

bool COptionsPageThemes::Validate()
{
	return true;
}

bool COptionsPageThemes::DisplayTheme(std::wstring const& theme)
{
	std::wstring name, author, mail;
	if (!CThemeProvider::Get()->GetThemeData(theme, name, author, mail)) {
		return false;
	}
	if (name.empty()) {
		return false;
	}

	if (author.empty()) {
		author = _("N/a").ToStdWstring();
	}
	if (mail.empty()) {
		mail = _("N/a").ToStdWstring();
	}

	bool failure = false;
	SetStaticText(XRCID("ID_AUTHOR"), author, failure);
	SetStaticText(XRCID("ID_EMAIL"), mail, failure);

	auto scale_factor = xrc_call(*this, "ID_SCALE", &wxSpinCtrlDouble::GetValue);
	wxSize size = CThemeProvider::Get()->GetIconSize(iconSizeSmall);
	size.Scale(scale_factor, scale_factor);

	xrc_call(*this, "ID_PREVIEW", &CIconPreview::LoadIcons, theme, size);

	return !failure;
}

void COptionsPageThemes::OnThemeChange(wxCommandEvent&)
{
	wxChoice* pTheme = XRCCTRL(*this, "ID_THEME", wxChoice);

	const int sel = pTheme->GetSelection();
	std::wstring const theme = ((wxStringClientData*)pTheme->GetClientObject(sel))->GetData().ToStdWstring();
	DisplayTheme(theme);
}

bool COptionsPageThemes::OnDisplayedFirstTime()
{
	bool failure = false;

	wxChoice* pTheme = XRCCTRL(*this, "ID_THEME", wxChoice);
	if (!pTheme) {
		return false;
	}

	if (!pTheme || !XRCCTRL(*this, "ID_PREVIEW", CIconPreview) ||
		!XRCCTRL(*this, "ID_AUTHOR", wxStaticText) ||
		!XRCCTRL(*this, "ID_EMAIL", wxStaticText))
	{
		return false;
	}

	auto const themes = CThemeProvider::GetThemes();
	if (themes.empty()) {
		return false;
	}

	xrc_call<wxSpinCtrlDouble, double>(*this, "ID_SCALE", &wxSpinCtrlDouble::SetValue, static_cast<double>(m_pOptions->GetOptionVal(OPTION_ICONS_SCALE)) / 100.f);

	std::wstring activeTheme = m_pOptions->GetOption(OPTION_ICONS_THEME);
	std::wstring firstName;
	for (auto const& theme : themes) {
		std::wstring name, author, mail;
		if (!CThemeProvider::Get()->GetThemeData(theme, name, author, mail)) {
			continue;
		}
		if (firstName.empty()) {
			firstName = name;
		}
		int n = pTheme->Append(name, new wxStringClientData(theme));
		if (theme == activeTheme) {
			pTheme->SetSelection(n);
		}
	}
	if (pTheme->GetSelection() == wxNOT_FOUND) {
		pTheme->SetSelection(pTheme->FindString(firstName));
	}
	activeTheme = ((wxStringClientData*)pTheme->GetClientObject(pTheme->GetSelection()))->GetData();

	if (!DisplayTheme(activeTheme)) {
		failure = true;
	}

	pTheme->GetContainingSizer()->Layout();

	return !failure;
}
