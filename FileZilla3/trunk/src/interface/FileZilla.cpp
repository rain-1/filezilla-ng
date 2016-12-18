#include <filezilla.h>
#ifdef _MSC_VER
#pragma hdrstop
#endif
#include "filezillaapp.h"
#include "Mainfrm.h"
#include "Options.h"
#include "wrapengine.h"
#include "buildinfo.h"
#include "cmdline.h"
#include "welcome_dialog.h"
#include "msgbox.h"

#include <libfilezilla/local_filesys.hpp>

#include "wxfilesystem_blob_handler.h"
#include "xrc_helper.h"
#ifdef __WXMSW__
#include <wx/dynlib.h>
#endif
#ifdef WITH_LIBDBUS
#include <../dbus/session_manager.h>
#endif

#if defined(__WXMAC__) || defined(__UNIX__)
#include <wx/stdpaths.h>
#endif

#include "locale_initializer.h"

#ifdef USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif

#ifdef ENABLE_BINRELOC
	#define BR_PTHREADS 0
	#include "prefix.h"
#endif

#ifndef __WXGTK__
IMPLEMENT_APP(CFileZillaApp)
#else
IMPLEMENT_APP_NO_MAIN(CFileZillaApp)
#endif //__WXGTK__

#if !wxUSE_PRINTF_POS_PARAMS
  #error Please build wxWidgets with support for positional arguments.
#endif

CFileZillaApp::CFileZillaApp()
{
	m_profile_start = fz::monotonic_clock::now();
	AddStartupProfileRecord("CFileZillaApp::CFileZillaApp()");
}

CFileZillaApp::~CFileZillaApp()
{
	COptions::Destroy();
}

#ifdef __WXMSW__
namespace {
extern "C"
{
	typedef HRESULT(WINAPI *t_SetCurrentProcessExplicitAppUserModelID)(PCWSTR AppID);
}

static void SetAppId()
{
	wxDynamicLibrary dll;
	if (!dll.Load(_T("shell32.dll")))
		return;

	if (!dll.HasSymbol(_T("SetCurrentProcessExplicitAppUserModelID")))
		return;

	t_SetCurrentProcessExplicitAppUserModelID pSetCurrentProcessExplicitAppUserModelID =
		(t_SetCurrentProcessExplicitAppUserModelID)dll.GetSymbol(_T("SetCurrentProcessExplicitAppUserModelID"));

	if (!pSetCurrentProcessExplicitAppUserModelID)
		return;

	pSetCurrentProcessExplicitAppUserModelID(_T("FileZilla.Client.AppID"));
}

static bool InitWinsock()
{
	WSADATA d{};
	int res = WSAStartup((2 << 8) | 8, &d);
	if (res != 0) {
		int err = WSAGetLastError();
		wxString msg = wxString::Format(_("Could not initialize Winsock (%d): %s"), err, wxSysErrorMsg(err));
		wxMessageBoxEx(msg, _("Failed to initialize networking"), wxICON_EXCLAMATION);
		return false;
	}

	return true;
}

static void UninitWinsock()
{
	WSACleanup();
}
}

#endif //__WXMSW__

