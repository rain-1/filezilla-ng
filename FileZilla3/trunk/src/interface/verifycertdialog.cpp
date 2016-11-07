#include <filezilla.h>
#include "filezillaapp.h"
#include "verifycertdialog.h"
#include "dialogex.h"
#include "ipcmutex.h"
#include "Options.h"
#include "timeformatting.h"
#include "xrc_helper.h"

#include <wx/scrolwin.h>
#include <wx/statbox.h>
#include <wx/tokenzr.h>

CVerifyCertDialog::CVerifyCertDialog()
	: m_xmlFile(wxGetApp().GetSettingsFile(_T("trustedcerts")))
{
}

bool CVerifyCertDialog::DisplayCert(wxDialogEx* pDlg, const CCertificate& cert)
{
	bool warning = false;
	if (!cert.GetActivationTime().empty()) {
		if (cert.GetActivationTime() > fz::datetime::now()) {
			pDlg->SetChildLabel(XRCID("ID_ACTIVATION_TIME"), wxString::Format(_("%s - Not yet valid!"), CTimeFormat::Format(cert.GetActivationTime())));
			xrc_call(*pDlg, "ID_ACTIVATION_TIME", &wxWindow::SetForegroundColour, wxColour(255, 0, 0));
			warning = true;
		}
		else {
			pDlg->SetChildLabel(XRCID("ID_ACTIVATION_TIME"), CTimeFormat::Format(cert.GetActivationTime()));
		}
	}
	else {
		warning = true;
		pDlg->SetChildLabel(XRCID("ID_ACTIVATION_TIME"), _("Invalid date"));
	}

	if (!cert.GetExpirationTime().empty()) {
		if (cert.GetExpirationTime() < fz::datetime::now()) {
			pDlg->SetChildLabel(XRCID("ID_EXPIRATION_TIME"), wxString::Format(_("%s - Certificate expired!"), CTimeFormat::Format(cert.GetExpirationTime())));
			xrc_call(*pDlg, "ID_EXPIRATION_TIME", &wxWindow::SetForegroundColour, wxColour(255, 0, 0));
			warning = true;
		}
		else {
			pDlg->SetChildLabel(XRCID("ID_EXPIRATION_TIME"), CTimeFormat::Format(cert.GetExpirationTime()));
		}
	}
	else {
		warning = true;
		pDlg->SetChildLabel(XRCID("ID_EXPIRATION_TIME"), _("Invalid date"));
	}

	if (!cert.GetSerial().empty()) {
		pDlg->SetChildLabel(XRCID("ID_SERIAL"), cert.GetSerial());
	}
	else {
		pDlg->SetChildLabel(XRCID("ID_SERIAL"), _("None"));
	}

	pDlg->SetChildLabel(XRCID("ID_PKALGO"), wxString::Format(_("%s with %d bits"), cert.GetPkAlgoName(), cert.GetPkAlgoBits()));
	pDlg->SetChildLabel(XRCID("ID_SIGNALGO"), cert.GetSignatureAlgorithm());

	wxString const& sha256 = cert.GetFingerPrintSHA256();
	pDlg->SetChildLabel(XRCID("ID_FINGERPRINT_SHA256"), sha256.Left(sha256.size() / 2 + 1) + _T("\n") + sha256.Mid(sha256.size() / 2 + 1));
	pDlg->SetChildLabel(XRCID("ID_FINGERPRINT_SHA1"), cert.GetFingerPrintSHA1());

	ParseDN(XRCCTRL(*pDlg, "ID_ISSUER_BOX", wxStaticBox), cert.GetIssuer(), m_pIssuerSizer);

	auto subjectPanel = XRCCTRL(*pDlg, "ID_SUBJECT_PANEL", wxScrolledWindow);
	subjectPanel->Freeze();

	ParseDN(subjectPanel, cert.GetSubject(), m_pSubjectSizer);

	auto const& altNames = cert.GetAltSubjectNames();
	if (!altNames.empty()) {
		wxString str;
		for (auto const& altName : altNames) {
			str += altName + _T("\n");
		}
		str.RemoveLast();
		m_pSubjectSizer->Add(new wxStaticText(subjectPanel, wxID_ANY, wxPLURAL("Alternative name:", "Alternative names:", altNames.size())));
		m_pSubjectSizer->Add(new wxStaticText(subjectPanel, wxID_ANY, str));
	}
	m_pSubjectSizer->Fit(subjectPanel);

	wxSize min = m_pSubjectSizer->CalcMin();
	int const maxHeight = (line_height_ + m_pDlg->ConvertDialogToPixels(wxPoint(0, 1)).y) * 15;
	if (min.y >= maxHeight) {
		min.y = maxHeight;
		min.x += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
	}

	// Add extra safety margin to prevent squishing on OS X.
	min.x += 2;

	subjectPanel->SetMinSize(min);
	subjectPanel->Thaw();

	return warning;
}

