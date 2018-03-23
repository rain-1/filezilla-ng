#include <filezilla.h>
#include "manual_transfer.h"
#include "auto_ascii_files.h"
#include "state.h"
#include "Options.h"
#include "sitemanager.h"
#include "queue.h"
#include "QueueView.h"
#include "xrc_helper.h"

#include <libfilezilla/local_filesys.hpp>

BEGIN_EVENT_TABLE(CManualTransfer, wxDialogEx)
EVT_TEXT(XRCID("ID_LOCALFILE"), CManualTransfer::OnLocalChanged)
EVT_TEXT(XRCID("ID_REMOTEFILE"), CManualTransfer::OnRemoteChanged)
EVT_BUTTON(XRCID("ID_BROWSE"), CManualTransfer::OnLocalBrowse)
EVT_RADIOBUTTON(XRCID("ID_DOWNLOAD"), CManualTransfer::OnDirection)
EVT_RADIOBUTTON(XRCID("ID_UPLOAD"), CManualTransfer::OnDirection)
EVT_RADIOBUTTON(XRCID("ID_SERVER_CURRENT"), CManualTransfer::OnServerTypeChanged)
EVT_RADIOBUTTON(XRCID("ID_SERVER_SITE"), CManualTransfer::OnServerTypeChanged)
EVT_RADIOBUTTON(XRCID("ID_SERVER_CUSTOM"), CManualTransfer::OnServerTypeChanged)
EVT_BUTTON(XRCID("wxID_OK"), CManualTransfer::OnOK)
EVT_BUTTON(XRCID("ID_SERVER_SITE_SELECT"), CManualTransfer::OnSelectSite)
EVT_MENU(wxID_ANY, CManualTransfer::OnSelectedSite)
EVT_CHOICE(XRCID("ID_LOGONTYPE"), CManualTransfer::OnLogontypeSelChanged)
END_EVENT_TABLE()

CManualTransfer::CManualTransfer(CQueueView* pQueueView)
	: m_local_file_exists()
	, m_pState()
	, m_pQueueView(pQueueView)
{
}

void CManualTransfer::Run(wxWindow* pParent, CState* pState)
{
	if (!Load(pParent, _T("ID_MANUALTRANSFER"))) {
		return;
	}

	m_pState = pState;

	wxChoice *pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	if (pProtocol) {
		pProtocol->Append(CServer::GetProtocolName(FTP));
		pProtocol->Append(CServer::GetProtocolName(SFTP));
		pProtocol->Append(CServer::GetProtocolName(FTPS));
		pProtocol->Append(CServer::GetProtocolName(FTPES));
		pProtocol->Append(CServer::GetProtocolName(INSECURE_FTP));
	}

	wxChoice* pChoice = XRCCTRL(*this, "ID_LOGONTYPE", wxChoice);
	wxASSERT(pChoice);
	for (int i = 0; i < static_cast<int>(LogonType::count); ++i) {
		pChoice->Append(GetNameFromLogonType(static_cast<LogonType>(i)));
	}

	server_ = m_pState->GetServer();
	if (server_) {
		XRCCTRL(*this, "ID_SERVER_CURRENT", wxRadioButton)->SetValue(true);
		DisplayServer();
	}
	else {
		XRCCTRL(*this, "ID_SERVER_CUSTOM", wxRadioButton)->SetValue(true);
		XRCCTRL(*this, "ID_SERVER_CURRENT", wxRadioButton)->Disable();
		DisplayServer();
	}

	wxString localPath = m_pState->GetLocalDir().GetPath();
	XRCCTRL(*this, "ID_LOCALFILE", wxTextCtrl)->ChangeValue(localPath);

	XRCCTRL(*this, "ID_REMOTEPATH", wxTextCtrl)->ChangeValue(m_pState->GetRemotePath().GetPath());

	SetControlState();

	switch(COptions::Get()->GetOptionVal(OPTION_ASCIIBINARY))
	{
	case 1:
		XRCCTRL(*this, "ID_TYPE_ASCII", wxRadioButton)->SetValue(true);
		break;
	case 2:
		XRCCTRL(*this, "ID_TYPE_BINARY", wxRadioButton)->SetValue(true);
		break;
	default:
		XRCCTRL(*this, "ID_TYPE_AUTO", wxRadioButton)->SetValue(true);
		break;
	}

	wxSize minSize = GetSizer()->GetMinSize();
	SetClientSize(minSize);

	ShowModal();
}

