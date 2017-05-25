#include "filezilla.h"
#include "asksavepassworddialog.h"
#include "Options.h"
#include "filezillaapp.h"
#include "xrc_helper.h"
#include "sitemanager.h"
#include <libfilezilla/util.hpp>

bool CAskSavePasswordDialog::Create(wxWindow*)
{
	if (!Load(0, _T("ID_ASK_SAVE_PASSWORD"))) {
		return false;
	}

	wxGetApp().GetWrapEngine()->WrapRecursive(this, 2, "");

	auto onChange = [this](wxEvent const&) {
		bool checked = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
		xrc_call(*this, "ID_MASTERPASSWORD", &wxControl::Enable, checked);
		xrc_call(*this, "ID_MASTERPASSWORD_REPEAT", &wxControl::Enable, checked);

	};
	onChange(wxCommandEvent());

	XRCCTRL(*this, "ID_PASSWORDS_SAVE", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);
	XRCCTRL(*this, "ID_PASSWORDS_NOSAVE", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);
	XRCCTRL(*this, "ID_PASSWORDS_USEMASTERPASSWORD", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);

	XRCCTRL(*this, "wxID_OK", wxButton)->Bind(wxEVT_BUTTON, &CAskSavePasswordDialog::OnOk, &*this);

	return true;
}

void CAskSavePasswordDialog::OnOk(wxCommandEvent& event)
{
	bool useMaster = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
	if (useMaster) {
		wxString pw = xrc_call(*this, "ID_MASTERPASSWORD", &wxTextCtrl::GetValue);
		wxString repeat = xrc_call(*this, "ID_MASTERPASSWORD_REPEAT", &wxTextCtrl::GetValue);
		if (pw != repeat) {
			wxMessageBox(_("The entered passwords are not the same."), _("Invalid input"));
			return;
		}

		if (pw.size() < 8) {
			wxMessageBox(_("The master password needs to be at least 8 characters long."), _("Invalid input"));
			return;
		}

		auto priv = private_key::from_password(fz::to_utf8(pw), fz::random_bytes(private_key::salt_size));
		auto pub = priv.pubkey();
		if (!pub) {
			wxMessageBox(_("Could not generate key"), _("Error"));
			return;
		}
		else {
			COptions::Get()->SetOption(OPTION_DEFAULT_KIOSKMODE, 0);
			COptions::Get()->SetOption(OPTION_MASTERPASSWORDENCRYPTOR, fz::to_wstring_from_utf8(pub.to_base64()));
		}
	}
	else {
		bool save = xrc_call(*this, "ID_PASSWORDS_SAVE", &wxRadioButton::GetValue);
		COptions::Get()->SetOption(OPTION_DEFAULT_KIOSKMODE, save ? 0 : 1);
		COptions::Get()->SetOption(OPTION_MASTERPASSWORDENCRYPTOR, std::wstring());
	}
	event.Skip();
}

bool CAskSavePasswordDialog::Run(wxWindow* parent)
{
	bool ret = true;

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 0 && COptions::Get()->GetOptionVal(OPTION_PROMPTPASSWORDSAVE) != 0 &&
		!CSiteManager::HasSites() && COptions::Get()->GetOption(OPTION_MASTERPASSWORDENCRYPTOR).empty())
	{
		CAskSavePasswordDialog dlg;
		if (dlg.Create(parent)) {
			ret = dlg.ShowModal() == wxID_OK;
			if (ret) {
				COptions::Get()->SetOption(OPTION_PROMPTPASSWORDSAVE, 0);
			}
		}
	}
	else {
		COptions::Get()->SetOption(OPTION_PROMPTPASSWORDSAVE, 0);
	}

	return ret;
}
