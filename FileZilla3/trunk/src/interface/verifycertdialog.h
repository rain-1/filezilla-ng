#ifndef __VERIFYCERTDIALOG_H__
#define __VERIFYCERTDIALOG_H__

#include "xmlfunctions.h"

class wxDialogEx;
class CVerifyCertDialog final : protected wxEvtHandler
{
public:
	CVerifyCertDialog();

	bool IsTrusted(CCertificateNotification const& notification);
	void ShowVerificationDialog(CCertificateNotification& notification, bool displayOnly = false);

private:
	struct t_certData {
		std::wstring host;
		unsigned int port{};
		std::vector<uint8_t> data;
	};

	bool IsTrusted(std::wstring const& host, unsigned int port, std::vector<uint8_t> const& data, bool permanentOnly);
	bool DoIsTrusted(std::wstring const& host, unsigned int port, std::vector<uint8_t> const& data, std::list<t_certData> const& trustedCerts);

	bool DisplayAlgorithm(int controlId, wxString name, bool insecure);

	bool DisplayCert(wxDialogEx* pDlg, const CCertificate& cert);

	void ParseDN(wxWindow* parent, const wxString& dn, wxSizer* pSizer);
	void ParseDN_by_prefix(wxWindow* parent, std::list<wxString>& tokens, wxString prefix, const wxString& name, wxSizer* pSizer, bool decode = false);

	wxString DecodeValue(const wxString& value);

	void SetPermanentlyTrusted(CCertificateNotification const& notification);

	void LoadTrustedCerts();

	std::list<t_certData> m_trustedCerts;
	std::list<t_certData> m_sessionTrustedCerts;

	CXmlFile m_xmlFile;

	std::vector<CCertificate> m_certificates;
	wxDialogEx* m_pDlg{};
	wxSizer* m_pSubjectSizer{};
	wxSizer* m_pIssuerSizer{};
	int line_height_{};

	void OnCertificateChoice(wxCommandEvent& event);
};

#endif //__VERIFYCERTDIALOG_H__