void CManualTransfer::SetControlState()
{
	SetServerState();
	SetAutoAsciiState();

	XRCCTRL(*this, "ID_SERVER_SITE_SELECT", wxButton)->Enable(XRCCTRL(*this, "ID_SERVER_SITE", wxRadioButton)->GetValue());
}

void CManualTransfer::SetAutoAsciiState()
{
	if (XRCCTRL(*this, "ID_DOWNLOAD", wxRadioButton)->GetValue()) {
		wxString remote_file = XRCCTRL(*this, "ID_REMOTEFILE", wxTextCtrl)->GetValue();
		if (remote_file.empty()) {
			XRCCTRL(*this, "ID_TYPE_AUTO_ASCII", wxStaticText)->Hide();
			XRCCTRL(*this, "ID_TYPE_AUTO_BINARY", wxStaticText)->Hide();
		}
		else if (CAutoAsciiFiles::TransferLocalAsAscii(remote_file, server_.server.GetType())) {
			XRCCTRL(*this, "ID_TYPE_AUTO_ASCII", wxStaticText)->Show();
			XRCCTRL(*this, "ID_TYPE_AUTO_BINARY", wxStaticText)->Hide();
		}
		else {
			XRCCTRL(*this, "ID_TYPE_AUTO_ASCII", wxStaticText)->Hide();
			XRCCTRL(*this, "ID_TYPE_AUTO_BINARY", wxStaticText)->Show();
		}
	}
	else {
		wxString local_file = XRCCTRL(*this, "ID_LOCALFILE", wxTextCtrl)->GetValue();
		if (!m_local_file_exists) {
			XRCCTRL(*this, "ID_TYPE_AUTO_ASCII", wxStaticText)->Hide();
			XRCCTRL(*this, "ID_TYPE_AUTO_BINARY", wxStaticText)->Hide();
		}
		else if (CAutoAsciiFiles::TransferLocalAsAscii(local_file, server_.server.GetType())) {
			XRCCTRL(*this, "ID_TYPE_AUTO_ASCII", wxStaticText)->Show();
			XRCCTRL(*this, "ID_TYPE_AUTO_BINARY", wxStaticText)->Hide();
		}
		else {
			XRCCTRL(*this, "ID_TYPE_AUTO_ASCII", wxStaticText)->Hide();
			XRCCTRL(*this, "ID_TYPE_AUTO_BINARY", wxStaticText)->Show();
		}
	}
	XRCCTRL(*this, "ID_TYPE_AUTO_ASCII", wxStaticText)->GetContainingSizer()->Layout();
}

void CManualTransfer::SetServerState()
{
	bool server_enabled = XRCCTRL(*this, "ID_SERVER_CUSTOM", wxRadioButton)->GetValue();
	XRCCTRL(*this, "ID_HOST", wxWindow)->Enable(server_enabled);
	XRCCTRL(*this, "ID_PORT", wxWindow)->Enable(server_enabled);
	XRCCTRL(*this, "ID_PROTOCOL", wxWindow)->Enable(server_enabled);
	XRCCTRL(*this, "ID_LOGONTYPE", wxWindow)->Enable(server_enabled);

	wxString logon_type = XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->GetStringSelection();
	XRCCTRL(*this, "ID_USER", wxTextCtrl)->Enable(server_enabled && logon_type != _("Anonymous"));
	XRCCTRL(*this, "ID_PASS", wxTextCtrl)->Enable(server_enabled && (logon_type == _("Normal") || logon_type == _("Account")));
	XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->Enable(server_enabled && logon_type == _("Account"));
}

