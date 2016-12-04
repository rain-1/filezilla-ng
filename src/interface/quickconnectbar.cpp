#include <filezilla.h>
#include "quickconnectbar.h"
#include "recentserverlist.h"
#include "commandqueue.h"
#include "state.h"
#include "Options.h"
#include "loginmanager.h"
#include "Mainfrm.h"
#include "asksavepassworddialog.h"
#include "filezillaapp.h"

#include <wx/bmpbuttn.h>
#include <wx/statline.h>

BEGIN_EVENT_TABLE(CQuickconnectBar, wxPanel)
EVT_BUTTON(XRCID("ID_QUICKCONNECT_OK"), CQuickconnectBar::OnQuickconnect)
EVT_BUTTON(XRCID("ID_QUICKCONNECT_DROPDOWN"), CQuickconnectBar::OnQuickconnectDropdown)
EVT_MENU(wxID_ANY, CQuickconnectBar::OnMenu)
EVT_TEXT_ENTER(wxID_ANY, CQuickconnectBar::OnQuickconnect)
END_EVENT_TABLE()

CQuickconnectBar::CQuickconnectBar()
	: m_pHost()
	, m_pUser()
	, m_pPass()
	, m_pPort()
	, m_pMainFrame()
{
}

CQuickconnectBar::~CQuickconnectBar()
{
}

