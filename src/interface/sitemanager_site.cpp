#include <filezilla.h>
#include "sitemanager_site.h"

#include "filezillaapp.h"
#include "fzputtygen_interface.h"
#include "Options.h"
#if USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif
#include "sitemanager_dialog.h"
#if ENABLE_STORJ
#include "storj_key_interface.h"
#endif
#include "xrc_helper.h"

#include <wx/dcclient.h>
#include <wx/gbsizer.h>

#ifdef __WXMSW__
#include "commctrl.h"
#endif

#include <array>

BEGIN_EVENT_TABLE(CSiteManagerSite, wxNotebook)
EVT_CHOICE(XRCID("ID_PROTOCOL"), CSiteManagerSite::OnProtocolSelChanged)
EVT_CHOICE(XRCID("ID_LOGONTYPE"), CSiteManagerSite::OnLogontypeSelChanged)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_AUTO"), CSiteManagerSite::OnCharsetChange)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_UTF8"), CSiteManagerSite::OnCharsetChange)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_CUSTOM"), CSiteManagerSite::OnCharsetChange)
EVT_CHECKBOX(XRCID("ID_LIMITMULTIPLE"), CSiteManagerSite::OnLimitMultipleConnectionsChanged)
EVT_BUTTON(XRCID("ID_BROWSE"), CSiteManagerSite::OnRemoteDirBrowse)
EVT_BUTTON(XRCID("ID_KEYFILE_BROWSE"), CSiteManagerSite::OnKeyFileBrowse)
EVT_BUTTON(XRCID("ID_ENCRYPTIONKEY_GENERATE"), CSiteManagerSite::OnGenerateEncryptionKey)
END_EVENT_TABLE()

namespace {
std::array<ServerProtocol, 4> const ftpSubOptions{ FTP, FTPES, FTPS, INSECURE_FTP };
}

CSiteManagerSite::CSiteManagerSite(CSiteManagerDialog &sitemanager)
    : sitemanager_(sitemanager)
{
}

bool CSiteManagerSite::Load(wxWindow* parent)
{
	if (!wxXmlResource::Get()->LoadObject(this, parent, _T("ID_SITEMANAGER_NOTEBOOK_SITE"), _T("wxNotebook"))) {
		return false;
	}

	extraParameters_[ParameterSection::host].emplace_back(XRCCTRL(*this, "ID_EXTRA_HOST_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_HOST", wxTextCtrl));
	extraParameters_[ParameterSection::user].emplace_back(XRCCTRL(*this, "ID_EXTRA_USER_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_USER", wxTextCtrl));
	extraParameters_[ParameterSection::credentials].emplace_back(XRCCTRL(*this, "ID_EXTRA_CREDENTIALS_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_CREDENTIALS", wxTextCtrl));
	extraParameters_[ParameterSection::extra].emplace_back(XRCCTRL(*this, "ID_EXTRA_EXTRA_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_EXTRA", wxTextCtrl));

	InitProtocols();

	m_totalPages = GetPageCount();
	m_pCharsetPage = XRCCTRL(*this, "ID_CHARSET_PANEL", wxPanel);
	if (m_pCharsetPage) {
		m_charsetPageIndex = FindPage(m_pCharsetPage);
		m_charsetPageText = GetPageText(m_charsetPageIndex);
	}

	auto generalSizer = static_cast<wxGridBagSizer*>(xrc_call(*this, "ID_PROTOCOL", &wxWindow::GetContainingSizer));
	generalSizer->SetEmptyCellSize(wxSize(-generalSizer->GetHGap(), -generalSizer->GetVGap()));

	GetPage(0)->GetSizer()->Fit(GetPage(0));

#ifdef __WXMSW__
	// Make pages at least wide enough to fit all tabs
	HWND hWnd = (HWND)GetHandle();

	int width = 4;
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		RECT tab_rect{};
		if (TabCtrl_GetItemRect(hWnd, i, &tab_rect)) {
			width += tab_rect.right - tab_rect.left;
		}
	}
#else
	// Make pages at least wide enough to fit all tabs
	int width = 10; // Guessed
	wxClientDC dc(this);
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		wxCoord w, h;
		dc.GetTextExtent(GetPageText(i), &w, &h);

		width += w;
#ifdef __WXMAC__
		width += 20; // Guessed
#else
		width += 20;
#endif
	}
#endif

	wxSize const descSize = XRCCTRL(*this, "ID_ENCRYPTION_DESC", wxWindow)->GetSize();
	wxSize const encSize = XRCCTRL(*this, "ID_ENCRYPTION", wxWindow)->GetSize();

	int dataWidth = std::max(encSize.GetWidth(), XRCCTRL(*this, "ID_PROTOCOL", wxWindow)->GetSize().GetWidth());

	width = std::max(width, static_cast<int>(descSize.GetWidth() * 2 + dataWidth + generalSizer->GetHGap() * 3));

	wxSize page_min_size = GetPage(0)->GetSizer()->GetMinSize();
	if (page_min_size.x < width) {
		page_min_size.x = width;
		GetPage(0)->GetSizer()->SetMinSize(page_min_size);
	}

	// Set min height of general page sizer
	generalSizer->SetMinSize(generalSizer->GetMinSize());

	// Set min height of encryption row
	auto encSizer = xrc_call(*this, "ID_ENCRYPTION", &wxWindow::GetContainingSizer);
	encSizer->GetItem(encSizer->GetItemCount() - 1)->SetMinSize(0, std::max(descSize.GetHeight(), encSize.GetHeight()));

	return true;
}

