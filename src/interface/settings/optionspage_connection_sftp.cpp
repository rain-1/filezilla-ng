#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection_sftp.h"
#include "../filezillaapp.h"
#include "../inputdialog.h"
#include <wx/tokenzr.h>

BEGIN_EVENT_TABLE(COptionsPageConnectionSFTP, COptionsPage)
EVT_END_PROCESS(wxID_ANY, COptionsPageConnectionSFTP::OnEndProcess)
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
	if (!pKeys)
		return false;
	pKeys->InsertColumn(0, _("Filename"), wxLIST_FORMAT_LEFT, 150);
	pKeys->InsertColumn(1, _("Comment"), wxLIST_FORMAT_LEFT, 100);
	pKeys->InsertColumn(2, _("Data"), wxLIST_FORMAT_LEFT, 350);

	// Generic wxListCtrl has gross minsize
	wxSize size = pKeys->GetMinSize();
	size.x = 1;
	pKeys->SetMinSize(size);

	wxString keyFiles = m_pOptions->GetOption(OPTION_SFTP_KEYFILES);
	wxStringTokenizer tokens(keyFiles, _T("\n"), wxTOKEN_DEFAULT);
	while (tokens.HasMoreTokens())
		AddKey(tokens.GetNextToken(), true);

	bool failure = false;

	SetCtrlState();

	SetCheckFromOption(XRCID("ID_SFTP_COMPRESSION"), OPTION_SFTP_COMPRESSION, failure);

	return !failure;
}

bool COptionsPageConnectionSFTP::SavePage()
{
	// Don't save keys on process error
	if (!m_pFzpg->IsProcessStarted() || m_pFzpg->IsProcessCreated()) {
		wxString keyFiles;
		wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);
		for (int i = 0; i < pKeys->GetItemCount(); i++) {
			if (!keyFiles.empty())
				keyFiles += _T("\n");
			keyFiles += pKeys->GetItemText(i);
		}
		m_pOptions->SetOption(OPTION_SFTP_KEYFILES, keyFiles);
	}

	if (m_pFzpg->IsProcessCreated())
		m_pFzpg->EndProcess();

	SetOptionFromCheck(XRCID("ID_SFTP_COMPRESSION"), OPTION_SFTP_COMPRESSION);

	return true;
}

void COptionsPageConnectionSFTP::OnAdd(wxCommandEvent& event)
{
	wxFileDialog dlg(this, _("Select file containing private key"), wxString(), wxString(), wxFileSelectorDefaultWildcardStr, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK)
		return;

	const wxString file = dlg.GetPath();

	AddKey(dlg.GetPath(), false);
}

void COptionsPageConnectionSFTP::OnRemove(wxCommandEvent& event)
{
	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);

	int index = pKeys->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (index == -1)
		return;

	pKeys->DeleteItem(index);
}

bool COptionsPageConnectionSFTP::AddKey(wxString keyFile, bool silent)
{
	wxString comment, data;
	if (!m_pFzpg->LoadKeyFile(keyFile, silent, comment, data))
		return false;

	if (KeyFileExists(keyFile))
	{
		if (!silent)
			wxMessageBoxEx(_("Selected file is already loaded"), _("Cannot load keyfile"), wxICON_INFORMATION);
		return false;
	}

	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);
	int index = pKeys->InsertItem(pKeys->GetItemCount(), keyFile);
	pKeys->SetItem(index, 1, comment);
	pKeys->SetItem(index, 2, data);

	return true;
}

bool COptionsPageConnectionSFTP::KeyFileExists(const wxString& keyFile)
{
	wxListCtrl* pKeys = XRCCTRL(*this, "ID_KEYS", wxListCtrl);
	for (int i = 0; i < pKeys->GetItemCount(); ++i)
	{
		if (pKeys->GetItemText(i) == keyFile)
			return true;
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

void COptionsPageConnectionSFTP::OnSelChanged(wxListEvent& event)
{
	SetCtrlState();
}

void COptionsPageConnectionSFTP::OnEndProcess(wxProcessEvent& event)
{
	m_pFzpg->DeleteProcess();
}
