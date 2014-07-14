#include <filezilla.h>
#include "verifyhostkeydialog.h"
#include <wx/tokenzr.h>
#include "dialogex.h"
#include "ipcmutex.h"

std::list<CVerifyHostkeyDialog::t_keyData> CVerifyHostkeyDialog::m_sessionTrustedKeys;

void CVerifyHostkeyDialog::ShowVerificationDialog(wxWindow* parent, CHostKeyNotification* pNotification)
{
	wxDialogEx dlg;
	bool loaded;
	if (pNotification->GetRequestID() == reqId_hostkey)
		loaded = dlg.Load(parent, _T("ID_HOSTKEY"));
	else
		loaded = dlg.Load(parent, _T("ID_HOSTKEYCHANGED"));
	if (!loaded)
	{
		pNotification->m_trust = false;
		pNotification->m_alwaysTrust = false;
		wxBell();
		return;
	}

	dlg.WrapText(&dlg, XRCID("ID_DESC"), 400);

	const wxString host = wxString::Format(_T("%s:%d"), pNotification->GetHost(), pNotification->GetPort());
	dlg.SetChildLabel(XRCID("ID_HOST"), host);
	dlg.SetChildLabel(XRCID("ID_FINGERPRINT"), pNotification->GetFingerprint());

	dlg.GetSizer()->Fit(&dlg);
	dlg.GetSizer()->SetSizeHints(&dlg);

	int res = dlg.ShowModal();

	if (res == wxID_OK)
	{
		pNotification->m_trust = true;
		pNotification->m_alwaysTrust = XRCCTRL(dlg, "ID_ALWAYS", wxCheckBox)->GetValue();

		struct t_keyData data;
		data.host = host;
		data.fingerprint = pNotification->GetFingerprint();
		m_sessionTrustedKeys.push_back(data);
		return;
	}

	pNotification->m_trust = false;
	pNotification->m_alwaysTrust = false;
}

bool CVerifyHostkeyDialog::IsTrusted(CHostKeyNotification* pNotification)
{
	const wxString host = wxString::Format(_T("%s:%d"), pNotification->GetHost(), pNotification->GetPort());

	for(auto const& trusted : m_sessionTrustedKeys ) {
		if (trusted.host == host && trusted.fingerprint == pNotification->GetFingerprint())
			return true;
	}

	return false;
}