void CSiteManagerSite::InitProtocols()
{
	wxChoice *pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	if (!pProtocol) {
		return;
	}

	mainProtocolListIndex_[FTP] = pProtocol->Append(_("FTP - File Transfer Protocol"));
	for (auto const& proto : CServer::GetDefaultProtocols()) {
		if (std::find(ftpSubOptions.cbegin(), ftpSubOptions.cend(), proto) == ftpSubOptions.cend()) {
			mainProtocolListIndex_[proto] = pProtocol->Append(CServer::GetProtocolName(proto));
		}
		else {
			mainProtocolListIndex_[proto] = mainProtocolListIndex_[FTP];
		}
	}

	wxChoice *pChoice = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice);
	wxASSERT(pChoice);
	for (int i = 0; i < SERVERTYPE_MAX; ++i) {
		pChoice->Append(CServer::GetNameFromServerType(static_cast<ServerType>(i)));
	}

	pChoice = XRCCTRL(*this, "ID_LOGONTYPE", wxChoice);
	wxASSERT(pChoice);
	for (int i = 0; i < static_cast<int>(LogonType::count); ++i) {
		pChoice->Append(GetNameFromLogonType(static_cast<LogonType>(i)));
	}

	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);

	// Order must match ftpSubOptions
	pEncryption->Append(_("Use explicit FTP over TLS if available"));
	pEncryption->Append(_("Require explicit FTP over TLS"));
	pEncryption->Append(_("Require implicit FTP over TLS"));
	pEncryption->Append(_("Only use plain FTP (insecure)"));
	pEncryption->SetSelection(0);

	wxChoice* pColors = XRCCTRL(*this, "ID_COLOR", wxChoice);
	if (pColors) {
		for (int i = 0; ; ++i) {
			wxString name = CSiteManager::GetColourName(i);
			if (name.empty()) {
				break;
			}
			pColors->AppendString(wxGetTranslation(name));
		}
	}
}

void CSiteManagerSite::SetProtocol(ServerProtocol protocol)
{
	wxChoice* pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);
	wxStaticText* pEncryptionDesc = XRCCTRL(*this, "ID_ENCRYPTION_DESC", wxStaticText);

	auto const it = std::find(ftpSubOptions.cbegin(), ftpSubOptions.cend(), protocol);

	if (it != ftpSubOptions.cend()) {
		pEncryption->SetSelection(it - ftpSubOptions.cbegin());
		pEncryption->Show();
		pEncryptionDesc->Show();
	}
	else {
		pEncryption->Hide();
		pEncryptionDesc->Hide();
	}
	auto const protoIt = mainProtocolListIndex_.find(protocol);
	if (protoIt != mainProtocolListIndex_.cend()) {
		pProtocol->SetSelection(protoIt->second);
	}
	else if (protocol != ServerProtocol::UNKNOWN) {
		mainProtocolListIndex_[protocol] = pProtocol->Append(CServer::GetProtocolName(protocol));
		pProtocol->SetSelection(mainProtocolListIndex_[protocol]);
	}
	else {
		pProtocol->SetSelection(mainProtocolListIndex_[FTP]);
	}
	UpdateHostFromDefaults(GetProtocol());

	previousProtocol_ = protocol;
}

ServerProtocol CSiteManagerSite::GetProtocol() const
{
	int const sel = xrc_call(*this, "ID_PROTOCOL", &wxChoice::GetSelection);
	if (sel == mainProtocolListIndex_.at(FTP)) {
		int encSel = xrc_call(*this, "ID_ENCRYPTION", &wxChoice::GetSelection);
		if (encSel >= 0 && encSel < static_cast<int>(ftpSubOptions.size())) {
			return ftpSubOptions[encSel];
		}

		return FTP;
	}
	else {
		for (auto const it : mainProtocolListIndex_) {
			if (it.second == sel) {
				return it.first;
			}
		}
	}

	return UNKNOWN;
}