void CFileZillaApp::InitLocale()
{
	wxString language = COptions::Get()->GetOption(OPTION_LANGUAGE);
	const wxLanguageInfo* pInfo = wxLocale::FindLanguageInfo(language);
	if (!language.empty()) {
#ifdef __WXGTK__
		if (CInitializer::error) {
			wxString error;

			wxLocale *loc = wxGetLocale();
			const wxLanguageInfo* currentInfo = loc ? loc->GetLanguageInfo(loc->GetLanguage()) : 0;
			if (!loc || !currentInfo) {
				if (!pInfo)
					error.Printf(_("Failed to set language to %s, using default system language."),
						language);
				else
					error.Printf(_("Failed to set language to %s (%s), using default system language."),
						pInfo->Description, language);
			}
			else {
				wxString currentName = currentInfo->CanonicalName;

				if (!pInfo)
					error.Printf(_("Failed to set language to %s, using default system language (%s, %s)."),
						language, loc->GetLocale(),
						currentName);
				else
					error.Printf(_("Failed to set language to %s (%s), using default system language (%s, %s)."),
						pInfo->Description, language, loc->GetLocale(),
						currentName);
			}

			error += _T("\n");
			error += _("Please make sure the requested locale is installed on your system.");
			wxMessageBoxEx(error, _("Failed to change language"), wxICON_EXCLAMATION);

			COptions::Get()->SetOption(OPTION_LANGUAGE, _T(""));
		}
#else
		if (!pInfo || !SetLocale(pInfo->Language)) {
			for( language = GetFallbackLocale(language); !language.empty(); language = GetFallbackLocale(language) ) {
				const wxLanguageInfo* fallbackInfo = wxLocale::FindLanguageInfo(language);
				if( fallbackInfo && SetLocale(fallbackInfo->Language )) {
					COptions::Get()->SetOption(OPTION_LANGUAGE, language.ToStdWstring());
					return;
				}
			}
			COptions::Get()->SetOption(OPTION_LANGUAGE, std::wstring());
			if (pInfo && !pInfo->Description.empty())
				wxMessageBoxEx(wxString::Format(_("Failed to set language to %s (%s), using default system language"), pInfo->Description, language), _("Failed to change language"), wxICON_EXCLAMATION);
			else
				wxMessageBoxEx(wxString::Format(_("Failed to set language to %s, using default system language"), language), _("Failed to change language"), wxICON_EXCLAMATION);
		}
#endif
	}
}

namespace {
std::wstring translator(char const* const t)
{
	return wxGetTranslation(t).ToStdWstring();
}

std::wstring translator_pf(char const* const singular, char const* const plural, int64_t n)
{
	// wxGetTranslation does not support 64bit ints on 32bit systems.
	if (n < 0) {
		n = -n;
	}
	return wxGetTranslation(singular, plural, (sizeof(unsigned int) < 8 && n > 1000000000) ? (1000000000 + n % 1000000000) : n).ToStdWstring();
}
}

bool CFileZillaApp::OnInit()
{
	AddStartupProfileRecord("CFileZillaApp::OnInit()");

	fz::set_translators(translator, translator_pf);

#ifdef __WXMSW__
	if (!InitWinsock()) {
		return false;
	}

	SetAppId();
#endif

	//wxSystemOptions is slow, if a value is not set, it keeps querying the environment
	//each and every time...
	wxSystemOptions::SetOption(_T("filesys.no-mimetypesmanager"), 0);
	wxSystemOptions::SetOption(_T("window-default-variant"), _T(""));
#ifdef __WXMSW__
	wxSystemOptions::SetOption(_T("no-maskblt"), 0);
	wxSystemOptions::SetOption(_T("msw.window.no-clip-children"), 0);
	wxSystemOptions::SetOption(_T("msw.font.no-proof-quality"), 0);
	wxSystemOptions::SetOption(_T("msw.remap"), 0);
	wxSystemOptions::SetOption(_T("msw.staticbox.optimized-paint"), 0);
#endif
#ifdef __WXMAC__
	wxSystemOptions::SetOption(_T("mac.listctrl.always_use_generic"), 1);
	wxSystemOptions::SetOption(_T("mac.textcontrol-use-spell-checker"), 0);
#endif

	int cmdline_result = ProcessCommandLine();
	if (!cmdline_result) {
		return false;
	}

	LoadLocales();

	if (cmdline_result < 0) {
		if (m_pCommandLine) {
			m_pCommandLine->DisplayUsage();
		}
		return false;
	}

	InitDefaultsDir();

	COptions::Init();

#ifdef USE_MAC_SANDBOX
	OSXSandboxUserdirs::Get().Load();
#endif

	InitLocale();

#ifndef _DEBUG
	const wxString& buildType = CBuildInfo::GetBuildType();
	if (buildType == _T("nightly")) {
		wxMessageBoxEx(_T("You are using a nightly development version of FileZilla 3, do not expect anything to work.\r\nPlease use the official releases instead.\r\n\r\n\
Unless explicitly instructed otherwise,\r\n\
DO NOT post bugreports,\r\n\
DO NOT use it in production environments,\r\n\
DO NOT distribute the binaries,\r\n\
DO NOT complain about it\r\n\
USE AT OWN RISK"), _T("Important Information"));
	}
	else {
		wxString v;
		if (!wxGetEnv(_T("FZDEBUG"), &v) || v != _T("1")) {
			COptions::Get()->SetOption(OPTION_LOGGING_DEBUGLEVEL, 0);
			COptions::Get()->SetOption(OPTION_LOGGING_RAWLISTING, 0);
		}
	}
#endif

	if (!LoadResourceFiles()) {
		COptions::Destroy();
		return false;
	}

	CheckExistsFzsftp();

	// Turn off idle events, we don't need them
	wxIdleEvent::SetMode(wxIDLE_PROCESS_SPECIFIED);

	wxUpdateUIEvent::SetMode(wxUPDATE_UI_PROCESS_SPECIFIED);

#ifdef WITH_LIBDBUS
	CSessionManager::Init();
#endif

	// Load the text wrapping engine
	m_pWrapEngine = std::make_unique<CWrapEngine>();
	m_pWrapEngine->LoadCache();

	CMainFrame *frame = new CMainFrame();
	frame->Show(true);
	SetTopWindow(frame);

	CWelcomeDialog *welcome_dialog = new CWelcomeDialog;
	welcome_dialog->Run(frame, false, true);

	frame->ProcessCommandLine();
	frame->PostInitialize();

	ShowStartupProfile();

	return true;
}

