#include <filezilla.h>

#include "aboutdialog.h"
#include "buildinfo.h"
#include "xrc_helper.h"
#include "Options.h"
#include "themeprovider.h"

#include <misc.h>

#include <wx/hyperlink.h>
#include <wx/clipbrd.h>

BEGIN_EVENT_TABLE(CAboutDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CAboutDialog::OnOK)
EVT_BUTTON(XRCID("ID_COPY"), CAboutDialog::OnCopy)
END_EVENT_TABLE()

bool CAboutDialog::Create(wxWindow* parent)
{
	if (!Load(parent, _T("ID_ABOUT"))) {
		return false;
	}

	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap("ART_FILEZILLA", wxString(), CThemeProvider::GetIconSize(iconSizeLarge));
	xrc_call(*this, "ID_FILEZILLA_LOGO", &wxStaticBitmap::SetBitmap, bmp);

	xrc_call(*this, "ID_URL", &wxHyperlinkCtrl::SetLabel, _T("https://filezilla-project.org/"));
	xrc_call(*this, "ID_COPYRIGHT", &wxStaticText::SetLabel, _T("Copyright (C) 2004-2017  Tim Kosse"));

	wxString version = CBuildInfo::GetVersion();
	if (CBuildInfo::GetBuildType() == _T("nightly")) {
		version += _T("-nightly");
	}
	if (!SetChildLabel(XRCID("ID_VERSION"), version)) {
		return false;
	}

	wxString const host = CBuildInfo::GetHostname();
	if (host.empty()) {
		xrc_call(*this, "ID_HOST", &wxStaticText::Hide);
		xrc_call(*this, "ID_HOST_DESC", &wxStaticText::Hide);
	}
	else {
		xrc_call(*this, "ID_HOST", &wxStaticText::SetLabel, host);
	}

	wxString const build = CBuildInfo::GetBuildSystem();
	if (build.empty()) {
		xrc_call(*this, "ID_BUILD", &wxStaticText::Hide);
		xrc_call(*this, "ID_BUILD_DESC", &wxStaticText::Hide);
	}
	else {
		xrc_call(*this, "ID_BUILD", &wxStaticText::SetLabel, build);
	}

	if (!SetChildLabel(XRCID("ID_BUILDDATE"), CBuildInfo::GetBuildDateString())) {
		return false;
	}

	if (!SetChildLabel(XRCID("ID_COMPILEDWITH"), CBuildInfo::GetCompiler(), 200)) {
		return false;
	}

	wxString compilerFlags = CBuildInfo::GetCompilerFlags();
	if (compilerFlags.empty()) {
		xrc_call(*this, "ID_CFLAGS", &wxStaticText::Hide);
		xrc_call(*this, "ID_CFLAGS_DESC", &wxStaticText::Hide);
	}
	else {
		WrapText(this, compilerFlags, 300);
		xrc_call(*this, "ID_CFLAGS", &wxStaticText::SetLabel, compilerFlags);
	}

	xrc_call(*this, "ID_VER_WX", &wxStaticText::SetLabel, GetDependencyVersion(gui_lib_dependency::wxwidgets));
	xrc_call(*this, "ID_VER_GNUTLS", &wxStaticText::SetLabel, GetDependencyVersion(lib_dependency::gnutls));
	xrc_call(*this, "ID_VER_SQLITE", &wxStaticText::SetLabel, GetDependencyVersion(gui_lib_dependency::sqlite));

	wxString const os = wxGetOsDescription();
	if (os.empty()) {
		xrc_call(*this, "ID_SYSTEM_NAME", &wxStaticText::Hide);
		xrc_call(*this, "ID_SYSTEM_NAME_DESC", &wxStaticText::Hide);
	}
	else {
		xrc_call(*this, "ID_SYSTEM_NAME", &wxStaticText::SetLabel, os);
	}

	int major, minor;
	if (GetRealOsVersion(major, minor)) {
		wxString osVersion = wxString::Format(_T("%d.%d"), major, minor);
		int fakeMajor, fakeMinor;
		if (wxGetOsVersion(&fakeMajor, &fakeMinor) != wxOS_UNKNOWN && (fakeMajor != major || fakeMinor != minor)) {
			osVersion += _T(" ");
			osVersion += wxString::Format(_("(app-compat is set to %d.%d)"), fakeMajor, fakeMinor);
		}
		xrc_call(*this, "ID_SYSTEM_VER", &wxStaticText::SetLabel, osVersion);
	}
	else {
		xrc_call(*this, "ID_SYSTEM_VER", &wxStaticText::Hide);
		xrc_call(*this, "ID_SYSTEM_VER_DESC", &wxStaticText::Hide);
	}

#ifdef __WXMSW__
	if (::wxIsPlatform64Bit()) {
		xrc_call(*this, "ID_SYSTEM_PLATFORM", &wxStaticText::SetLabel, _("64-bit system"));
	}
	else {
		xrc_call(*this, "ID_SYSTEM_PLATFORM", &wxStaticText::SetLabel, _("32-bit system"));
	}
#else
	xrc_call(*this, "ID_SYSTEM_PLATFORM", &wxStaticText::Hide);
	xrc_call(*this, "ID_SYSTEM_PLATFORM_DESC", &wxStaticText::Hide);
#endif

	wxString cpuCaps = CBuildInfo::GetCPUCaps(' ');
	if (!cpuCaps.empty()) {
		WrapText(this, cpuCaps, 300);
		xrc_call(*this, "ID_SYSTEM_CPU", &wxStaticText::SetLabel, cpuCaps);
	}
	else {
		xrc_call(*this, "ID_SYSTEM_CPU_DESC", &wxStaticText::Hide);
		xrc_call(*this, "ID_SYSTEM_CPU", &wxStaticText::Hide);
	}

	xrc_call(*this, "ID_SYSTEM_SETTINGS_DIR", &wxStaticText::SetLabel, COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR));

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	return true;
}