void CSiteManagerSite::SetControlVisibility(ServerProtocol protocol, LogonType type)
{
	bool const isFtp = std::find(ftpSubOptions.cbegin(), ftpSubOptions.cend(), protocol) != ftpSubOptions.cend();

	xrc_call(*this, "ID_ENCRYPTION_DESC", &wxStaticText::Show, isFtp);
	xrc_call(*this, "ID_ENCRYPTION", &wxChoice::Show, isFtp);

	xrc_call(*this, "ID_SIGNUP", &wxControl::Show, protocol == STORJ);

	auto const supportedlogonTypes = GetSupportedLogonTypes(protocol);
	assert(!supportedlogonTypes.empty());

	auto choice = XRCCTRL(*this, "ID_LOGONTYPE", wxChoice);
	choice->Clear();

	if (std::find(supportedlogonTypes.cbegin(), supportedlogonTypes.cend(), type) == supportedlogonTypes.cend()) {
		type = supportedlogonTypes.front();
	}

	for (auto const supportedLogonType : supportedlogonTypes) {
		choice->Append(GetNameFromLogonType(supportedLogonType));
		if (supportedLogonType == type) {
			choice->SetSelection(choice->GetCount() - 1);
		}
	}

	xrc_call(*this, "ID_USER_DESC", &wxStaticText::Show, type != LogonType::anonymous);
	xrc_call(*this, "ID_USER", &wxTextCtrl::Show, type != LogonType::anonymous);
	xrc_call(*this, "ID_PASS_DESC", &wxStaticText::Show, type != LogonType::anonymous && type != LogonType::interactive  && (protocol != SFTP || type != LogonType::key));
	xrc_call(*this, "ID_PASS", &wxTextCtrl::Show, type != LogonType::anonymous && type != LogonType::interactive && (protocol != SFTP || type != LogonType::key));
	xrc_call(*this, "ID_ACCOUNT_DESC", &wxStaticText::Show, isFtp && type == LogonType::account);
	xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::Show, isFtp && type == LogonType::account);
	xrc_call(*this, "ID_KEYFILE_DESC", &wxStaticText::Show, protocol == SFTP && type == LogonType::key);
	xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::Show, protocol == SFTP && type == LogonType::key);
	xrc_call(*this, "ID_KEYFILE_BROWSE", &wxButton::Show, protocol == SFTP && type == LogonType::key);

	xrc_call(*this, "ID_ENCRYPTIONKEY_DESC", &wxStaticText::Show, protocol == STORJ);
	xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::Show, protocol == STORJ);
	xrc_call(*this, "ID_ENCRYPTIONKEY_GENERATE", &wxButton::Show, protocol == STORJ);

	wxString hostLabel = _("&Host:");
	wxString hostHint;
	wxString userHint;
	wxString userLabel = _("&User:");
	wxString passLabel = _("Pass&word:");
	switch (protocol) {
	case S3:
		// @translator: Keep short
		userLabel = _("&Access key ID:");
		// @translator: Keep short
		passLabel = _("Secret Access &Key:");
		break;
	case AZURE_FILE:
	case AZURE_BLOB:
		// @translator: Keep short
		userLabel = _("Storage &account:");
		passLabel = _("Access &Key:");
		break;
	case GOOGLE:
		userLabel = _("Pro&ject ID:");
		break;
	case SWIFT:
		// @translator: Keep short
		hostLabel = _("Identity &host:");
		// @translator: Keep short
		hostHint = _("Host name of identity service");
		userLabel = _("Pro&ject:");
		// @translator: Keep short
		userHint = _("Project (or tenant) name or ID");
		break;
	default:
		break;
	}
	xrc_call(*this, "ID_HOST_DESC", &wxStaticText::SetLabel, hostLabel);
	xrc_call(*this, "ID_HOST", &wxTextCtrl::SetHint, hostHint);
	xrc_call(*this, "ID_USER_DESC", &wxStaticText::SetLabel, userLabel);
	xrc_call(*this, "ID_PASS_DESC", &wxStaticText::SetLabel, passLabel);
	xrc_call(*this, "ID_USER", &wxTextCtrl::SetHint, userHint);

	auto InsertRow = [this](std::vector<std::pair<wxStaticText*, wxTextCtrl*>> & rows, bool password) {

		if (rows.empty()) {
			return rows.end();
		}

		wxGridBagSizer* sizer = dynamic_cast<wxGridBagSizer*>(rows.back().first->GetContainingSizer());
		if (!sizer) {
			return rows.end();
		}
		auto pos = sizer->GetItemPosition(rows.back().first);

		for (int row = sizer->GetRows() - 1; row > pos.GetRow(); --row) {
			auto left = sizer->FindItemAtPosition(wxGBPosition(row, 0));
			auto right = sizer->FindItemAtPosition(wxGBPosition(row, 1));
			if (!left) {
				break;
			}
			left->SetPos(wxGBPosition(row + 1, 0));
			if (right) {
				right->SetPos(wxGBPosition(row + 1, 1));
			}
		}
		auto label = new wxStaticText(rows.back().first->GetParent(), wxID_ANY, L"");
		auto text = new wxTextCtrl(rows.back().first->GetParent(), wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize, password ? wxTE_PASSWORD : 0);
		sizer->Add(label, wxGBPosition(pos.GetRow() + 1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
		sizer->Add(text, wxGBPosition(pos.GetRow() + 1, 1), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxGROW);

		rows.emplace_back(label, text);
		return rows.end() - 1;
	};

	auto SetLabel = [](wxStaticText & label, ServerProtocol const protocol, std::string const& name) {
		if (name == "email") {
			label.SetLabel(_("E-&mail account:"));
		}
		else if (name == "identpath") {
			// @translator: Keep short
			label.SetLabel(_("Identity service path:"));
		}
		else if (name == "identuser") {
			label.SetLabel(_("&User:"));
		}
		else {
			label.SetLabel(name);
		}
	};

	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		auto & parameters = extraParameters_[trait.section_];
		auto & it = paramIt[trait.section_];

		if (it == parameters.cend()) {
			it = InsertRow(parameters, trait.section_ == ParameterSection::credentials);
		}

		if (it == parameters.cend()) {
			continue;
		}
		it->first->Show();
		it->second->Show();
		SetLabel(*it->first, protocol, trait.name_);
		it->second->SetHint(trait.hint_);

		++it;
	}
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (; paramIt[i] != extraParameters_[i].cend(); ++paramIt[i]) {
			paramIt[i]->first->Hide();
			paramIt[i]->second->Hide();
		}
	}

	auto keyfileSizer = xrc_call(*this, "ID_KEYFILE_DESC", &wxStaticText::GetContainingSizer);
	if (keyfileSizer) {
		keyfileSizer->CalcMin();
		keyfileSizer->Layout();
	}

	auto encryptionkeySizer = xrc_call(*this, "ID_ENCRYPTIONKEY_DESC", &wxStaticText::GetContainingSizer);
	if (encryptionkeySizer) {
		encryptionkeySizer->CalcMin();
		encryptionkeySizer->Layout();
	}

	auto serverTypeSizer = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice)->GetContainingSizer();
	if (serverTypeSizer) {
		auto labelSizerItem = serverTypeSizer->GetItemById(XRCID("ID_SERVERTYPE_LABEL_SIZERITEM"));
		auto choiceSizerItem = serverTypeSizer->GetItemById(XRCID("ID_SERVERTYPE_CHOICE_SIZERITEM"));
		if (labelSizerItem && choiceSizerItem) {
			if (CServer::ProtocolHasFeature(protocol, ProtocolFeature::ServerType)) {
				labelSizerItem->Show(true);
				choiceSizerItem->Show(true);
			}
			else {
				labelSizerItem->Show(false);
				choiceSizerItem->Show(false);
			}
			serverTypeSizer->CalcMin();
			serverTypeSizer->Layout();
			xrc_call(*this, "ID_ADVANCED_PANEL", &wxPanel::Layout);
		}
	}

	auto transferModeSizer = XRCCTRL(*this, "ID_TRANSFERMODE_LABEL", wxStaticText)->GetContainingSizer();
	if (transferModeSizer) {
		auto labelSizerItem = transferModeSizer->GetItemById(XRCID("ID_TRANSFERMODE_LABEL_SIZERITEM"));
		auto groupSizerItem = transferModeSizer->GetItemById(XRCID("ID_TRANSFERMODE_GROUP_SIZERITEM"));
		if (labelSizerItem && groupSizerItem) {
			if (CServer::ProtocolHasFeature(protocol, ProtocolFeature::TransferMode)) {
				labelSizerItem->Show(true);
				groupSizerItem->Show(true);
			}
			else {
				labelSizerItem->Show(false);
				groupSizerItem->Show(false);
			}
			transferModeSizer->CalcMin();
			transferModeSizer->Layout();
		}
	}

	if (CServer::ProtocolHasFeature(protocol, ProtocolFeature::Charset)) {
		if (GetPageCount() != m_totalPages) {
			AddPage(m_pCharsetPage, m_charsetPageText);
			wxGetApp().GetWrapEngine()->WrapRecursive(XRCCTRL(*this, "ID_CHARSET_AUTO", wxWindow)->GetParent(), 1.3);
		}
	}
	else {
		if (GetPageCount() == m_totalPages) {
			RemovePage(m_charsetPageIndex);
		}
	}

	GetPage(0)->GetSizer()->Fit(GetPage(0));
}


