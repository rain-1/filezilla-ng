#include <filezilla.h>
#include "sftp_crypt_info_dlg.h"
#include "dialogex.h"
#include "xrc_helper.h"

void CSftpEncryptioInfoDialog::ShowDialog(CSftpEncryptionNotification* pNotification)
{
	wxDialogEx dlg;
	if (!dlg.Load(0, _T("ID_SFTP_ENCRYPTION"))) {
		wxBell();
		return;
	}

	SetLabel(dlg, XRCID("ID_KEXALGO"), pNotification->kexAlgorithm);
	SetLabel(dlg, XRCID("ID_KEXHASH"), pNotification->kexHash);
	if (!pNotification->kexCurve.empty()) {
		SetLabel(dlg, XRCID("ID_KEXCURVE"), pNotification->kexCurve);
	}
	else {
		xrc_call(dlg, "ID_KEXCURVE_LABEL", &wxWindow::Show, false);
		xrc_call(dlg, "ID_KEXCURVE", &wxWindow::Show, false);
	}

	if (!pNotification->hostKeyAlgorithm.empty()) {
		SetLabel(dlg, XRCID("ID_HOSTKEYALGO"), pNotification->hostKeyAlgorithm);
	}
	std::wstring fingerprints = fz::sprintf(L"SHA256: %s\nMD5: %s", pNotification->hostKeyFingerprintSHA256, pNotification->hostKeyFingerprintMD5);
	SetLabel(dlg, XRCID("ID_FINGERPRINT"), fingerprints);
	SetLabel(dlg, XRCID("ID_C2S_CIPHER"), pNotification->cipherClientToServer);
	SetLabel(dlg, XRCID("ID_C2S_MAC"), pNotification->macClientToServer);
	SetLabel(dlg, XRCID("ID_S2C_CIPHER"), pNotification->cipherServerToClient);
	SetLabel(dlg, XRCID("ID_S2C_MAC"), pNotification->macServerToClient);

	dlg.GetSizer()->Fit(&dlg);
	dlg.GetSizer()->SetSizeHints(&dlg);

	dlg.ShowModal();
}

void CSftpEncryptioInfoDialog::SetLabel(wxDialogEx & dlg, int id, const wxString& text)
{
	if (text.empty())
		dlg.SetChildLabel(id, _("unknown"));
	else
		dlg.SetChildLabel(id, text);
}