#include <wx/scrolwin.h>

bool CVerifyCertDialog::DisplayAlgorithm(int controlId, wxString name, bool insecure)
{
	if (insecure) {
		name += _T(" - ");
		name += _("Insecure algorithm!");

		auto wnd = m_pDlg->FindWindow(controlId);
		if (wnd) {
			wnd->SetForegroundColour(wxColour(255, 0, 0));
		}
	}

	m_pDlg->SetChildLabel(controlId, name);

	return insecure;
}

void CVerifyCertDialog::ShowVerificationDialog(CCertificateNotification& notification, bool displayOnly /*=false*/)
{
	LoadTrustedCerts();

	m_pDlg = new wxDialogEx;
	if (!m_pDlg->Load(0, _T("ID_VERIFYCERT"))) {
		wxBell();
		delete m_pDlg;
		m_pDlg = 0;
		return;
	}

	if (displayOnly) {
		xrc_call(*m_pDlg, "ID_DESC", &wxWindow::Hide);
		xrc_call(*m_pDlg, "ID_ALWAYS_DESC", &wxWindow::Hide);
		xrc_call(*m_pDlg, "ID_ALWAYS", &wxWindow::Hide);
		xrc_call(*m_pDlg, "wxID_CANCEL", &wxWindow::Hide);
		m_pDlg->SetTitle(_T("Certificate details"));
	}
	else {
		m_pDlg->WrapText(m_pDlg, XRCID("ID_DESC"), 400);

		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
			XRCCTRL(*m_pDlg, "ID_ALWAYS", wxCheckBox)->Hide();
		}
	}

	m_certificates = notification.GetCertificates();
	if (m_certificates.size() == 1) {
		XRCCTRL(*m_pDlg, "ID_CHAIN_DESC", wxStaticText)->Hide();
		XRCCTRL(*m_pDlg, "ID_CHAIN", wxChoice)->Hide();
	}
	else {
		wxChoice* pChoice = XRCCTRL(*m_pDlg, "ID_CHAIN", wxChoice);
		for (unsigned int i = 0; i < m_certificates.size(); ++i) {
			pChoice->Append(wxString::Format(_T("%d"), i));
		}
		pChoice->SetSelection(0);

		pChoice->Connect(wxEVT_COMMAND_CHOICE_SELECTED, wxCommandEventHandler(CVerifyCertDialog::OnCertificateChoice), 0, this);
	}

	m_pDlg->SetChildLabel(XRCID("ID_HOST"), wxString::Format(_T("%s:%d"), notification.GetHost(), notification.GetPort()));

	line_height_ = XRCCTRL(*m_pDlg, "ID_SUBJECT_DUMMY", wxStaticText)->GetSize().y;

	m_pSubjectSizer = XRCCTRL(*m_pDlg, "ID_SUBJECT_DUMMY", wxStaticText)->GetContainingSizer();
	m_pSubjectSizer->Clear(true);

	m_pIssuerSizer = XRCCTRL(*m_pDlg, "ID_ISSUER_DUMMY", wxStaticText)->GetContainingSizer();
	m_pIssuerSizer->Clear(true);

	wxSize minSize(0, 0);
	for (unsigned int i = 0; i < m_certificates.size(); ++i) {
		DisplayCert(m_pDlg, m_certificates[i]);
		m_pDlg->Layout();
		m_pDlg->GetSizer()->Fit(m_pDlg);
		minSize.IncTo(m_pDlg->GetSizer()->GetMinSize());
	}
	m_pDlg->GetSizer()->SetMinSize(minSize);

	bool warning = DisplayCert(m_pDlg, m_certificates[0]);

	DisplayAlgorithm(XRCID("ID_PROTOCOL"), notification.GetProtocol(), (notification.GetAlgorithmWarnings() & CCertificateNotification::tlsver) != 0);
	DisplayAlgorithm(XRCID("ID_KEYEXCHANGE"), notification.GetKeyExchange(), (notification.GetAlgorithmWarnings() & CCertificateNotification::kex) != 0);
	DisplayAlgorithm(XRCID("ID_CIPHER"), notification.GetSessionCipher(), (notification.GetAlgorithmWarnings() & CCertificateNotification::cipher) != 0);
	DisplayAlgorithm(XRCID("ID_MAC"), notification.GetSessionMac(), (notification.GetAlgorithmWarnings() & CCertificateNotification::mac) != 0);

	if (notification.GetAlgorithmWarnings() != 0) {
		warning = true;
	}

	if (warning) {
		XRCCTRL(*m_pDlg, "ID_IMAGE", wxStaticBitmap)->SetBitmap(wxArtProvider::GetBitmap(wxART_WARNING));
		XRCCTRL(*m_pDlg, "ID_ALWAYS", wxCheckBox)->Enable(false);
	}

	m_pDlg->GetSizer()->Fit(m_pDlg);
	m_pDlg->GetSizer()->SetSizeHints(m_pDlg);

	int res = m_pDlg->ShowModal();

	if (!displayOnly) {
		if (res == wxID_OK) {
			wxASSERT(!IsTrusted(notification));

			notification.m_trusted = true;

			if (!notification.GetAlgorithmWarnings()) {
				if (!warning && XRCCTRL(*m_pDlg, "ID_ALWAYS", wxCheckBox)->GetValue()) {
					SetPermanentlyTrusted(notification);
				}
				else {
					t_certData cert;
					cert.host = notification.GetHost();
					cert.port = notification.GetPort();
					cert.data = m_certificates[0].GetRawData();
					m_sessionTrustedCerts.emplace_back(std::move(cert));
				}
			}
		}
		else {
			notification.m_trusted = false;
		}
	}

	delete m_pDlg;
	m_pDlg = 0;
}