bool CQuickconnectBar::Create(CMainFrame* pParent)
{
	m_pMainFrame = pParent;

	if (!wxPanel::Create(pParent, XRCID("ID_QUICKCONNECT"))) {
		return false;
	}

	auto sizer = new wxBoxSizer(wxVERTICAL);
	SetSizer(sizer);
#ifndef __WXMAC__
	sizer->Add(new wxStaticLine(this, -1, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL), wxSizerFlags().Expand());
#endif
	auto mainSizer = new wxFlexGridSizer(1, 0, 0, 5);
	sizer->Add(mainSizer, wxSizerFlags().Border(wxALL, ConvertDialogToPixels(wxPoint(2, 0)).x));

	mainSizer->Add(new wxStaticText(this, -1, _("&Host:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pHost = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition, ConvertDialogToPixels(wxSize(63, -1)), wxTE_PROCESS_ENTER);
	m_pHost->SetToolTip(_("Enter the address of the server. To specify the server protocol, prepend the host with the protocol identifier. If no protocol is specified, the default protocol (ftp://) will be used. You can also enter complete URLs in the form protocol://user:pass@host:port here, the values in the other fields will be overwritten then.\n\nSupported protocols are:\n- ftp:// for normal FTP with optional encryption\n- sftp:// for SSH file transfer protocol\n- ftps:// for FTP over TLS (implicit)\n- ftpes:// for FTP over TLS (explicit)"));
	mainSizer->Add(m_pHost, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	mainSizer->Add(new wxStaticText(this, -1, _("&Username:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pUser = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	mainSizer->Add(m_pUser, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	mainSizer->Add(new wxStaticText(this, -1, _("Pass&word:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pPass = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER|wxTE_PASSWORD);
	mainSizer->Add(m_pPass, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	mainSizer->Add(new wxStaticText(this, -1, _("&Port:")), wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	m_pPort = new wxTextCtrl(this, -1, wxString(), wxDefaultPosition, ConvertDialogToPixels(wxSize(27, -1)), wxTE_PROCESS_ENTER);
	m_pPort->SetToolTip(_("Enter the port on which the server listens. The default for FTP is 21, the default for SFTP is 22."));
	m_pPort->SetMaxLength(5);
	mainSizer->Add(m_pPort, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

	auto connectSizer = new wxBoxSizer(wxHORIZONTAL);
	mainSizer->Add(connectSizer, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));
	auto connect = new wxButton(this, XRCID("ID_QUICKCONNECT_OK"), _("&Quickconnect"));
	connect->SetDefault();
	connectSizer->Add(connect, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL));

	wxBitmap bmp(wxGetApp().GetResourceDir().GetPath() + _T("dropdown.png"), wxBITMAP_TYPE_PNG);
	auto flags = wxSizerFlags().Expand();
#if defined(__WXMSW__)
	wxSize dropdownSize = ConvertDialogToPixels(wxSize(12, -1));
#elif defined(__WXMAC__)
	wxSize dropdownSize(20, -1);
	flags.Border(wxLEFT, 6);
#else
	wxSize dropdownSize(-1, -1);
#endif
	connectSizer->Add(new wxBitmapButton(this, XRCID("ID_QUICKCONNECT_DROPDOWN"), bmp, wxDefaultPosition, dropdownSize, wxBU_AUTODRAW | wxBU_EXACTFIT), flags);

#ifdef __WXMAC__
	// Under OS X default buttons are toplevel window wide, where under Windows / GTK they stop at the parent panel.
	wxTopLevelWindow *tlw = dynamic_cast<wxTopLevelWindow*>(wxGetTopLevelParent(pParent));
	if (tlw) {
		tlw->SetDefaultItem(0);
	}
#endif

	return true;
}

void CQuickconnectBar::OnQuickconnect(wxCommandEvent& event)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState || !pState->m_pEngine) {
		wxMessageBoxEx(_("FTP Engine not initialized, can't connect"), _("FileZilla Error"), wxICON_EXCLAMATION);
		return;
	}

	std::wstring host = m_pHost->GetValue().ToStdWstring();
	std::wstring user = m_pUser->GetValue().ToStdWstring();
	std::wstring pass = m_pPass->GetValue().ToStdWstring();
	std::wstring port = m_pPort->GetValue().ToStdWstring();

	CServer server;

	std::wstring error;

	CServerPath path;
	if (!server.ParseUrl(host, port, user, pass, error, path)) {
		wxString msg = _("Could not parse server address:");
		msg += _T("\n");
		msg += error;
		wxMessageBoxEx(msg, _("Syntax error"), wxICON_EXCLAMATION);
		return;
	}

	host = server.Format(ServerFormat::host_only);
	ServerProtocol protocol = server.GetProtocol();
	switch (protocol)
	{
	case FTP:
	case UNKNOWN:
		if (CServer::GetProtocolFromPort(server.GetPort()) != FTP &&
			CServer::GetProtocolFromPort(server.GetPort()) != UNKNOWN)
		{
			host = _T("ftp://") + host;
		}
		break;
	default:
		{
			const wxString prefix = server.GetPrefixFromProtocol(protocol);
			if (!prefix.empty()) {
				host = prefix + _T("://") + host;
			}
		}
		break;
	}

	m_pHost->SetValue(host);
	if (server.GetPort() != server.GetDefaultPort(server.GetProtocol())) {
		m_pPort->SetValue(wxString::Format(_T("%d"), server.GetPort()));
	}
	else {
		m_pPort->ChangeValue(wxString());
	}

	m_pUser->SetValue(server.GetUser());
	if (server.GetLogonType() != ANONYMOUS) {
		m_pPass->SetValue(server.GetPass());
	}
	else {
		m_pPass->ChangeValue(wxString());
	}

	if (protocol == HTTP || protocol == HTTPS) {
		wxString protocolError = _("Invalid protocol specified. Valid protocols are:\nftp:// for normal FTP with optional encryption,\nsftp:// for SSH file transfer protocol,\nftps:// for FTP over TLS (implicit) and\nftpes:// for FTP over TLS (explicit).");
		wxMessageBoxEx(protocolError, _("Syntax error"), wxICON_EXCLAMATION);
		return;
	}

	if (event.GetId() == 1) {
		server.SetBypassProxy(true);
	}

	if (server.GetLogonType() != ANONYMOUS && !CAskSavePasswordDialog::Run(this)) {
		return;
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) && server.GetLogonType() == NORMAL) {
		server.SetLogonType(ASK);
		CLoginManager::Get().RememberPassword(server);
	}
	Site site;
	site.m_server = server;
	Bookmark bm;
	bm.m_remoteDir = path;
	if (!m_pMainFrame->ConnectToSite(site, bm)) {
		return;
	}

	CRecentServerList::SetMostRecentServer(server);
}

void CQuickconnectBar::OnQuickconnectDropdown(wxCommandEvent&)
{
	wxMenu* pMenu = new wxMenu;

	// We have to start with id 1 since menu items with id 0 don't work under OS X
	if (COptions::Get()->GetOptionVal(OPTION_FTP_PROXY_TYPE))
		pMenu->Append(1, _("Connect bypassing proxy settings"));
	pMenu->Append(2, _("Clear quickconnect bar"));
	pMenu->Append(3, _("Clear history"));

	m_recentServers = CRecentServerList::GetMostRecentServers();
	if (!m_recentServers.empty()) {
		pMenu->AppendSeparator();

		unsigned int i = 0;
		for (std::list<CServer>::const_iterator iter = m_recentServers.begin();
			iter != m_recentServers.end();
			++iter, ++i)
		{
			wxString name(iter->Format(ServerFormat::with_user_and_optional_port));
			name.Replace(_T("&"), _T("&&"));
			pMenu->Append(10 + i, name);
		}
	}
	else {
		pMenu->Enable(3, false);
	}

	XRCCTRL(*this, "ID_QUICKCONNECT_DROPDOWN", wxButton)->PopupMenu(pMenu);
	delete pMenu;
	m_recentServers.clear();
}

void CQuickconnectBar::OnMenu(wxCommandEvent& event)
{
	const int id = event.GetId();
	if (id == 1) {
		OnQuickconnect(event);
	}
	else if (id == 2) {
		ClearFields();
	}
	else if (id == 3) {
		CRecentServerList::Clear();
	}

	if (id < 10) {
		return;
	}

	unsigned int index = id - 10;
	if (index >= m_recentServers.size()) {
		return;
	}

	std::list<CServer>::const_iterator iter = m_recentServers.begin();
	std::advance(iter, index);

	Site site;
	site.m_server = *iter;
	m_pMainFrame->ConnectToSite(site, Bookmark());
}

void CQuickconnectBar::ClearFields()
{
	m_pHost->SetValue(_T(""));
	m_pPort->SetValue(_T(""));
	m_pUser->SetValue(_T(""));
	m_pPass->SetValue(_T(""));
}
