#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_interface.h"
#include "../loginmanager.h"
#include "../Mainfrm.h"
#include "../power_management.h"
#include "../recentserverlist.h"
#include "../xrc_helper.h"
#include <libfilezilla/util.hpp>

BEGIN_EVENT_TABLE(COptionsPageInterface, COptionsPage)
EVT_CHECKBOX(XRCID("ID_FILEPANESWAP"), COptionsPageInterface::OnLayoutChange)
EVT_CHOICE(XRCID("ID_FILEPANELAYOUT"), COptionsPageInterface::OnLayoutChange)
EVT_CHOICE(XRCID("ID_MESSAGELOGPOS"), COptionsPageInterface::OnLayoutChange)
END_EVENT_TABLE()

bool COptionsPageInterface::LoadPage()
{
	bool failure = false;

	SetCheckFromOption(XRCID("ID_FILEPANESWAP"), OPTION_FILEPANE_SWAP, failure);
	SetChoice(XRCID("ID_FILEPANELAYOUT"), m_pOptions->GetOptionVal(OPTION_FILEPANE_LAYOUT), failure);

	SetChoice(XRCID("ID_MESSAGELOGPOS"), m_pOptions->GetOptionVal(OPTION_MESSAGELOG_POSITION), failure);

#ifndef __WXMAC__
	SetCheckFromOption(XRCID("ID_MINIMIZE_TRAY"), OPTION_MINIMIZE_TRAY, failure);
#endif

	SetCheckFromOption(XRCID("ID_PREVENT_IDLESLEEP"), OPTION_PREVENT_IDLESLEEP, failure);

	SetCheckFromOption(XRCID("ID_SPEED_DISPLAY"), OPTION_SPEED_DISPLAY, failure);

	if (!CPowerManagement::IsSupported()) {
		XRCCTRL(*this, "ID_PREVENT_IDLESLEEP", wxCheckBox)->Hide();
	}

	auto onChange = [this](wxEvent const&) {
		bool checked = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
		xrc_call(*this, "ID_MASTERPASSWORD", &wxControl::Enable, checked);
		xrc_call(*this, "ID_MASTERPASSWORD_REPEAT", &wxControl::Enable, checked);

	};	
	XRCCTRL(*this, "ID_PASSWORDS_SAVE", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);
	XRCCTRL(*this, "ID_PASSWORDS_NOSAVE", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);
	XRCCTRL(*this, "ID_PASSWORDS_USEMASTERPASSWORD", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);

	if (m_pOptions->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) || m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
		xrc_call(*this, "ID_PASSWORDS_NOSAVE", &wxRadioButton::SetValue, true);
		xrc_call(*this, "ID_PASSWORDS_SAVE", &wxControl::Disable);
		xrc_call(*this, "ID_PASSWORDS_NOSAVE", &wxControl::Disable);
		xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxControl::Disable);
	}
	else {
		if (m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0) {
			xrc_call(*this, "ID_PASSWORDS_NOSAVE", &wxRadioButton::SetValue, true);
		}
		else {
			auto key = public_key::from_base64(fz::to_utf8(m_pOptions->GetOption(OPTION_MASTERPASSWORDENCRYPTOR)));
			if (key) {
				xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::SetValue, true);
				xrc_call(*this, "ID_MASTERPASSWORD", &wxTextCtrl::SetHint, _("Leave empty to keep existing password.")); // @translator: Keep this string as short as possible
			}
			else {
				xrc_call(*this, "ID_PASSWORDS_SAVE", &wxRadioButton::SetValue, true);
			}
		}
	}
	onChange(wxCommandEvent());

	SetCheckFromOption(XRCID("ID_INTERFACE_SITEMANAGER_ON_STARTUP"), OPTION_INTERFACE_SITEMANAGER_ON_STARTUP, failure);

	int action = m_pOptions->GetOptionVal(OPTION_ALREADYCONNECTED_CHOICE);
	if (action & 2) {
		action = 1 + (action & 1);
	}
	else {
		action = 0;
	}
	SetChoice(XRCID("ID_NEWCONN_ACTION"), action, failure);

	m_pOwner->RememberOldValue(OPTION_MESSAGELOG_POSITION);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_LAYOUT);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_SWAP);

	return !failure;
}