void CManualTransfer::DisplayServer()
{
	if (server_) {
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->ChangeValue(server_.Format(ServerFormat::host_only));
		unsigned int port = server_.server.GetPort();

		if (port != CServer::GetDefaultPort(server_.server.GetProtocol())) {
			XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString::Format(_T("%d"), port));
		}
		else {
			XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(_T(""));
		}

		const wxString& protocolName = CServer::GetProtocolName(server_.server.GetProtocol());
		if (!protocolName.empty()) {
			XRCCTRL(*this, "ID_PROTOCOL", wxChoice)->SetStringSelection(protocolName);
		}
		else {
			XRCCTRL(*this, "ID_PROTOCOL", wxChoice)->SetStringSelection(CServer::GetProtocolName(FTP));
		}

		switch (server_.credentials.logonType_)
		{
		case LogonType::normal:
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(_("Normal"));
			break;
		case LogonType::ask:
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(_("Ask for password"));
			break;
		case LogonType::interactive:
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(_("Interactive"));
			break;
		case LogonType::account:
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(_("Account"));
			break;
		default:
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(_("Anonymous"));
			break;
		}

		XRCCTRL(*this, "ID_USER", wxTextCtrl)->ChangeValue(server_.server.GetUser());
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->ChangeValue(server_.credentials.account_);
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->ChangeValue(server_.credentials.GetPass());
	}
	else {
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->ChangeValue(_T(""));
		XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(_T(""));
		XRCCTRL(*this, "ID_PROTOCOL", wxChoice)->SetStringSelection(CServer::GetProtocolName(FTP));
		XRCCTRL(*this, "ID_USER", wxTextCtrl)->Enable(false);
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->Enable(false);
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->Enable(false);
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(_("Anonymous"));

		XRCCTRL(*this, "ID_USER", wxTextCtrl)->ChangeValue(_T(""));
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->ChangeValue(_T(""));
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->ChangeValue(_T(""));
	}
}

void CManualTransfer::OnLocalChanged(wxCommandEvent&)
{
	if (XRCCTRL(*this, "ID_DOWNLOAD", wxRadioButton)->GetValue()) {
		return;
	}

	wxString file = XRCCTRL(*this, "ID_LOCALFILE", wxTextCtrl)->GetValue();

	m_local_file_exists = fz::local_filesys::get_file_type(fz::to_native(file)) == fz::local_filesys::file;

	SetAutoAsciiState();
}

void CManualTransfer::OnRemoteChanged(wxCommandEvent&)
{
	SetAutoAsciiState();
}

void CManualTransfer::OnLocalBrowse(wxCommandEvent&)
{
	int flags;
	wxString title;
	if (xrc_call(*this, "ID_DOWNLOAD", &wxRadioButton::GetValue)) {
		flags = wxFD_SAVE | wxFD_OVERWRITE_PROMPT;
		title = _("Select target filename");
	}
	else {
		flags = wxFD_OPEN | wxFD_FILE_MUST_EXIST;
		title = _("Select file to upload");
	}

	wxFileDialog dlg(this, title, _T(""), _T(""), _T("*.*"), flags);
	int res = dlg.ShowModal();

	if (res != wxID_OK) {
		return;
	}

	// SetValue on purpose
	xrc_call(*this, "ID_LOCALFILE", &wxTextCtrl::SetValue, dlg.GetPath());
}

void CManualTransfer::OnDirection(wxCommandEvent& event)
{
	if (xrc_call(*this, "ID_DOWNLOAD", &wxRadioButton::GetValue)) {
		SetAutoAsciiState();
	}
	else {
		// Need to check for file existence
		OnLocalChanged(event);
	}
}

void CManualTransfer::OnServerTypeChanged(wxCommandEvent& event)
{
	if (event.GetId() == XRCID("ID_SERVER_CURRENT")) {
		server_ = m_pState->GetServer();
	}
	else if (event.GetId() == XRCID("ID_SERVER_SITE")) {
		server_ = lastSite_;
	}
	xrc_call(*this, "ID_SERVER_SITE_SELECT", &wxButton::Enable, event.GetId() == XRCID("ID_SERVER_SITE"));
	DisplayServer();
	SetServerState();
}