int CFileZillaApp::OnExit()
{
	COptions::Get()->SaveIfNeeded();

#ifdef WITH_LIBDBUS
	CSessionManager::Uninit();
#endif
#ifdef __WXMSW__
	UninitWinsock();
#endif
	return wxApp::OnExit();
}

bool CFileZillaApp::FileExists(std::wstring const& file) const
{
	return fz::local_filesys::get_file_type(fz::to_native(file), true) == fz::local_filesys::file;
}

CLocalPath CFileZillaApp::GetDataDir(std::wstring fileToFind) const
{
	/*
	 * Finding the resources in all cases is a difficult task,
	 * due to the huge variety of diffent systems and their filesystem
	 * structure.
	 * Basically we just check a couple of paths for presence of the resources,
	 * and hope we find them. If not, the user can still specify on the cmdline
	 * and using environment variables where the resources are.
	 *
	 * At least on OS X it's simple: All inside application bundle.
	 */

#ifdef __WXMAC__
	CLocalPath path(wxStandardPaths::Get().GetDataDir().ToStdWstring());
	if (FileExists(path.GetPath() + fileToFind)) {
		return path;
	}

	return CLocalPath();
#else

	wxPathList pathList;
	// FIXME: --datadir cmdline

	// First try the user specified data dir.
	pathList.AddEnvList(_T("FZ_DATADIR"));

	// Next try the current path and the current executable path.
	// Without this, running development versions would be difficult.
	pathList.Add(wxGetCwd());

#ifdef ENABLE_BINRELOC
	const char* path = SELFPATH;
	if (path && *path) {
		wxString datadir(SELFPATH , *wxConvCurrent);
		wxFileName fn(datadir);
		datadir = fn.GetPath();
		if (!datadir.empty())
			pathList.Add(datadir);

	}
	path = DATADIR;
	if (path && *path) {
		wxString datadir(DATADIR, *wxConvCurrent);
		if (!datadir.empty())
			pathList.Add(datadir);
	}
#elif defined __WXMSW__
	wxChar path[1024];
	int res = GetModuleFileName(0, path, 1000);
	if (res > 0 && res < 1000) {
		wxFileName fn(path);
		pathList.Add(fn.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR));
	}
#endif //ENABLE_BINRELOC and __WXMSW__ blocks

	// Now scan through the path
	pathList.AddEnvList(_T("PATH"));

#ifndef __WXMSW__
	// Try some common paths
	pathList.Add(_T("/usr/share/filezilla"));
	pathList.Add(_T("/usr/local/share/filezilla"));