void COptionsPageInterface::SavePasswordOption()
{
	if (!m_pOptions->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) && m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 2) {
		auto oldPub = public_key::from_base64(fz::to_utf8(m_pOptions->GetOption(OPTION_MASTERPASSWORDENCRYPTOR)));
		wxString const newPw = xrc_call(*this, "ID_MASTERPASSWORD", &wxTextCtrl::GetValue);

		bool useMaster = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
		if (useMaster && newPw.empty()) {
			// Keeping existing master password
			return;
		}

		CLoginManager loginManager;
		if (oldPub) {
			if (!loginManager.AskDecryptor(oldPub, true, true)) {
				return;
			}
		}

		if (useMaster) {
			auto priv = private_key::from_password(fz::to_utf8(newPw), fz::random_bytes(private_key::salt_size));
			auto pub = priv.pubkey();
			if (!pub) {
				wxMessageBox(_("Could not generate key"), _("Error"));
			}
			else {
				m_pOptions->SetOption(OPTION_DEFAULT_KIOSKMODE, 0);
				m_pOptions->SetOption(OPTION_MASTERPASSWORDENCRYPTOR, fz::to_wstring_from_utf8(pub.to_base64()));
			}
		}
		else {
			bool save = xrc_call(*this, "ID_PASSWORDS_SAVE", &wxRadioButton::GetValue);
			m_pOptions->SetOption(OPTION_DEFAULT_KIOSKMODE, save ? 0 : 1);
			m_pOptions->SetOption(OPTION_MASTERPASSWORDENCRYPTOR, std::wstring());
		}

		// Now actually change stored passwords

		{
			ServerWithCredentials last;
			if (m_pOptions->GetLastServer(last)) {
				loginManager.AskDecryptor(last.credentials.encrypted_, true, false);
				last.credentials.Unprotect(loginManager.GetDecryptor(last.credentials.encrypted_), true);
				m_pOptions->SetLastServer(last);
			}
		}

		{
			auto recentServers = CRecentServerList::GetMostRecentServers();
			for (auto & server : recentServers) {
				loginManager.AskDecryptor(server.credentials.encrypted_, true, false);
				server.credentials.Unprotect(loginManager.GetDecryptor(server.credentials.encrypted_), true);
			}
			CRecentServerList::SetMostRecentServers(recentServers);
		}

		for (auto state : *CContextManager::Get()->GetAllStates()) {
			auto site = state->GetLastSite();
			auto path = state->GetLastServerPath();
			loginManager.AskDecryptor(site.server_.credentials.encrypted_, true, false);
			site.server_.credentials.Unprotect(loginManager.GetDecryptor(site.server_.credentials.encrypted_), true);
			state->SetLastSite(site, path);
		}

		CSiteManager::Rewrite(loginManager, true);
	}
}

bool COptionsPageInterface::SavePage()
{
	SetOptionFromCheck(XRCID("ID_FILEPANESWAP"), OPTION_FILEPANE_SWAP);
	m_pOptions->SetOption(OPTION_FILEPANE_LAYOUT, GetChoice(XRCID("ID_FILEPANELAYOUT")));

	m_pOptions->SetOption(OPTION_MESSAGELOG_POSITION, GetChoice(XRCID("ID_MESSAGELOGPOS")));

#ifndef __WXMAC__
	SetOptionFromCheck(XRCID("ID_MINIMIZE_TRAY"), OPTION_MINIMIZE_TRAY);
#endif

	SetOptionFromCheck(XRCID("ID_PREVENT_IDLESLEEP"), OPTION_PREVENT_IDLESLEEP);

	SetOptionFromCheck(XRCID("ID_SPEED_DISPLAY"), OPTION_SPEED_DISPLAY);

	SavePasswordOption();

	SetOptionFromCheck(XRCID("ID_INTERFACE_SITEMANAGER_ON_STARTUP"), OPTION_INTERFACE_SITEMANAGER_ON_STARTUP);

	int action = GetChoice(XRCID("ID_NEWCONN_ACTION"));
	if (!action) {
		action = m_pOptions->GetOptionVal(OPTION_ALREADYCONNECTED_CHOICE) & 1;
	}
	else {
		action += 1;
	}
	m_pOptions->SetOption(OPTION_ALREADYCONNECTED_CHOICE, action);

	return true;
}

bool COptionsPageInterface::Validate()
{
	bool useMaster = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
	if (useMaster) {
		wxString pw = xrc_call(*this, "ID_MASTERPASSWORD", &wxTextCtrl::GetValue);
		wxString repeat = xrc_call(*this, "ID_MASTERPASSWORD_REPEAT", &wxTextCtrl::GetValue);
		if (pw != repeat) {
			return DisplayError(_T("ID_MASTERPASSWORD"), _("The entered passwords are not the same."));
		}

		auto key = public_key::from_base64(fz::to_utf8(m_pOptions->GetOption(OPTION_MASTERPASSWORDENCRYPTOR)));
		if (!key && pw.empty()) {
			return DisplayError(_T("ID_MASTERPASSWORD"), _("You need to enter a master password."));
		}

		if (!pw.empty() && pw.size() < 8) {
			return DisplayError(_T("ID_MASTERPASSWORD"), _("The master password needs to be at least 8 characters long."));
		}
	}
	return true;
}

void COptionsPageInterface::OnLayoutChange(wxCommandEvent&)
{
	m_pOptions->SetOption(OPTION_FILEPANE_LAYOUT, GetChoice(XRCID("ID_FILEPANELAYOUT")));
	m_pOptions->SetOption(OPTION_FILEPANE_SWAP, GetCheck(XRCID("ID_FILEPANESWAP")) ? 1 : 0);
	m_pOptions->SetOption(OPTION_MESSAGELOG_POSITION, GetChoice(XRCID("ID_MESSAGELOGPOS")));
}