void CManualTransfer::OnOK(wxCommandEvent&)
{
	if (!UpdateServer()) {
		return;
	}

	bool download = xrc_call(*this, "ID_DOWNLOAD", &wxRadioButton::GetValue);

	bool start = xrc_call(*this, "ID_START", &wxCheckBox::GetValue);

	if (!server_) {
		wxMessageBoxEx(_("You need to specify a server."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring local_file = xrc_call(*this, "ID_LOCALFILE", &wxTextCtrl::GetValue).ToStdWstring();
	if (local_file.empty()) {
		wxMessageBoxEx(_("You need to specify a local file."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	fz::local_filesys::type type = fz::local_filesys::get_file_type(fz::to_native(local_file));
	if (type == fz::local_filesys::dir) {
		wxMessageBoxEx(_("Local file is a directory instead of a regular file."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}
	if (!download && type != fz::local_filesys::file && start) {
		wxMessageBoxEx(_("Local file does not exist."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring remote_file = xrc_call(*this, "ID_REMOTEFILE", &wxTextCtrl::GetValue).ToStdWstring();

	if (remote_file.empty()) {
		wxMessageBoxEx(_("You need to specify a remote file."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring remote_path_str = xrc_call(*this, "ID_REMOTEPATH", &wxTextCtrl::GetValue).ToStdWstring();
	if (remote_path_str.empty()) {
		wxMessageBoxEx(_("You need to specify a remote path."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	CServerPath path(remote_path_str, server_.server.GetType());
	if (path.empty()) {
		wxMessageBoxEx(_("Remote path could not be parsed."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	int old_data_type = COptions::Get()->GetOptionVal(OPTION_ASCIIBINARY);

	// Set data type for the file to add
	if (xrc_call(*this, "ID_TYPE_ASCII", &wxRadioButton::GetValue)) {
		COptions::Get()->SetOption(OPTION_ASCIIBINARY, 1);
	}
	else if (xrc_call(*this, "ID_TYPE_BINARY", &wxRadioButton::GetValue)) {
		COptions::Get()->SetOption(OPTION_ASCIIBINARY, 2);
	}
	else {
		COptions::Get()->SetOption(OPTION_ASCIIBINARY, 0);
	}

	std::wstring name;
	CLocalPath localPath(local_file, &name);

	if (name.empty()) {
		wxMessageBoxEx(_("Local file is not a valid filename."), _("Manual transfer"), wxICON_EXCLAMATION);
		return;
	}

	m_pQueueView->QueueFile(!start, download,
		download ? remote_file : name,
		(remote_file != name) ? (download ? name : remote_file) : std::wstring(),
		localPath, path, server_, -1);

	// Restore old data type
	COptions::Get()->SetOption(OPTION_ASCIIBINARY, old_data_type);

	m_pQueueView->QueueFile_Finish(start);

	EndModal(wxID_OK);
}

bool CManualTransfer::UpdateServer()
{
	if (!xrc_call(*this, "ID_SERVER_CUSTOM", &wxRadioButton::GetValue)) {
		return true;
	}

	if (!VerifyServer()) {
		return false;
	}

	unsigned long port;
	if (!xrc_call(*this, "ID_PORT", &wxTextCtrl::GetValue).ToULong(&port)) {
		return false;
	}

	server_ = ServerWithCredentials();

	std::wstring host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
	// SetHost does not accept URL syntax
	if (!host.empty() && host[0] == '[') {
		host = host.substr(1, host.size() - 2);
	}
	server_.server.SetHost(host, port);

	std::wstring const protocolName = xrc_call(*this, "ID_PROTOCOL", &wxChoice::GetStringSelection).ToStdWstring();
	ServerProtocol const protocol = CServer::GetProtocolFromName(protocolName);
	if (protocol != UNKNOWN) {
		server_.server.SetProtocol(protocol);
	}
	else {
		server_.server.SetProtocol(FTP);
	}

	server_.SetLogonType(GetLogonTypeFromName(xrc_call(*this, "ID_LOGONTYPE", &wxChoice::GetStringSelection).ToStdWstring()));
	server_.SetUser(xrc_call(*this, "ID_USER", &wxTextCtrl::GetValue).ToStdWstring());
	server_.credentials.SetPass(xrc_call(*this, "ID_PASS", &wxTextCtrl::GetValue).ToStdWstring());
	server_.credentials.account_ = xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::GetValue).ToStdWstring();

	return true;
}

bool CManualTransfer::VerifyServer()
{
	std::wstring const host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
	if (host.empty()) {
		xrc_call(*this, "ID_HOST", &wxTextCtrl::SetFocus);
		wxMessageBoxEx(_("You have to enter a hostname."));
		return false;
	}

	LogonType logon_type = GetLogonTypeFromName(xrc_call(*this, "ID_LOGONTYPE", &wxChoice::GetStringSelection).ToStdWstring());

	std::wstring protocolName = xrc_call(*this, "ID_PROTOCOL", &wxChoice::GetStringSelection).ToStdWstring();
	ServerProtocol protocol = CServer::GetProtocolFromName(protocolName);
	if (protocol == SFTP &&
		logon_type == LogonType::account)
	{
		xrc_call(*this, "ID_LOGONTYPE", &wxChoice::SetFocus);
		wxMessageBoxEx(_("'Account' logontype not supported by selected protocol"));
		return false;
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0 &&
		(logon_type == LogonType::account || logon_type == LogonType::normal))
	{
		xrc_call(*this, "ID_LOGONTYPE", &wxChoice::SetFocus);
		wxString msg;
		if (COptions::Get()->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) && COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0) {
			msg = _("Saving of password has been disabled by your system administrator.");
		}
		else {
			msg = _("Saving of passwords has been disabled by you.");
		}
		msg += _T("\n");
		msg += _("'Normal' and 'Account' logontypes are not available, using 'Ask for password' instead.");
		xrc_call(*this, "ID_LOGONTYPE", &wxChoice::SetStringSelection, GetNameFromLogonType(LogonType::ask));
		xrc_call(*this, "ID_PASS", &wxTextCtrl::ChangeValue, _T(""));
		logon_type = LogonType::ask;
		wxMessageBoxEx(msg, _("Cannot remember password"), wxICON_INFORMATION, this);
	}

	ServerWithCredentials server;

	// Set selected type
	server.SetLogonType(logon_type);

	if (protocol != UNKNOWN) {
		server.server.SetProtocol(protocol);
	}

	CServerPath path;
	std::wstring error;
	if (!server.ParseUrl(host, xrc_call(*this, "ID_PORT", &wxTextCtrl::GetValue).ToStdWstring(), std::wstring(), std::wstring(), error, path, protocol)) {
		xrc_call(*this, "ID_HOST", &wxTextCtrl::SetFocus);
		wxMessageBoxEx(error);
		return false;
	}

	xrc_call(*this, "ID_HOST", &wxTextCtrl::ChangeValue, server.Format(ServerFormat::host_only));
	xrc_call(*this, "ID_PORT", &wxTextCtrl::ChangeValue, wxString::Format(_T("%d"), server.server.GetPort()));

	protocolName = CServer::GetProtocolName(server.server.GetProtocol());
	if (protocolName.empty()) {
		CServer::GetProtocolName(FTP);
	}
	xrc_call(*this, "ID_PROTOCOL", &wxChoice::SetStringSelection, protocolName);

	// Require username for non-anonymous, non-ask logon type
	const wxString user = xrc_call(*this, "ID_USER", &wxTextCtrl::GetValue);
	if (logon_type != LogonType::anonymous &&
		logon_type != LogonType::ask &&
		logon_type != LogonType::interactive &&
		user.empty())
	{
		xrc_call(*this, "ID_USER", &wxTextCtrl::SetFocus);
		wxMessageBoxEx(_("You have to specify a user name"));
		return false;
	}

	// We don't allow username of only spaces, confuses both users and XML libraries
	if (!user.empty()) {
		bool space_only = true;
		for (unsigned int i = 0; i < user.Len(); ++i) {
			if (user[i] != ' ') {
				space_only = false;
				break;
			}
		}
		if (space_only) {
			xrc_call(*this, "ID_USER", &wxTextCtrl::SetFocus);
			wxMessageBoxEx(_("Username cannot be a series of spaces"));
			return false;
		}

	}

	// Require account for account logon type
	if (logon_type == LogonType::account &&
		xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::GetValue).empty())
	{
		xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::SetFocus);
		wxMessageBoxEx(_("You have to enter an account name"));
		return false;
	}

	return true;
}

void CManualTransfer::OnSelectSite(wxCommandEvent&)
{
	std::unique_ptr<wxMenu> pMenu = CSiteManager::GetSitesMenu();
	if (pMenu) {
		PopupMenu(pMenu.get());
	}
}

void CManualTransfer::OnSelectedSite(wxCommandEvent& event)
{
	std::unique_ptr<Site> pData = CSiteManager::GetSiteById(event.GetId());
	if (!pData) {
		return;
	}

	server_ = pData->server_;
	lastSite_ = pData->server_;

	xrc_call(*this, "ID_SERVER_SITE_SERVER", &wxStaticText::SetLabel, server_.server.GetName());

	DisplayServer();
}

void CManualTransfer::OnLogontypeSelChanged(wxCommandEvent& event)
{
	xrc_call(*this, "ID_USER", &wxTextCtrl::Enable, event.GetString() != _("Anonymous"));
	xrc_call(*this, "ID_PASS", &wxTextCtrl::Enable, event.GetString() == _("Normal") || event.GetString() == _("Account"));
	xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::Enable, event.GetString() == _("Account"));
}