#endif

	// For each path, check for the resources
	wxPathList::const_iterator node;
	for (node = pathList.begin(); node != pathList.end(); ++node) {
		auto const cur = CLocalPath(node->ToStdWstring()).GetPath();
		if (FileExists(cur + fileToFind)) {
			return CLocalPath(cur);
		}
		if (FileExists(cur + _T("share/filezilla/") + fileToFind)) {
			return CLocalPath(cur + _T("/share/filezilla"));
		}
		if (FileExists(cur + _T("filezilla/") + fileToFind)) {
			return CLocalPath(cur + _T("filezilla"));
		}
	}

	for (node = pathList.begin(); node != pathList.end(); ++node) {
		auto const cur = CLocalPath(node->ToStdWstring()).GetPath();
		if (FileExists(cur + _T("../") + fileToFind)) {
			return CLocalPath(cur + _T("/.."));
		}
		if (FileExists(cur + _T("../share/filezilla/") + fileToFind)) {
			return CLocalPath(cur + _T("../share/filezilla"));
		}
	}

	for (node = pathList.begin(); node != pathList.end(); ++node) {
		auto const cur = CLocalPath(node->ToStdWstring()).GetPath();
		if (FileExists(cur + _T("../../") + fileToFind)) {
			return CLocalPath(cur + _T("../.."));
		}
	}

	return CLocalPath();
#endif //__WXMAC__
}

bool CFileZillaApp::LoadResourceFiles()
{
	AddStartupProfileRecord("CFileZillaApp::LoadResourceFiles");
	m_resourceDir = GetDataDir(_T("resources/defaultfilters.xml"));

	wxImage::AddHandler(new wxPNGHandler());

	if (m_resourceDir.empty()) {
		wxString msg = _("Could not find the resource files for FileZilla, closing FileZilla.\nYou can set the data directory of FileZilla using the '--datadir <custompath>' commandline option or by setting the FZ_DATADIR environment variable.");
		wxMessageBoxEx(msg, _("FileZilla Error"), wxOK | wxICON_ERROR);
		return false;
	}

	m_resourceDir.AddSegment(_T("resources"));

	wxXmlResource *pResource = wxXmlResource::Get();

#ifndef __WXDEBUG__
	pResource->SetFlags(pResource->GetFlags() | wxXRC_NO_RELOADING);
#endif

	InitHandlers(*pResource);

	fz::local_filesys fs;
	std::wstring dir = m_resourceDir.GetPath() + L"xrc/";
	pResource->LoadFile(wxString(dir + L"certificate.xrc"));
	pResource->LoadFile(wxString(dir + L"dialogs.xrc"));
	pResource->LoadFile(wxString(dir + L"inputdialog.xrc"));
	pResource->LoadFile(wxString(dir + L"menus.xrc"));
	pResource->LoadFile(wxString(dir + L"netconfwizard.xrc"));
	pResource->LoadFile(wxString(dir + L"settings.xrc"));
	pResource->LoadFile(wxString(dir + L"sitemanager.xrc"));
	pResource->LoadFile(wxString(dir + L"update.xrc"));

	// Useful for XRC files with embedded image data.
	wxFileSystem::AddHandler(new wxFileSystemBlobHandler);

	return true;
}

bool CFileZillaApp::InitDefaultsDir()
{
	AddStartupProfileRecord("InitDefaultsDir");
#ifdef __WXGTK__
	m_defaultsDir = COptions::GetUnadjustedSettingsDir();
	if( m_defaultsDir.empty() || !wxFileName::FileExists(m_defaultsDir.GetPath() + _T("fzdefaults.xml"))) {
		if (wxFileName::FileExists(_T("/etc/filezilla/fzdefaults.xml"))) {
			m_defaultsDir.SetPath(_T("/etc/filezilla"));
		}
		else {
			m_defaultsDir.clear();
		}
	}

#endif
	if( m_defaultsDir.empty() ) {
		m_defaultsDir = GetDataDir(_T("fzdefaults.xml"));
	}

	return !m_defaultsDir.empty();
}

bool CFileZillaApp::LoadLocales()
{
	AddStartupProfileRecord("CFileZillaApp::LoadLocales");
#ifndef __WXMAC__
	m_localesDir = GetDataDir(_T("../locale/de/filezilla.mo"));
	if (m_localesDir.empty())
		m_localesDir = GetDataDir(_T("../locale/de/LC_MESSAGES/filezilla.mo"));
	if (!m_localesDir.empty()) {
		m_localesDir.ChangePath( _T("../locale") );
	}
	else {
		m_localesDir = GetDataDir(_T("locales/de/filezilla.mo"));
		if (!m_localesDir.empty()) {
			m_localesDir.AddSegment(_T("locales"));
		}
	}
#else
	m_localesDir.SetPath(wxStandardPaths::Get().GetDataDir().ToStdWstring() + _T("/locales"));
#endif

	if (!m_localesDir.empty()) {
		wxLocale::AddCatalogLookupPathPrefix(m_localesDir.GetPath());
	}

	SetLocale(wxLANGUAGE_DEFAULT);

	return true;
}