void CSiteManagerSite::SetLogonTypeCtrlState()
{
	LogonType const t = GetLogonType();
	xrc_call(*this, "ID_USER", &wxTextCtrl::Enable, t != LogonType::anonymous);
	xrc_call(*this, "ID_PASS", &wxTextCtrl::Enable, t == LogonType::normal || t == LogonType::account);
	xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::Enable, t == LogonType::account);
	xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::Enable, t == LogonType::key);
	xrc_call(*this, "ID_KEYFILE_BROWSE", &wxButton::Enable, t == LogonType::key);
	xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::Enable, t == LogonType::normal);
	xrc_call(*this, "ID_ENCRYPTIONKEY_GENERATE", &wxButton::Enable, t == LogonType::normal);
}

LogonType CSiteManagerSite::GetLogonType() const
{
	return GetLogonTypeFromName(xrc_call(*this, "ID_LOGONTYPE", &wxChoice::GetStringSelection).ToStdWstring());
}

bool CSiteManagerSite::Verify(bool predefined)
{
	std::wstring const host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
	if (host.empty()) {
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(_("You have to enter a hostname."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		return false;
	}

	auto logon_type = GetLogonType();

	ServerProtocol protocol = GetProtocol();
	wxASSERT(protocol != UNKNOWN);

	if (protocol == SFTP &&
	        logon_type == LogonType::account)
	{
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetFocus();
		wxMessageBoxEx(_("'Account' logontype not supported by selected protocol"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		return false;
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0 &&
	        !predefined &&
	        (logon_type == LogonType::account || logon_type == LogonType::normal))
	{
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetFocus();
		wxString msg;
		if (COptions::Get()->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) && COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0) {
			msg = _("Saving of password has been disabled by your system administrator.");
		}
		else {
			msg = _("Saving of passwords has been disabled by you.");
		}
		msg += _T("\n");
		msg += _("'Normal' and 'Account' logontypes are not available. Your entry has been changed to 'Ask for password'.");
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(GetNameFromLogonType(LogonType::ask));
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->ChangeValue(wxString());
		logon_type = LogonType::ask;
		wxMessageBoxEx(msg, _("Site Manager - Cannot remember password"), wxICON_INFORMATION, this);
	}

	// Set selected type
	ServerWithCredentials server;
	server.SetLogonType(logon_type);
	server.server.SetProtocol(protocol);

	std::wstring port = xrc_call(*this, "ID_PORT", &wxTextCtrl::GetValue).ToStdWstring();
	CServerPath path;
	std::wstring error;
	if (!server.ParseUrl(host, port, std::wstring(), std::wstring(), error, path, protocol)) {
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(error, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		return false;
	}

	XRCCTRL(*this, "ID_HOST", wxTextCtrl)->ChangeValue(server.Format(ServerFormat::host_only));
	if (server.server.GetPort() != CServer::GetDefaultPort(server.server.GetProtocol())) {
		XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString::Format(_T("%d"), server.server.GetPort()));
	}
	else {
		XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString());
	}

	SetProtocol(server.server.GetProtocol());

	if (XRCCTRL(*this, "ID_CHARSET_CUSTOM", wxRadioButton)->GetValue()) {
		if (XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->GetValue().empty()) {
			XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("Need to specify a character encoding"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	// Require username for non-anonymous, non-ask logon type
	const wxString user = XRCCTRL(*this, "ID_USER", wxTextCtrl)->GetValue();
	if (logon_type != LogonType::anonymous &&
	        logon_type != LogonType::ask &&
	        logon_type != LogonType::interactive &&
	        user.empty())
	{
		XRCCTRL(*this, "ID_USER", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(_("You have to specify a user name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
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
			XRCCTRL(*this, "ID_USER", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("Username cannot be a series of spaces"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	// Require account for account logon type
	if (logon_type == LogonType::account &&
	        XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->GetValue().empty())
	{
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(_("You have to enter an account name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		return false;
	}

	// In key file logon type, check that the provided key file exists
	if (logon_type == LogonType::key) {
		std::wstring keyFile = xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::GetValue).ToStdWstring();
		if (keyFile.empty()) {
			wxMessageBox(_("You have to enter a key file path"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			xrc_call(*this, "ID_KEYFILE", &wxWindow::SetFocus);
			return false;
		}

		// Check (again) that the key file is in the correct format since it might have been introduced manually
		CFZPuttyGenInterface cfzg(this);

		std::wstring keyFileComment, keyFileData;
		if (cfzg.LoadKeyFile(keyFile, false, keyFileComment, keyFileData)) {
			xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::ChangeValue, keyFile);
		}
		else {
			xrc_call(*this, "ID_KEYFILE", &wxWindow::SetFocus);
			return false;
		}
	}

	if (protocol == STORJ && logon_type == LogonType::normal) {
		std::wstring pw = xrc_call(*this, "ID_PASS", &wxTextCtrl::GetValue).ToStdWstring();
		std::wstring encryptionKey = xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::GetValue).ToStdWstring();

		bool encrypted = !xrc_call(*this, "ID_PASS", &wxTextCtrl::GetHint).empty();
		if (encrypted) {
			if (pw.empty() != encryptionKey.empty()) {
				wxMessageBox(_("You cannot change password and encryption key individually if using a master password."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				xrc_call(*this, "ID_ENCRYPTIONKEY", &wxWindow::SetFocus);
				return false;
			}
		}
#if ENABLE_STORJ
		if (!encryptionKey.empty() || !encrypted) {
			CStorjKeyInterface validator(this);
			if (!validator.ValidateKey(encryptionKey, false)) {
				wxMessageBox(_("You have to enter a valid encryption key"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				xrc_call(*this, "ID_ENCRYPTIONKEY", &wxWindow::SetFocus);
				return false;
			}
		}
#endif
	}

	std::wstring const remotePathRaw = XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->GetValue().ToStdWstring();
	if (!remotePathRaw.empty()) {
		std::wstring serverType = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice)->GetStringSelection().ToStdWstring();

		CServerPath remotePath;
		remotePath.SetType(CServer::GetServerTypeFromName(serverType));
		if (!remotePath.SetPath(remotePathRaw)) {
			XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("Default remote path cannot be parsed. Make sure it is a valid absolute path for the selected server type."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	std::wstring const localPath = XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->GetValue().ToStdWstring();
	if (XRCCTRL(*this, "ID_SYNC", wxCheckBox)->GetValue()) {
		if (remotePathRaw.empty() || localPath.empty()) {
			XRCCTRL(*this, "ID_SYNC", wxCheckBox)->SetFocus();
			wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this site."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		if (!(trait.flags_ & ParameterTraits::optional)) {
			auto & controls = *paramIt[trait.section_];
			if (controls.second->GetValue().empty()) {
				controls.second->SetFocus();
				wxMessageBoxEx(_("You need to enter a value."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		++paramIt[trait.section_];
	}

	return true;
}

void CSiteManagerSite::UpdateSite(Site &site)
{
	ServerProtocol const protocol = GetProtocol();
	wxASSERT(protocol != UNKNOWN);
	site.server_.server.SetProtocol(protocol);

	unsigned long port;
	if (!xrc_call(*this, "ID_PORT", &wxTextCtrl::GetValue).ToULong(&port) || !port || port > 65535) {
		port = CServer::GetDefaultPort(protocol);
	}
	std::wstring host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
	// SetHost does not accept URL syntax
	if (!host.empty() && host[0] == '[') {
		host = host.substr(1, host.size() - 2);
	}
	site.server_.server.SetHost(host, port);

	auto logon_type = GetLogonType();
	site.server_.SetLogonType(logon_type);

	site.server_.SetUser(xrc_call(*this, "ID_USER", &wxTextCtrl::GetValue).ToStdWstring());
	auto pw = xrc_call(*this, "ID_PASS", &wxTextCtrl::GetValue).ToStdWstring();

	if (protocol == STORJ && logon_type == LogonType::normal && (!pw.empty() || !site.server_.credentials.encrypted_)) {
		pw += '|';
		pw += xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::GetValue).ToStdWstring();
	}

	if (site.server_.credentials.encrypted_) {
		if (!pw.empty()) {
			site.server_.credentials.encrypted_ = public_key();
			site.server_.credentials.SetPass(pw);
		}
	}
	else {
		site.server_.credentials.SetPass(pw);
	}
	site.server_.credentials.account_ = xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::GetValue).ToStdWstring();

	site.server_.credentials.keyFile_ = xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::GetValue).ToStdWstring();

	site.m_comments = xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::GetValue);
	site.m_colour = CSiteManager::GetColourFromIndex(xrc_call(*this, "ID_COLOR", &wxChoice::GetSelection));

	std::wstring const serverType = xrc_call(*this, "ID_SERVERTYPE", &wxChoice::GetStringSelection).ToStdWstring();
	site.server_.server.SetType(CServer::GetServerTypeFromName(serverType));

	site.m_default_bookmark.m_localDir = xrc_call(*this, "ID_LOCALDIR", &wxTextCtrl::GetValue);
	site.m_default_bookmark.m_remoteDir = CServerPath();
	site.m_default_bookmark.m_remoteDir.SetType(site.server_.server.GetType());
	site.m_default_bookmark.m_remoteDir.SetPath(xrc_call(*this, "ID_REMOTEDIR", &wxTextCtrl::GetValue).ToStdWstring());
	site.m_default_bookmark.m_sync = xrc_call(*this, "ID_SYNC", &wxCheckBox::GetValue);
	site.m_default_bookmark.m_comparison = xrc_call(*this, "ID_COMPARISON", &wxCheckBox::GetValue);

	int hours = xrc_call(*this, "ID_TIMEZONE_HOURS", &wxSpinCtrl::GetValue);
	int minutes = xrc_call(*this, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::GetValue);

	site.server_.server.SetTimezoneOffset(hours * 60 + minutes);

	if (xrc_call(*this, "ID_TRANSFERMODE_ACTIVE", &wxRadioButton::GetValue)) {
		site.server_.server.SetPasvMode(MODE_ACTIVE);
	}
	else if (xrc_call(*this, "ID_TRANSFERMODE_PASSIVE", &wxRadioButton::GetValue)) {
		site.server_.server.SetPasvMode(MODE_PASSIVE);
	}
	else {
		site.server_.server.SetPasvMode(MODE_DEFAULT);
	}

	if (xrc_call(*this, "ID_LIMITMULTIPLE", &wxCheckBox::GetValue)) {
		site.server_.server.MaximumMultipleConnections(xrc_call(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::GetValue));
	}
	else {
		site.server_.server.MaximumMultipleConnections(0);
	}

	if (xrc_call(*this, "ID_CHARSET_UTF8", &wxRadioButton::GetValue))
		site.server_.server.SetEncodingType(ENCODING_UTF8);
	else if (xrc_call(*this, "ID_CHARSET_CUSTOM", &wxRadioButton::GetValue)) {
		std::wstring encoding = xrc_call(*this, "ID_ENCODING", &wxTextCtrl::GetValue).ToStdWstring();
		site.server_.server.SetEncodingType(ENCODING_CUSTOM, encoding);
	}
	else {
		site.server_.server.SetEncodingType(ENCODING_AUTO);
	}

	if (xrc_call(*this, "ID_BYPASSPROXY", &wxCheckBox::GetValue)) {
		site.server_.server.SetBypassProxy(true);
	}
	else {
		site.server_.server.SetBypassProxy(false);
	}

	UpdateExtraParameters(site.server_.server);
}

void CSiteManagerSite::UpdateExtraParameters(CServer & server)
{
	server.ClearExtraParameters();
	
	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}
	auto const& traits = ExtraServerParameterTraits(server.GetProtocol());
	for (auto const& trait : traits) {
		if (trait.section_ == ParameterSection::credentials) {
			continue;
		}

		server.SetExtraParameter(trait.name_, paramIt[trait.section_]->second->GetValue().ToStdWstring());
		++paramIt[trait.section_];
	}
}

void CSiteManagerSite::SetSite(Site const& site, bool predefined)
{
	if (!site) {
		// Empty all site information
		xrc_call(*this, "ID_HOST", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_PORT", &wxTextCtrl::ChangeValue, wxString());
		SetProtocol(FTP);
		xrc_call(*this, "ID_BYPASSPROXY", &wxCheckBox::SetValue, false);
		xrc_call(*this, "ID_LOGONTYPE", &wxChoice::SetStringSelection, _("Anonymous"));
		xrc_call(*this, "ID_USER", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_PASS", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_PASS", &wxTextCtrl::SetHint, wxString());
		xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_COLOR", &wxChoice::Select, 0);

		SetControlVisibility(FTP, LogonType::anonymous);

		xrc_call(*this, "ID_SERVERTYPE", &wxChoice::SetSelection, 0);
		xrc_call(*this, "ID_LOCALDIR", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_REMOTEDIR", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_SYNC", &wxCheckBox::SetValue, false);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_HOURS", &wxSpinCtrl::SetValue, 0);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::SetValue, 0);

		xrc_call(*this, "ID_TRANSFERMODE_DEFAULT", &wxRadioButton::SetValue, true);
		xrc_call(*this, "ID_LIMITMULTIPLE", &wxCheckBox::SetValue, false);
		xrc_call<wxSpinCtrl, int>(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, 1);

		xrc_call(*this, "ID_CHARSET_AUTO", &wxRadioButton::SetValue, true);
		xrc_call(*this, "ID_ENCODING", &wxTextCtrl::ChangeValue, wxString());
	}
	else {
		xrc_call(*this, "ID_HOST", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_HOST", &wxTextCtrl::ChangeValue, site.server_.Format(ServerFormat::host_only));
		unsigned int port = site.server_.server.GetPort();

		if (port != CServer::GetDefaultPort(site.server_.server.GetProtocol())) {
			xrc_call(*this, "ID_PORT", &wxTextCtrl::ChangeValue, wxString::Format(_T("%d"), port));
		}
		else {
			xrc_call(*this, "ID_PORT", &wxTextCtrl::ChangeValue, wxString());
		}
		xrc_call(*this, "ID_PORT", &wxWindow::Enable, !predefined);

		ServerProtocol protocol = site.server_.server.GetProtocol();
		SetProtocol(protocol);
		xrc_call(*this, "ID_PROTOCOL", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_ENCRYPTION", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_BYPASSPROXY", &wxCheckBox::SetValue, site.server_.server.GetBypassProxy());

		LogonType logonType = site.server_.credentials.logonType_;
		xrc_call(*this, "ID_USER", &wxTextCtrl::Enable, !predefined && logonType != LogonType::anonymous);
		xrc_call(*this, "ID_PASS", &wxTextCtrl::Enable, !predefined && (logonType == LogonType::normal || logonType == LogonType::account));
		xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::Enable, !predefined && logonType == LogonType::account);
		xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::Enable, !predefined && logonType == LogonType::key);
		xrc_call(*this, "ID_KEYFILE_BROWSE", &wxButton::Enable, !predefined && logonType == LogonType::key);
		xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::Enable, !predefined && logonType == LogonType::normal);
		xrc_call(*this, "ID_ENCRYPTIONKEY_GENERATE", &wxButton::Enable, !predefined && logonType == LogonType::normal);

		SetControlVisibility(protocol, logonType);

		xrc_call(*this, "ID_LOGONTYPE", &wxChoice::SetStringSelection, GetNameFromLogonType(logonType));
		xrc_call(*this, "ID_LOGONTYPE", &wxWindow::Enable, !predefined);

		xrc_call(*this, "ID_USER", &wxTextCtrl::ChangeValue, site.server_.server.GetUser());
		xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::ChangeValue, site.server_.credentials.account_);

		std::wstring pass = site.server_.credentials.GetPass();
		std::wstring encryptionKey;
		if (protocol == STORJ) {
			size_t pos = pass.rfind('|');
			if (pos != std::wstring::npos) {
				encryptionKey = pass.substr(pos + 1);
				pass = pass.substr(0, pos);
			}
		}

		if (site.server_.credentials.encrypted_) {
			xrc_call(*this, "ID_PASS", &wxTextCtrl::ChangeValue, wxString());
			xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, wxString());

			// @translator: Keep this string as short as possible
			xrc_call(*this, "ID_PASS", &wxTextCtrl::SetHint, _("Leave empty to keep existing password."));
			for (auto & control : extraParameters_[ParameterSection::credentials]) {
				control.second->SetHint(_("Leave empty to keep existing data."));
			}
		}
		else {
			xrc_call(*this, "ID_PASS", &wxTextCtrl::ChangeValue, pass);
			xrc_call(*this, "ID_PASS", &wxTextCtrl::SetHint, wxString());
			xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, encryptionKey);

			auto it = extraParameters_[ParameterSection::credentials].begin();

			auto const& traits = ExtraServerParameterTraits(protocol);
			for (auto const& trait : traits) {
				if (trait.section_ != ParameterSection::credentials) {
					continue;
				}

				it->second->ChangeValue(site.server_.credentials.GetExtraParameter(trait.name_));
				++it;
			}
		}

		SetExtraParameters(site.server_.server);

		xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::ChangeValue, site.server_.credentials.keyFile_);
		xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::ChangeValue, site.m_comments);
		xrc_call(*this, "ID_COMMENTS", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_COLOR", &wxChoice::Select, CSiteManager::GetColourIndex(site.m_colour));
		xrc_call(*this, "ID_COLOR", &wxWindow::Enable, !predefined);

		xrc_call(*this, "ID_SERVERTYPE", &wxChoice::SetSelection, site.server_.server.GetType());
		xrc_call(*this, "ID_SERVERTYPE", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_LOCALDIR", &wxTextCtrl::ChangeValue, site.m_default_bookmark.m_localDir);
		xrc_call(*this, "ID_LOCALDIR", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_REMOTEDIR", &wxTextCtrl::ChangeValue, site.m_default_bookmark.m_remoteDir.GetPath());
		xrc_call(*this, "ID_REMOTEDIR", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_SYNC", &wxCheckBox::Enable, !predefined);
		xrc_call(*this, "ID_SYNC", &wxCheckBox::SetValue, site.m_default_bookmark.m_sync);
		xrc_call(*this, "ID_COMPARISON", &wxCheckBox::Enable, !predefined);
		xrc_call(*this, "ID_COMPARISON", &wxCheckBox::SetValue, site.m_default_bookmark.m_comparison);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_HOURS", &wxSpinCtrl::SetValue, site.server_.server.GetTimezoneOffset() / 60);
		xrc_call(*this, "ID_TIMEZONE_HOURS", &wxWindow::Enable, !predefined);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::SetValue, site.server_.server.GetTimezoneOffset() % 60);
		xrc_call(*this, "ID_TIMEZONE_MINUTES", &wxWindow::Enable, !predefined);

		if (CServer::ProtocolHasFeature(site.server_.server.GetProtocol(), ProtocolFeature::TransferMode)) {
			PasvMode pasvMode = site.server_.server.GetPasvMode();
			if (pasvMode == MODE_ACTIVE) {
				xrc_call(*this, "ID_TRANSFERMODE_ACTIVE", &wxRadioButton::SetValue, true);
			}
			else if (pasvMode == MODE_PASSIVE) {
				xrc_call(*this, "ID_TRANSFERMODE_PASSIVE", &wxRadioButton::SetValue, true);
			}
			else {
				xrc_call(*this, "ID_TRANSFERMODE_DEFAULT", &wxRadioButton::SetValue, true);
			}
			xrc_call(*this, "ID_TRANSFERMODE_ACTIVE", &wxWindow::Enable, !predefined);
			xrc_call(*this, "ID_TRANSFERMODE_PASSIVE", &wxWindow::Enable, !predefined);
			xrc_call(*this, "ID_TRANSFERMODE_DEFAULT", &wxWindow::Enable, !predefined);
		}

		int maxMultiple = site.server_.server.MaximumMultipleConnections();
		xrc_call(*this, "ID_LIMITMULTIPLE", &wxCheckBox::SetValue, maxMultiple != 0);
		xrc_call(*this, "ID_LIMITMULTIPLE", &wxWindow::Enable, !predefined);
		if (maxMultiple != 0) {
			xrc_call(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, !predefined);
			xrc_call<wxSpinCtrl, int>(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, maxMultiple);
		}
		else {
			xrc_call(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, false);
			xrc_call<wxSpinCtrl, int>(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, 1);
		}

		switch (site.server_.server.GetEncodingType()) {
		default:
		case ENCODING_AUTO:
			xrc_call(*this, "ID_CHARSET_AUTO", &wxRadioButton::SetValue, true);
			break;
		case ENCODING_UTF8:
			xrc_call(*this, "ID_CHARSET_UTF8", &wxRadioButton::SetValue, true);
			break;
		case ENCODING_CUSTOM:
			xrc_call(*this, "ID_CHARSET_CUSTOM", &wxRadioButton::SetValue, true);
			break;
		}
		xrc_call(*this, "ID_CHARSET_AUTO", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_CHARSET_UTF8", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_CHARSET_CUSTOM", &wxWindow::Enable, !predefined);
		xrc_call(*this, "ID_ENCODING", &wxTextCtrl::Enable, !predefined && site.server_.server.GetEncodingType() == ENCODING_CUSTOM);
		xrc_call(*this, "ID_ENCODING", &wxTextCtrl::ChangeValue, site.server_.server.GetCustomEncoding());
	}
}

void CSiteManagerSite::SetExtraParameters(CServer const& server)
{
	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}
	auto const& traits = ExtraServerParameterTraits(server.GetProtocol());
	for (auto const& trait : traits) {
		if (trait.section_ == ParameterSection::credentials) {
			continue;
		}

		std::wstring value = server.GetExtraParameter(trait.name_);
		paramIt[trait.section_]->second->ChangeValue(value.empty() ? trait.default_ : value);
		++paramIt[trait.section_];
	}
}

void CSiteManagerSite::OnProtocolSelChanged(wxCommandEvent&)
{
	auto const protocol = GetProtocol();
	UpdateHostFromDefaults(protocol);

	CServer server;
	if (previousProtocol_ != UNKNOWN) {
		server.SetProtocol(previousProtocol_);
		UpdateExtraParameters(server);
	}
	server.SetProtocol(protocol);
	SetExtraParameters(server);

	auto const logonType = GetLogonType();
	SetControlVisibility(protocol, logonType);

	previousProtocol_ = protocol;
}

void CSiteManagerSite::OnLogontypeSelChanged(wxCommandEvent&)
{
	LogonType const t = GetLogonType();
	SetControlVisibility(GetProtocol(), t);

	SetLogonTypeCtrlState();
}

void CSiteManagerSite::OnCharsetChange(wxCommandEvent&)
{
	bool checked = xrc_call(*this, "ID_CHARSET_CUSTOM", &wxRadioButton::GetValue);
	xrc_call(*this, "ID_ENCODING", &wxTextCtrl::Enable, checked);
}

void CSiteManagerSite::OnLimitMultipleConnectionsChanged(wxCommandEvent& event)
{
	XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->Enable(event.IsChecked());
}

void CSiteManagerSite::OnRemoteDirBrowse(wxCommandEvent&)
{
	wxDirDialog dlg(this, _("Choose the default local directory"), XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK) {
		XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->ChangeValue(dlg.GetPath());
	}
}

void CSiteManagerSite::OnKeyFileBrowse(wxCommandEvent&)
{
	wxString wildcards(_T("PPK files|*.ppk|PEM files|*.pem|All files|*.*"));
	wxFileDialog dlg(this, _("Choose a key file"), wxString(), wxString(), wildcards, wxFD_OPEN|wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() == wxID_OK) {
		std::wstring keyFilePath = dlg.GetPath().ToStdWstring();
		// If the selected file was a PEM file, LoadKeyFile() will automatically convert it to PPK
		// and tell us the new location.
		CFZPuttyGenInterface fzpg(this);

		std::wstring keyFileComment, keyFileData;
		if (fzpg.LoadKeyFile(keyFilePath, false, keyFileComment, keyFileData)) {
			XRCCTRL(*this, "ID_KEYFILE", wxTextCtrl)->ChangeValue(keyFilePath);
#if USE_MAC_SANDBOX
			OSXSandboxUserdirs::Get().AddFile(keyFilePath);
#endif

		}
		else {
			xrc_call(*this, "ID_KEYFILE", &wxWindow::SetFocus);
		}
	}
}

void CSiteManagerSite::OnGenerateEncryptionKey(wxCommandEvent&)
{
#if ENABLE_STORJ
	CStorjKeyInterface generator(this);
	std::wstring key = generator.GenerateKey();
	if (!key.empty()) {
		xrc_call(*this, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, wxString(key));
		xrc_call(*this, "ID_ENCRYPTIONKEY", &wxWindow::SetFocus);

		wxDialogEx dlg;
		if (dlg.Load(this, "ID_STORJ_GENERATED_KEY")) {
			dlg.WrapRecursive(&dlg, 2.5);
			dlg.GetSizer()->Fit(&dlg);
			dlg.GetSizer()->SetSizeHints(&dlg);
			xrc_call(dlg, "ID_KEY", &wxTextCtrl::ChangeValue, wxString(key));
			dlg.ShowModal();
		}
	}
#endif
}

void CSiteManagerSite::UpdateHostFromDefaults(ServerProtocol const protocol)
{
	if (protocol != previousProtocol_) {
		auto const oldDefault = std::get<0>(GetDefaultHost(previousProtocol_));
		auto const newDefault = GetDefaultHost(protocol);

		std::wstring const host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
		if (host.empty() || host == oldDefault) {
			xrc_call(*this, "ID_HOST", &wxTextCtrl::ChangeValue, std::get<0>(newDefault));
		}
		xrc_call(*this, "ID_HOST", &wxTextCtrl::SetHint, std::get<1>(newDefault));
	}
}
