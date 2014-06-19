#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_language.h"
#include "../filezillaapp.h"
#include <algorithm>

BEGIN_EVENT_TABLE(COptionsPageLanguage, COptionsPage)
END_EVENT_TABLE()

bool COptionsPageLanguage::LoadPage()
{
	return true;
}

bool COptionsPageLanguage::SavePage()
{
	if (!m_was_selected)
		return true;

	wxListBox* pListBox = XRCCTRL(*this, "ID_LANGUAGES", wxListBox);

	if (pListBox->GetSelection() == wxNOT_FOUND)
		return true;

	const int selection = pListBox->GetSelection();
	wxString code;
	if (selection > 0)
		code = m_locale[selection - 1].code;

#ifdef __WXGTK__
	m_pOptions->SetOption(OPTION_LANGUAGE, code);
#else
	bool successful = false;
	if (code.empty()) {
		wxGetApp().SetLocale(wxLANGUAGE_DEFAULT);

		// Default language cannot fail, has to silently fall back to English
		successful = true;
	}
	else {
		const wxLanguageInfo* pInfo = wxLocale::FindLanguageInfo(code);
		if (pInfo)
			successful = wxGetApp().SetLocale(pInfo->Language);
	}

	if (successful)
		m_pOptions->SetOption(OPTION_LANGUAGE, code);
	else
		wxMessageBoxEx(wxString::Format(_("Failed to set language to %s, using default system language"), pListBox->GetStringSelection().c_str()), _("Failed to change language"), wxICON_EXCLAMATION, this);
#endif
	return true;
}

bool COptionsPageLanguage::Validate()
{
	return true;
}

static bool compareLangAsc(const struct COptionsPageLanguage::_locale_info &lang1, const struct COptionsPageLanguage::_locale_info &lang2)
{
	return lang1.name.wxString::Cmp(lang2.name) <= 0;
}

bool COptionsPageLanguage::OnDisplayedFirstTime()
{
	wxListBox* pListBox = XRCCTRL(*this, "ID_LANGUAGES", wxListBox);
	if (!pListBox)
		return false;

	wxString currentLanguage = m_pOptions->GetOption(OPTION_LANGUAGE);

	pListBox->Clear();

	const wxString defaultName = _("Default system language");
	int n = pListBox->Append(defaultName);
	if (currentLanguage.empty())
		pListBox->SetSelection(n);

	m_locale.push_back(_locale_info());
	m_locale.back().code = _T("en_US");
	m_locale.back().name = _T("English");

	wxString localesDir = wxGetApp().GetLocalesDir();
	if (localesDir.empty() || !wxDir::Exists(localesDir)) {
		pListBox->GetContainingSizer()->Layout();
		return true;
	}

	wxDir dir(localesDir);
	wxString locale;
	for (bool found = dir.GetFirst(&locale); found; found = dir.GetNext(&locale)) {
		if (!wxFileName::FileExists(localesDir + locale + _T("/filezilla.mo"))) {
			if (!wxFileName::FileExists(localesDir + locale + _T("/LC_MESSAGES/filezilla.mo")))
				continue;
		}

		wxString name;
		const wxLanguageInfo* pInfo = wxLocale::FindLanguageInfo(locale);
		if (!pInfo)
			continue;
		if (!pInfo->Description.empty())
			name = pInfo->Description;
		else
			name = locale;

		m_locale.push_back({name, locale});
	}

	std::sort(m_locale.begin(), m_locale.end(), compareLangAsc);

	for(auto const& locale : m_locale) {
		n = pListBox->Append(locale.name + _T(" (") + locale.code + _T(")"));
		if (locale.code == currentLanguage)
			pListBox->SetSelection(n);
	}
	pListBox->GetContainingSizer()->Layout();

	return true;
}