bool CFileZillaApp::SetLocale(int language)
{
	// First check if we can load the new locale
	auto pLocale = std::make_unique<wxLocale>();
	wxLogNull log;
	pLocale->Init(language);
	if (!pLocale->IsOk()) {
		return false;
	}

	// Load catalog. Only fail if a non-default language has been selected
	if (!pLocale->AddCatalog(_T("filezilla")) && language != wxLANGUAGE_DEFAULT) {
		return false;
	}

	if (m_pLocale) {
		// Now unload old locale
		// We unload new locale as well, else the internal locale chain in wxWidgets get's broken.
		pLocale.reset();
		m_pLocale.reset();

		// Finally load new one
		pLocale = std::make_unique<wxLocale>();
		pLocale->Init(language);
		if (!pLocale->IsOk()) {
			return false;
		}
		else if (!pLocale->AddCatalog(_T("filezilla")) && language != wxLANGUAGE_DEFAULT) {
			return false;
		}
	}
	m_pLocale = std::move(pLocale);

	return true;
}

int CFileZillaApp::GetCurrentLanguage() const
{
	if (!m_pLocale)
		return wxLANGUAGE_ENGLISH;

	return m_pLocale->GetLanguage();
}

wxString CFileZillaApp::GetCurrentLanguageCode() const
{
	if (!m_pLocale)
		return wxString();

	return m_pLocale->GetCanonicalName();
}

#if wxUSE_DEBUGREPORT && wxUSE_ON_FATAL_EXCEPTION
void CFileZillaApp::OnFatalException()
{
}
#endif

void CFileZillaApp::DisplayEncodingWarning()
{
	static bool displayedEncodingWarning = false;
	if (displayedEncodingWarning)
		return;

	displayedEncodingWarning = true;

	wxMessageBoxEx(_("A local filename could not be decoded.\nPlease make sure the LC_CTYPE (or LC_ALL) environment variable is set correctly.\nUnless you fix this problem, files might be missing in the file listings.\nNo further warning will be displayed this session."), _("Character encoding issue"), wxICON_EXCLAMATION);
}

CWrapEngine* CFileZillaApp::GetWrapEngine()
{
	return m_pWrapEngine.get();
}

void CFileZillaApp::CheckExistsFzsftp()
{
	AddStartupProfileRecord("FileZillaApp::CheckExistsFzstp");
	// Get the correct path to the fzsftp executable

#ifdef __WXMAC__
	wxString executable = wxStandardPaths::Get().GetExecutablePath();
	int pos = executable.Find('/', true);
	if (pos != -1)
		executable = executable.Left(pos);
	executable += _T("/fzsftp");
	if (!wxFileName::FileExists(executable.ToStdWstring()))
	{
		wxMessageBoxEx(wxString::Format(_("%s could not be found. Without this component of FileZilla, SFTP will not work.\n\nPlease download FileZilla again. If this problem persists, please submit a bug report."), executable),
			_("File not found"), wxICON_ERROR);
		executable.clear();
	}

#else

	wxString program = _T("fzsftp");
#ifdef __WXMSW__
	program += _T(".exe");
#endif

	bool found = false;

	// First check the FZ_FZSFTP environment variable
	wxString executable;
	if (wxGetEnv(_T("FZ_FZSFTP"), &executable)) {
		if (wxFileName::FileExists(executable.ToStdWstring()))
			found = true;
	}

	if (!found) {
		wxPathList pathList;

		// Add current working directory
		const wxString &cwd = wxGetCwd();
		pathList.Add(cwd);
#ifdef __WXMSW__

		// Add executable path
		wxChar modulePath[1000];
		DWORD len = GetModuleFileName(0, modulePath, 999);
		if (len) {
			modulePath[len] = 0;
			wxString path(modulePath);
			int pos = path.Find('\\', true);
			if (pos != -1) {
				path = path.Left(pos);
				pathList.Add(path);
			}
		}
#endif

		// Add a few paths relative to the current working directory
		pathList.Add(cwd + _T("/bin"));
		pathList.Add(cwd + _T("/src/putty"));
		pathList.Add(cwd + _T("/putty"));

		executable = pathList.FindAbsoluteValidPath(program);
		if (!executable.empty())
			found = true;
	}

#ifdef __UNIX__
	if (!found) {
		const wxString prefix = ((const wxStandardPaths&)wxStandardPaths::Get()).GetInstallPrefix();
		if (prefix != _T("/usr/local")) {
			// /usr/local is the fallback value. /usr/local/bin is most likely in the PATH
			// environment variable already so we don't have to check it. Furthermore, other
			// directories might be listed before it (For example a developer's own
			// application prefix)
			wxFileName fn(prefix + _T("/bin/"), program);
			fn.Normalize();
			if (fn.FileExists()) {
				executable = fn.GetFullPath();
				found = true;
			}
		}
	}
#endif

	if (!found) {
		// Check PATH
		wxPathList pathList;
		pathList.AddEnvList(_T("PATH"));
		executable = pathList.FindAbsoluteValidPath(program);
		if (!executable.empty())
			found = true;
	}

	if (!found) {
		// Quote path if it contains spaces
		if (executable.Find(_T(" ")) != -1 && executable[0] != '"' && executable[0] != '\'')
			executable = _T("\"") + executable + _T("\"");

		wxMessageBoxEx(wxString::Format(_("%s could not be found. Without this component of FileZilla, SFTP will not work.\n\nPossible solutions:\n- Make sure %s is in a directory listed in your PATH environment variable.\n- Set the full path to %s in the FZ_FZSFTP environment variable."), program, program, program),
			_("File not found"), wxICON_ERROR | wxOK);
		executable.clear();
	}
#endif

	COptions::Get()->SetOption(OPTION_FZSFTP_EXECUTABLE, executable.ToStdWstring());
}

