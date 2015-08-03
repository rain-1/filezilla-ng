#include <filezilla.h>
#include "fzputtygen_interface.h"
#include "Options.h"
#include "filezillaapp.h"
#include "inputdialog.h"

CFZPuttyGenInterface::CFZPuttyGenInterface(wxWindow* parent)
	: m_parent(parent)
{
}

CFZPuttyGenInterface::~CFZPuttyGenInterface()
{
	if (m_pProcess)
		EndProcess();
}

void CFZPuttyGenInterface::EndProcess()
{
	m_pProcess->CloseOutput();
	m_pProcess->Detach();
	m_pProcess = 0;
}

void CFZPuttyGenInterface::DeleteProcess()
{
	delete m_pProcess;
	m_pProcess = 0;
}

bool CFZPuttyGenInterface::IsProcessCreated()
{
	return m_pProcess != 0;
}

bool CFZPuttyGenInterface::IsProcessStarted()
{
	return m_initialized;
}

bool CFZPuttyGenInterface::IsKeyFileValid(wxString keyFile, bool silent)
{
	bool result = false;
	wxString reply;
	ReplyCode code;

	if (!LoadProcess())
		return false;

	if (!Send(_T("file " + keyFile)))
		return false;

	code = GetReply(reply);
	if (code == failure)
		return false;
	if (code == error || (reply != _T("0") && reply != _T("1")))
	{
		if (!silent)
		{
			const wxString msg = wxString::Format(_("The file '%s' could not be loaded or does not contain a private key."), keyFile);
			wxMessageBoxEx(msg, _("Could not load keyfile"), wxICON_EXCLAMATION);
		}
		return false;
	}

	if (reply == _T("1"))
	{
		if (silent)
			return false;

		// This is a PPK key
		result = false;
	}
	else
		result = true;

	return result;
}

bool CFZPuttyGenInterface::IsKeyFileEncrypted(wxString keyFile, bool silent)
{
	bool result = false;
	wxString reply;
	ReplyCode code;

	if (!LoadProcess())
		return false;

	if (!Send(_T("encrypted")))
		return false;
	code = GetReply(reply);
	if (code != success)
	{
		wxASSERT(code != error);
		return false;
	}

	if (reply == _T("1"))
	{
		if (silent)
			return false;

		result = true;
	}
	else
		result = false;

	return result;
}