void CAboutDialog::OnOK(wxCommandEvent&)
{
	EndModal(wxID_OK);
}

void CAboutDialog::OnCopy(wxCommandEvent&)
{
	wxString text = _T("FileZilla Client\n");
	text += _T("----------------\n\n");

	text += _T("Version:          ") + CBuildInfo::GetVersion();
	if (CBuildInfo::GetBuildType() == _T("nightly")) {
		text += _T("-nightly");
	}
	text += '\n';

	text += _T("\nBuild information:\n");

	wxString host = CBuildInfo::GetHostname();
	if (!host.empty()) {
		text += _T("  Compiled for:   ") + host + _T("\n");
	}

	wxString build = CBuildInfo::GetBuildSystem();
	if (!build.empty()) {
		text += _T("  Compiled on:    ") + build + _T("\n");
	}

	text += _T("  Build date:     ") + CBuildInfo::GetBuildDateString() + _T("\n");

	text += _T("  Compiled with:  ") + CBuildInfo::GetCompiler() + _T("\n");

	wxString compilerFlags = CBuildInfo::GetCompilerFlags();
	if (!compilerFlags.empty()) {
		text += _T("  Compiler flags: ") + compilerFlags + _T("\n");
	}

	text += _T("\nLinked against:\n");
	for (int i = 0; i < static_cast<int>(gui_lib_dependency::count); ++i) {
		text += wxString::Format(_T("  % -15s %s\n"),
			GetDependencyName(gui_lib_dependency(i)) + _T(":"),
			GetDependencyVersion(gui_lib_dependency(i)));
	}
	for (int i = 0; i < static_cast<int>(lib_dependency::count); ++i) {
		text += wxString::Format(_T("  % -15s %s\n"),
			GetDependencyName(lib_dependency(i)) + _T(":"),
			GetDependencyVersion(lib_dependency(i)));
	}

	text += _T("\nOperating system:\n");
	wxString os = wxGetOsDescription();
	if (!os.empty()) {
		text += _T("  Name:           ") + os + _T("\n");
	}

	int major, minor;
	if (GetRealOsVersion(major, minor)) {
		wxString version = wxString::Format(_T("%d.%d"), major, minor);
		int fakeMajor, fakeMinor;
		if (wxGetOsVersion(&fakeMajor, &fakeMinor) != wxOS_UNKNOWN && (fakeMajor != major || fakeMinor != minor)) {
			version += _T(" ");
			version += wxString::Format(_("(app-compat is set to %d.%d)"), fakeMajor, fakeMinor);
		}
		text += wxString::Format(_T("  Version:        %s\n"), version);
	}

#ifdef __WXMSW__
	if (::wxIsPlatform64Bit()) {
		text += _T("  Platform:       64-bit system\n");
	}
	else {
		text += _T("  Platform:       32-bit system\n");
	}
#endif

	wxString cpuCaps = CBuildInfo::GetCPUCaps(' ');
	if (!cpuCaps.empty()) {
		text += _T("  CPU features:   ") + cpuCaps + _T("\n");
	}

	text += _T("  Settings dir:   ") + COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR) + _T("\n");

#ifdef __WXMSW__
	text.Replace(_T("\n"), _T("\r\n"));
#endif

	if (!wxTheClipboard->Open()) {
		wxMessageBoxEx(_("Could not open clipboard"), _("Could not copy data"), wxICON_EXCLAMATION);
		return;
	}

	wxTheClipboard->SetData(new wxTextDataObject(text));
	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}