#ifdef __WXMSW__
extern "C" BOOL CALLBACK EnumWindowCallback(HWND hwnd, LPARAM)
{
	HWND child = FindWindowEx(hwnd, 0, 0, _T("FileZilla process identificator 3919DB0A-082D-4560-8E2F-381A35969FB4"));
	if (child) {
		::PostMessage(hwnd, WM_ENDSESSION, (WPARAM)TRUE, (LPARAM)ENDSESSION_LOGOFF);
	}

	return TRUE;
}
#endif

int CFileZillaApp::ProcessCommandLine()
{
	AddStartupProfileRecord("CFileZillaApp::ProcessCommandLine");
	m_pCommandLine = std::make_unique<CCommandLine>(argc, argv);
	int res = m_pCommandLine->Parse() ? 1 : -1;

	if (res > 0) {
		if (m_pCommandLine->HasSwitch(CCommandLine::close)) {
#ifdef __WXMSW__
			EnumWindows((WNDENUMPROC)EnumWindowCallback, 0);
#endif
			return 0;
		}

		if (m_pCommandLine->HasSwitch(CCommandLine::version)) {
			wxString out = wxString::Format(_T("FileZilla %s"), CBuildInfo::GetVersion());
			if (!CBuildInfo::GetBuildType().empty())
				out += _T(" ") + CBuildInfo::GetBuildType() + _T(" build");
			out += _T(", compiled on ") + CBuildInfo::GetBuildDateString();

			printf("%s\n", (const char*)out.mb_str());
			return 0;
		}
	}

	return res;
}

void CFileZillaApp::AddStartupProfileRecord(std::string const& msg)
{
	if (!m_profile_start) {
		return;
	}

	m_startupProfile.emplace_back(fz::monotonic_clock::now(), msg);
}

void CFileZillaApp::ShowStartupProfile()
{
	if (m_profile_start && m_pCommandLine && m_pCommandLine->HasSwitch(CCommandLine::debug_startup)) {
		wxString msg = _T("Profile:\n");
		for (auto const& p : m_startupProfile) {
			auto const diff = p.first - m_profile_start;

			msg += std::to_wstring(diff.get_milliseconds());
			msg += _T(" ");
			msg += fz::to_wstring(p.second);
			msg += _T("\n");
		}
		wxMessageBoxEx(msg);
	}

	m_profile_start = fz::monotonic_clock();
	m_startupProfile.clear();
}

std::wstring CFileZillaApp::GetSettingsFile(std::wstring const& name) const
{
	return COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR) + name + _T(".xml");
}