bool CFZPuttyGenInterface::LoadKeyFile(wxString& keyFile, bool silent, wxString& comment, wxString& data)
{
	wxString msg, reply;
	ReplyCode code;
	bool encrypted = false, needs_conversion = false;
	int res = 0;
	CInputDialog dlg;

	// If the key file is not valid, then it needs conversion
	needs_conversion = !IsKeyFileValid(keyFile, silent);
	encrypted = IsKeyFileEncrypted(keyFile, silent);

	if (encrypted || needs_conversion) {
		wxASSERT(!silent);

		if (needs_conversion) {
			if (!encrypted)
				msg = wxString::Format(_("The file '%s' is not in a format supported by FileZilla.\nWould you like to convert it into a supported format?"), keyFile);
			else
				msg = wxString::Format(_("The file '%s' is not in a format supported by FileZilla.\nThe file is also password protected. Password protected keyfiles are not supported by FileZilla yet.\nWould you like to convert it into a supported, unprotected format?"), keyFile);
		}
		else if (encrypted)
			msg = wxString::Format(_("The file '%s' is password protected. Password protected keyfiles are not supported by FileZilla yet.\nWould you like to convert it into an unprotected file?"), keyFile);

		res = wxMessageBoxEx(msg, _("Convert keyfile"), wxICON_QUESTION | wxYES_NO);
		if (res != wxYES)
			return false;

		if (encrypted) {
			msg = wxString::Format(_("Enter the password for the file '%s'.\nPlease note that the converted file will not be password protected."), keyFile);

			if (!dlg.Create(m_parent, _("Password required"), msg))
				return false;

			dlg.SetPasswordMode(true);

			if (dlg.ShowModal() != wxID_OK)
				return false;
			if (!Send(_T("password " + dlg.GetValue())))
				return false;
			if (GetReply(reply) != success)
				return false;
		}

		if (!Send(_T("load")))
			return false;
		code = GetReply(reply);
		if (code == failure)
			return false;
		if (code != success) {
			msg = wxString::Format(_("Failed to load private key: %s"), reply);
			wxMessageBoxEx(msg, _("Could not load private key"), wxICON_EXCLAMATION);
			return false;
		}

		wxFileDialog dlg(m_parent, _("Select filename for converted keyfile"), wxString(), wxString(), _T("PuTTY private key files (*.ppk)|*.ppk"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (dlg.ShowModal() != wxID_OK)
			return false;

		wxString newName = dlg.GetPath();
		if (newName.empty())
			return false;

		if (newName == keyFile) {
			// Not actually a requirement by fzputtygen, but be on the safe side. We don't want the user to lose his keys.
			wxMessageBoxEx(_("Source and target file may not be the same"), _("Could not convert private key"), wxICON_EXCLAMATION);
			return false;
		}

		Send(_T("write ") + newName);
		code = GetReply(reply);
		if (code == failure)
			return false;
		if (code != success) {
			wxMessageBoxEx(wxString::Format(_("Could not write keyfile: %s"), reply), _("Could not convert private key"), wxICON_EXCLAMATION);
			return false;
		}
		keyFile = newName;
	}
	else {
		if (!Send(_T("load")))
			return false;
		code = GetReply(reply);
		if (code != success)
			return false;
	}

	Send(_T("comment"));
	code = GetReply(comment);
	if (code != success)
		return false;

	Send(_T("data"));
	code = GetReply(data);
	if (code != success)
		return false;

	return true;
}

bool CFZPuttyGenInterface::LoadProcess()
{
	if (m_initialized)
		return m_pProcess != 0;

	wxString executable = COptions::Get()->GetOption(OPTION_FZSFTP_EXECUTABLE);
	int pos = executable.Find(wxFileName::GetPathSeparator(), true);
	if (pos == -1) {
		wxMessageBoxEx(_("fzputtygen could not be started.\nPlease make sure this executable exists in the same directory as the main FileZilla executable."), _("Error starting program"), wxICON_EXCLAMATION);
		return false;
	}
	else {
		executable = executable.Left(pos + 1) + _T("fzputtygen");
#ifdef __WXMSW__
		executable += _T(".exe");
#endif
		if (!executable.empty() && executable[0] == '"')
			executable += '"';

		if (executable.Find(' ') != -1 && executable[0] != '"') {
			executable = '"' + executable + '"';
		}
	}

	m_pProcess = new wxProcess(m_parent);
	m_pProcess->Redirect();

	if (!wxExecute(executable, wxEXEC_ASYNC, m_pProcess)) {
		delete m_pProcess;
		m_pProcess = 0;

		wxMessageBoxEx(_("fzputtygen could not be started.\nPlease make sure this executable exists in the same directory as the main FileZilla executable."), _("Error starting program"), wxICON_EXCLAMATION);
		return false;
	}

	m_initialized = true;
	return true;
}

bool CFZPuttyGenInterface::Send(const wxString& cmd)
{
	if (!m_pProcess)
		return false;

	const wxWX2MBbuf buf = (cmd + _T("\n")).mb_str();
	const size_t len = strlen (buf);

	wxOutputStream* stream = m_pProcess->GetOutputStream();
	stream->Write((const char *) buf, len);

	if (stream->GetLastError() != wxSTREAM_NO_ERROR || stream->LastWrite() != len) {
		wxMessageBoxEx(_("Could not send command to fzputtygen."), _("Command failed"), wxICON_EXCLAMATION);
		return false;
	}

	return true;
}

CFZPuttyGenInterface::ReplyCode CFZPuttyGenInterface::GetReply(wxString& reply)
{
	if (!m_pProcess)
		return failure;
	wxInputStream *pStream = m_pProcess->GetInputStream();
	if (!pStream) {
		wxMessageBoxEx(_("Could not get reply from fzputtygen."), _("Command failed"), wxICON_EXCLAMATION);
		return failure;
	}

	char buffer[100];

	wxString input;

	for (;;) {
		int pos = input.Find('\n');
		if (pos == wxNOT_FOUND) {
			pStream->Read(buffer, 99);
			int read;
			if (pStream->Eof() || !(read = pStream->LastRead())) {
				wxMessageBoxEx(_("Could not get reply from fzputtygen."), _("Command failed"), wxICON_EXCLAMATION);
				return failure;
			}
			buffer[read] = 0;

			// Should only ever return ASCII strings so this is ok
			input += wxString(buffer, wxConvUTF8);

			pos = input.Find('\n');
			if (pos == wxNOT_FOUND)
				continue;
		}

		int pos2;
		if (pos && input[pos - 1] == '\r')
			pos2 = pos - 1;
		else
			pos2 = pos;
		if (!pos2) {
			input = input.Mid(pos + 1);
			continue;
		}
		if (input.empty()) {
			continue;
		}
		wxChar c = input[0];

		reply = input.Mid(1, pos2 - 1);
		input = input.Mid(pos + 1);

		if (c == '0' || c == '1')
			return success;
		else if (c == '2')
			return error;
		// Ignore others
	}
}