void CVerifyCertDialog::ParseDN(wxWindow* parent, const wxString& dn, wxSizer* pSizer)
{
	pSizer->Clear(true);

	wxStringTokenizer tokens(dn, _T(","));

	std::list<wxString> tokenlist;
	while (tokens.HasMoreTokens()) {
		tokenlist.push_back(tokens.GetNextToken());
	}

	ParseDN_by_prefix(parent, tokenlist, _T("CN"), _("Common name:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("O"), _("Organization:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("2.5.4.15"), _("Business category:"), pSizer, true);
	ParseDN_by_prefix(parent, tokenlist, _T("OU"), _("Unit:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("T"), _("Title:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("C"), _("Country:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("ST"), _("State or province:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("L"), _("Locality:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("2.5.4.17"), _("Postal code:"), pSizer, true);
	ParseDN_by_prefix(parent, tokenlist, _T("postalCode"), _("Postal code:"), pSizer, true);
	ParseDN_by_prefix(parent, tokenlist, _T("STREET"), _("Street:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("EMAIL"), _("E-Mail:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("serialNumber"), _("Serial number:"), pSizer);
	ParseDN_by_prefix(parent, tokenlist, _T("1.3.6.1.4.1.311.60.2.1.3"), _("Jurisdiction country:"), pSizer, true);
	ParseDN_by_prefix(parent, tokenlist, _T("1.3.6.1.4.1.311.60.2.1.2"), _("Jurisdiction state or province:"), pSizer, true);
	ParseDN_by_prefix(parent, tokenlist, _T("1.3.6.1.4.1.311.60.2.1.1"), _("Jurisdiction locality:"), pSizer, true);

	if (!tokenlist.empty()) {
		wxString value = tokenlist.front();
		for (auto iter = ++tokenlist.cbegin(); iter != tokenlist.cend(); ++iter) {
			value += _T(",") + *iter;
		}

		pSizer->Add(new wxStaticText(parent, wxID_ANY, _("Other:")));
		pSizer->Add(new wxStaticText(parent, wxID_ANY, value));
	}
}

void CVerifyCertDialog::ParseDN_by_prefix(wxWindow* parent, std::list<wxString>& tokens, wxString prefix, const wxString& name, wxSizer* pSizer, bool decode /*=false*/)
{
	prefix += _T("=");
	int len = prefix.Length();

	wxString value;

	bool append = false;

	auto iter = tokens.begin();
	while (iter != tokens.end()) {
		if (!append) {
			if (iter->Left(len) != prefix) {
				++iter;
				continue;
			}

			if (!value.empty()) {
				value += _T("\n");
			}
		}
		else {
			append = false;
			value += _T(",");
		}

		value += iter->Mid(len);

		if (iter->Last() == '\\') {
			value.RemoveLast();
			append = true;
			len = 0;
		}

		auto remove = iter++;
		tokens.erase(remove);
	}

	if (decode) {
		value = DecodeValue(value);
	}

	if (!value.empty()) {
		pSizer->Add(new wxStaticText(parent, wxID_ANY, name));
		pSizer->Add(new wxStaticText(parent, wxID_ANY, value));
	}
}

bool CVerifyCertDialog::IsTrusted(CCertificateNotification const& notification)
{
	if (notification.GetAlgorithmWarnings() != 0) {
		// These certs are never trusted.
		return false;
	}

	LoadTrustedCerts();

	CCertificate cert = notification.GetCertificates()[0];

	return IsTrusted(notification.GetHost(), notification.GetPort(), cert.GetRawData(), false);
}

bool CVerifyCertDialog::DoIsTrusted(std::wstring const& host, unsigned int port, std::vector<uint8_t> const& data, std::list<CVerifyCertDialog::t_certData> const& trustedCerts)
{
	if (!data.size()) {
		return false;
	}

	for (auto const& cert : trustedCerts) {
		if (host != cert.host) {
			continue;
		}

		if (port != cert.port) {
			continue;
		}

		if (cert.data == data) {
			return true;
		}
	}

	return false;
}

bool CVerifyCertDialog::IsTrusted(std::wstring const& host, unsigned int port, std::vector<uint8_t> const& data, bool permanentOnly)
{
	bool trusted = DoIsTrusted(host, port, data, m_trustedCerts);
	if (!trusted && !permanentOnly) {
		trusted = DoIsTrusted(host, port, data, m_sessionTrustedCerts);
	}

	return trusted;
}

void CVerifyCertDialog::LoadTrustedCerts()
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);
	if (!m_xmlFile.Modified()) {
		return;
	}

	auto element = m_xmlFile.Load();
	if (!element) {
		return;
	}

	m_trustedCerts.clear();

	if (!(element = element.child("TrustedCerts"))) {
		return;
	}

	bool modified = false;

	auto cert = element.child("Certificate");
	while (cert) {
		std::wstring value = GetTextElement(cert, "Data");

		pugi::xml_node remove;

		t_certData data;
		data.data = fz::hex_decode(value);
		if (data.data.empty()) {
			remove = cert;
		}

		data.host = GetTextElement(cert, "Host");
		data.port = GetTextElementInt(cert, "Port");
		if (data.host.empty() || data.port < 1 || data.port > 65535) {
			remove = cert;
		}

		fz::datetime const now = fz::datetime::now();
		int64_t activationTime = GetTextElementInt(cert, "ActivationTime", 0);
		if (activationTime == 0 || activationTime > now.get_time_t()) {
			remove = cert;
		}

		int64_t expirationTime = GetTextElementInt(cert, "ExpirationTime", 0);
		if (expirationTime == 0 || expirationTime < now.get_time_t()) {
			remove = cert;
		}

		// Weed out duplicates
		if (IsTrusted(data.host, data.port, data.data, true)) {
			remove = cert;
		}

		if (!remove) {
			m_trustedCerts.emplace_back(std::move(data));
		}

		cert = cert.next_sibling("Certificate");

		if (remove) {
			modified = true;
			element.remove_child(remove);
		}
	}

	if (modified) {
		m_xmlFile.Save(false);
	}
}

void CVerifyCertDialog::SetPermanentlyTrusted(CCertificateNotification const& notification)
{
	const CCertificate certificate = notification.GetCertificates()[0];

	t_certData cert;
	cert.host = notification.GetHost();
	cert.port = notification.GetPort();
	cert.data = certificate.GetRawData();

	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);
	LoadTrustedCerts();

	if (IsTrusted(cert.host, cert.port, cert.data, true))	{
		return;
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 2) {
		auto element = m_xmlFile.GetElement();
		if (element) {
			auto certs = element.child("TrustedCerts");
			if (!certs) {
				certs = element.append_child("TrustedCerts");
			}

			auto xCert = certs.append_child("Certificate");
			AddTextElementUtf8(xCert, "Data", fz::hex_encode<std::string>(cert.data));
			AddTextElement(xCert, "ActivationTime", static_cast<int64_t>(certificate.GetActivationTime().get_time_t()));
			AddTextElement(xCert, "ExpirationTime", static_cast<int64_t>(certificate.GetExpirationTime().get_time_t()));
			AddTextElement(xCert, "Host", cert.host);
			AddTextElement(xCert, "Port", cert.port);

			m_xmlFile.Save(true);
		}
	}
	m_trustedCerts.emplace_back(std::move(cert));
}

wxString CVerifyCertDialog::DecodeValue(const wxString& value)
{
	// Decodes string in hex notation
	// #xxxx466F6F626172 -> Foobar
	// First two encoded bytes are ignored, some weird type information I don't care about
	// Only accepts ASCII for now.
	if (value.empty() || value[0] != '#') {
		return value;
	}

	unsigned int len = value.Len();

	wxString out;

	for (unsigned int i = 5; i + 1 < len; i += 2) {
		wxChar c = value[i];
		wxChar d = value[i + 1];
		if (c >= '0' && c <= '9') {
			c -= '0';
		}
		else if (c >= 'a' && c <= 'z') {
			c -= 'a' - 10;
		}
		else if (c >= 'A' && c <= 'Z') {
			c -= 'A' - 10;
		}
		else {
			continue;
		}
		if (d >= '0' && d <= '9') {
			d -= '0';
		}
		else if (d >= 'a' && d <= 'z') {
			d -= 'a' - 10;
		}
		else if (d >= 'A' && d <= 'Z') {
			d -= 'A' - 10;
		}
		else {
			continue;
		}

		c = c * 16 + d;
		if (c > 127 || c < 32) {
			continue;
		}
		out += c;
	}

	return out;
}

void CVerifyCertDialog::OnCertificateChoice(wxCommandEvent& event)
{
	int sel = event.GetSelection();
	if (sel < 0 || static_cast<unsigned int>(sel) > m_certificates.size()) {
		return;
	}
	DisplayCert(m_pDlg, m_certificates[sel]);

	m_pDlg->Layout();
	m_pDlg->GetSizer()->Fit(m_pDlg);
	m_pDlg->Refresh();
}
