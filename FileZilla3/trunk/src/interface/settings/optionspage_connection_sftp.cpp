#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection_sftp.h"
#include "../filezillaapp.h"
#include "../inputdialog.h"

BEGIN_EVENT_TABLE(COptionsPageConnectionSFTP, COptionsPage)
EVT_BUTTON(XRCID("ID_ADDKEY"), COptionsPageConnectionSFTP::OnAdd)
EVT_BUTTON(XRCID("ID_REMOVEKEY"), COptionsPageConnectionSFTP::OnRemove)
EVT_LIST_ITEM_SELECTED(wxID_ANY, COptionsPageConnectionSFTP::OnSelChanged)
EVT_LIST_ITEM_DESELECTED(wxID_ANY, COptionsPageConnectionSFTP::OnSelChanged)
END_EVENT_TABLE()

COptionsPageConnectionSFTP::COptionsPageConnectionSFTP()
{
	m_pFzpg = new CFZPuttyGenInterface(this);
}

COptionsPageConnectionSFTP::~COptionsPageConnectionSFTP()
{
	delete m_pFzpg;
}

bool COptionsPageConnectionSFTP::LoadPage()
{
	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);
	if (!pKeys) {
		return false;
	}
	pKeys->InsertColumn(0, _("Filename"), wxLIST_FORMAT_LEFT, 150);
	pKeys->InsertColumn(1, _("Comment"), wxLIST_FORMAT_LEFT, 100);
	pKeys->InsertColumn(2, _("Data"), wxLIST_FORMAT_LEFT, 350);

	// Generic wxListCtrl has gross minsize
	wxSize size = pKeys->GetMinSize();
	size.x = 1;
	pKeys->SetMinSize(size);

	std::wstring keyFiles = m_pOptions->GetOption(OPTION_SFTP_KEYFILES);
	auto tokens = fz::strtok(keyFiles, L"\r\n");
	for (auto const& token : tokens) {
		AddKey(token, true);
	}

	bool failure = false;

	SetCtrlState();

	SetCheckFromOption(XRCID("ID_SFTP_COMPRESSION"), OPTION_SFTP_COMPRESSION, failure);

	return !failure;
}

bool COptionsPageConnectionSFTP::SavePage()
{
	// Don't save keys on process error
	if (!m_pFzpg->ProcessFailed()) {
		wxString keyFiles;
		wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);
		for (int i = 0; i < pKeys->GetItemCount(); ++i) {
			if (!keyFiles.empty()) {
				keyFiles += _T("\n");
			}
			keyFiles += pKeys->GetItemText(i);
		}
		m_pOptions->SetOption(OPTION_SFTP_KEYFILES, keyFiles.ToStdWstring());
	}

	SetOptionFromCheck(XRCID("ID_SFTP_COMPRESSION"), OPTION_SFTP_COMPRESSION);

	return true;
}

void COptionsPageConnectionSFTP::OnAdd(wxCommandEvent&)
{
	wxFileDialog dlg(this, _("Select file containing private key"), wxString(), wxString(), wxFileSelectorDefaultWildcardStr, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	wxString const file = dlg.GetPath();

	AddKey(dlg.GetPath().ToStdWstring(), false);
}

void COptionsPageConnectionSFTP::OnRemove(wxCommandEvent&)
{
	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);

	int index = pKeys->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (index == -1) {
		return;
	}

	pKeys->DeleteItem(index);
}

bool COptionsPageConnectionSFTP::AddKey(std::wstring keyFile, bool silent)
{
	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);
	if (!pKeys) {
		return false;
	}

	std::wstring comment, data;
	if (!m_pFzpg->LoadKeyFile(keyFile, silent, comment, data)) {
		if (silent) {
			int index = pKeys->InsertItem(pKeys->GetItemCount(), keyFile);
			pKeys->SetItem(index, 1, comment);
			pKeys->SetItem(index, 2, data);
		}
		return false;
	}

	if (KeyFileExists(keyFile)) {
		if (!silent) {
			wxMessageBoxEx(_("Selected file is already loaded"), _("Cannot load key file"), wxICON_INFORMATION);
		}
		return false;
	}

	int index = pKeys->InsertItem(pKeys->GetItemCount(), keyFile);
	pKeys->SetItem(index, 1, comment);
	pKeys->SetItem(index, 2, data);

	return true;
}

bool COptionsPageConnectionSFTP::KeyFileExists(std::wstring const& keyFile)
{
	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);
	for (int i = 0; i < pKeys->GetItemCount(); ++i) {
		if (pKeys->GetItemText(i) == keyFile) {
			return true;
		}
	}
	return false;
}

void COptionsPageConnectionSFTP::SetCtrlState()
{
	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);

	int index = pKeys->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	XRCCTRL(*this, "ID_REMOVEKEY", wxButton)->Enable(index != -1);
	return;
}

void COptionsPageConnectionSFTP::OnSelChanged(wxListEvent&)
{
	SetCtrlState();
}
