#include <filezilla.h>
#include "welcome_dialog.h"
#include "buildinfo.h"
#include <wx/hyperlink.h>
#include "Options.h"
#include "xrc_helper.h"

BEGIN_EVENT_TABLE(CWelcomeDialog, wxDialogEx)
EVT_TIMER(wxID_ANY, CWelcomeDialog::OnTimer)
END_EVENT_TABLE()

bool CWelcomeDialog::Run(wxWindow* parent, bool force /*=false*/, bool delay /*=false*/)
{
	const wxString ownVersion = CBuildInfo::GetVersion();
	wxString greetingVersion = COptions::Get()->GetOption(OPTION_GREETINGVERSION);

	wxString const resources = COptions::Get()->GetOption(OPTION_GREETINGRESOURCES);
	COptions::Get()->SetOption(OPTION_GREETINGRESOURCES, _T(""));

	if (!force) {
		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
			if (delay)
				delete this;
			return true;
		}

		if (!greetingVersion.empty() &&
			CBuildInfo::ConvertToVersionNumber(ownVersion.c_str()) <= CBuildInfo::ConvertToVersionNumber(greetingVersion.c_str()))
		{
			// Been there done that
			if (delay)
				delete this;
			return true;
		}
		COptions::Get()->SetOption(OPTION_GREETINGVERSION, ownVersion.ToStdWstring());

		if (greetingVersion.empty() && !COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE))
			COptions::Get()->SetOption(OPTION_PROMPTPASSWORDSAVE, 1);
	}

	if (!Load(parent, _T("ID_WELCOME"))) {
		if (delay) {
			delete this;
		}
		return false;
	}

	InitFooter(force ? wxString() : resources);

	xrc_call(*this, "ID_FZVERSION", &wxStaticText::SetLabel, _T("FileZilla ") + CBuildInfo::GetVersion());

	wxString const url = _T("https://welcome.filezilla-project.org/welcome?type=client&category=%s&version=") + ownVersion;

	if (!greetingVersion.empty()) {
		xrc_call(*this, "ID_LINK_NEWS", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("news")) + _T("&oldversion=") + greetingVersion);
		xrc_call(*this, "ID_LINK_NEWS", &wxHyperlinkCtrl::SetLabel, wxString::Format(_("New features and improvements in %s"), CBuildInfo::GetVersion()));
	}
	else {
		xrc_call(*this, "ID_LINK_NEWS", &wxHyperlinkCtrl::Hide);
		xrc_call(*this, "ID_HEADING_NEWS", &wxStaticText::Hide);
	}

	xrc_call(*this, "ID_DOCUMENTATION_BASIC", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("documentation_basic")));
	xrc_call(*this, "ID_DOCUMENTATION_NETWORK", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("documentation_network")));
	xrc_call(*this, "ID_DOCUMENTATION_MORE", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("documentation_more")));
	xrc_call(*this, "ID_SUPPORT_FORUM", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("support_forum")));
	xrc_call(*this, "ID_SUPPORT_MORE", &wxHyperlinkCtrl::SetURL, wxString::Format(url, _T("support_more")));

#ifdef FZ_WINDOWS
	// Add phone support link in official Windows builds builds...
	if (CBuildInfo::GetBuildType() == _T("official")) {
		auto lang = wxGetLocale() ? wxGetLocale()->GetName() : wxString();
		// but only in English...
		if (lang.StartsWith(_T("en"))) {
			auto const now = fz::datetime::now();
			// while the build is fresh...
			if ((now - CBuildInfo::GetBuildDate()).get_days() < 60) {
				// and only for US and Canada, so limit by timezone
				auto ref = fz::datetime(now.format("%Y%m%d%H%M%S", fz::datetime::utc), fz::datetime::utc);
				auto offset = fz::datetime(ref.format("%Y%m%d%H%M%S", fz::datetime::utc), fz::datetime::local);
				auto diff = (ref - offset).get_hours();
				if (diff >= -9 && diff <= -3) {
					auto sizer = xrc_call(*this, "ID_SUPPORT_MORE", &wxWindow::GetContainingSizer);
					if (sizer) {
						auto link = new wxHyperlinkCtrl(sizer->GetContainingWindow(), wxID_ANY, _T("Phone support"), _T("https://filezilla-project.org/phone_support.php"));
						sizer->Insert(0, link);
					}
				}
			}
		}
	}
#endif

	Layout();

	GetSizer()->Fit(this);

	if (delay) {
		m_delayedShowTimer.SetOwner(this);
		m_delayedShowTimer.Start(10, true);
	}
	else
		ShowModal();

	return true;
}

void CWelcomeDialog::OnTimer(wxTimerEvent&)
{
	ShowModal();
	Destroy();
}

#if FZ_WINDOWS && FZ_MANUALUPDATECHECK
void MakeLinksFromTooltips(wxWindow& parent);

namespace {
void CreateMessagePanel(wxWindow& dlg, char const* ctrl, wxXmlResource& resource, wxString const& resourceName)
{
	wxWindow* parent = XRCCTRL(dlg, ctrl, wxPanel);
	if (parent) {
		wxPanel* p = new wxPanel();
		if (resource.LoadPanel(p, parent, resourceName)) {
			wxSize minSize = p->GetSizer()->GetMinSize();
			parent->SetInitialSize(minSize);
			MakeLinksFromTooltips(*p);
		}
		else {
			delete p;
		}
	}
}
}
#endif

void CWelcomeDialog::InitFooter(wxString const& resources)
{
#if FZ_WINDOWS && FZ_MANUALUPDATECHECK
	if (CBuildInfo::GetBuildType() == _T("official") && !COptions::Get()->GetOptionVal(OPTION_DISABLE_UPDATE_FOOTER)) {
		if (!resources.empty()) {
			wxLogNull null;

			wxXmlResource res(wxXRC_NO_RELOADING);
			InitHandlers(res);
			if (res.Load(_T("blob:") + resources)) {
				CreateMessagePanel(*this, "ID_HEADERMESSAGE_PANEL", res, _T("ID_WELCOME_HEADER"));
				CreateMessagePanel(*this, "ID_FOOTERMESSAGE_PANEL", res, _T("ID_WELCOME_FOOTER"));
			}
		}
	}
#else
	(void)resources;
#endif
}
