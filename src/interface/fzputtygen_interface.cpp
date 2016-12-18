#include <filezilla.h>
#include "fzputtygen_interface.h"
#include "Options.h"
#include "filezillaapp.h"
#include "inputdialog.h"

#include <libfilezilla/process.hpp>

CFZPuttyGenInterface::CFZPuttyGenInterface(wxWindow* parent)
	: m_parent(parent)
{
}

CFZPuttyGenInterface::~CFZPuttyGenInterface()
{
}

int CFZPuttyGenInterface::NeedsConversion(std::wstring const& keyFile, bool silent)
{
	if (!Send(L"file " + keyFile)) {
		return -1;
	}

	std::wstring reply;
	ReplyCode code = GetReply(reply);
	if (code == failure) {
		return -1;
	}
	if (code == error || (reply != L"ok" && reply != L"convertible")) {
		if (!silent) {
			wxString msg;
			if (reply == _T("incompatible")) {
				msg.Printf(_("The file '%s' contains an SSH1 key. The SSH1 protocol has been deprecated, FileZilla only supports SSH2 keys."), keyFile);
			}
			else {
				msg.Printf(_("The file '%s' could not be loaded or does not contain a private key."), keyFile);
			}
			wxMessageBoxEx(msg, _("Could not load key file"), wxICON_EXCLAMATION);
		}
		return -1;
	}

	return reply == L"convertible" ? 1 : 0;
}

int CFZPuttyGenInterface::IsKeyFileEncrypted()
{
	if (!Send(L"encrypted")) {
		return -1;
	}

	std::wstring reply;
	ReplyCode code = GetReply(reply);
	if (code != success) {
		assert(code != error);
		return -1;
	}

	return reply == _T("1") ? 1 : 0;
}

bool CFZPuttyGenInterface::LoadKeyFile(std::wstring& keyFile, bool silent, std::wstring& comment, std::wstring& data)
{
	if (!LoadProcess(silent)) {
		comment = _("Could not load key file");
		return false;
	}

	int needs_conversion = NeedsConversion(keyFile, silent);
	if (needs_conversion < 0) {
		comment = _("Could not load key file");
		return false;
	}

	ReplyCode code;
	if (needs_conversion) {
		if (silent) {
			comment = _("Could not load key file");
			return false;
		}

		int encrypted = IsKeyFileEncrypted();
		if (encrypted < 0) {
			return false;
		}

		wxString msg = wxString::Format(_("The file '%s' is not in a format supported by FileZilla.\nWould you like to convert it into a supported format?"), keyFile);
		int res = wxMessageBoxEx(msg, _("Convert key file"), wxICON_QUESTION | wxYES_NO);
		if (res != wxYES) {
			return false;
		}

		msg = wxString::Format(_("Enter the password for the file '%s'.\nThe converted file will be protected with the same password."), keyFile);
		CInputDialog dlg;
		if (!dlg.Create(m_parent, _("Password required"), msg)) {
			return false;
		}

		dlg.SetPasswordMode(true);

		if (dlg.ShowModal() != wxID_OK) {
			return false;
		}
		if (!Send(L"password " + dlg.GetValue().ToStdWstring())) {
			return false;
		}
		std::wstring reply;
		code = GetReply(reply);
		if (code != success) {
			msg = wxString::Format(_("Failed to load private key: %s"), reply);
			wxMessageBoxEx(msg, _("Could not load private key"), wxICON_EXCLAMATION);
			return false;
		}

		wxFileDialog fileDlg(m_parent, _("Select filename for converted key file"), wxString(), wxString(), _T("PuTTY private key files (*.ppk)|*.ppk"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		if (fileDlg.ShowModal() != wxID_OK) {
			return false;
		}

		std::wstring newName = fileDlg.GetPath().ToStdWstring();
		if (newName.empty()) {
			return false;
		}

		if (newName == keyFile) {
			// Not actually a requirement by fzputtygen, but be on the safe side. We don't want the user to lose his keys.
			wxMessageBoxEx(_("Source and target file may not be the same"), _("Could not convert private key"), wxICON_EXCLAMATION);
			return false;
		}

		Send(L"write " + newName);
		code = GetReply(reply);
		if (code == failure) {
			return false;
		}
		if (code != success) {
			wxMessageBoxEx(wxString::Format(_("Could not write key file: %s"), reply), _("Could not convert private key"), wxICON_EXCLAMATION);
			return false;
		}
		keyFile = newName;
	}

	if (!Send(L"fingerprint")) {
		comment = _("Could not load key file");
		return false;
	}
	code = GetReply(data);
	if (code != success) {
		comment = _("Could not load key file");
		data.clear();
		return false;
	}

	Send(L"comment");
	code = GetReply(comment);
	if (code != success) {
		comment = _("Could not load key file");
		data.clear();
		comment.clear();
		return false;
	}

	return true;
}

bool CFZPuttyGenInterface::LoadProcess(bool silent)
{
	if (m_initialized) {
		return m_process != 0;
	}
	m_initialized = true;

	std::wstring executable = COptions::Get()->GetOption(OPTION_FZSFTP_EXECUTABLE);
	size_t pos = executable.rfind(wxFileName::GetPathSeparator());
	if (pos == std::wstring::npos) {
		if (!silent) {
			wxMessageBoxEx(_("fzputtygen could not be started.\nPlease make sure this executable exists in the same directory as the main FileZilla executable."), _("Error starting program"), wxICON_EXCLAMATION);
		}
		return false;
	}

	executable = executable.substr(0, pos + 1) + L"fzputtygen";
#ifdef FZ_WINDOWS
	executable += L".exe";
#endif

	m_process = std::make_unique<fz::process>();
	if (!m_process->spawn(fz::to_native(executable))) {
		m_process.reset();

		if (!silent) {
			wxMessageBoxEx(_("fzputtygen could not be started.\nPlease make sure this executable exists in the same directory as the main FileZilla executable."), _("Error starting program"), wxICON_EXCLAMATION);
		}
		return false;
	}

	return true;
}

bool CFZPuttyGenInterface::Send(std::wstring const& cmd)
{
	if (!m_process) {
		return false;
	}

	std::string utf8 = fz::to_utf8(cmd) + "\n";
	if (!m_process->write(utf8)) {
		m_process.reset();

		wxMessageBoxEx(_("Could not send command to fzputtygen."), _("Command failed"), wxICON_EXCLAMATION);
		return false;
	}

	return true;
}

CFZPuttyGenInterface::ReplyCode CFZPuttyGenInterface::GetReply(std::wstring & reply)
{
	if (!m_process) {
		return failure;
	}

	char buffer[100];

	std::string input;

	for (;;) {
		size_t pos = input.find_first_of("\r\n");
		if (pos == std::string::npos) {
			int read = m_process->read(buffer, 100);
			if (read <= 0) {
				wxMessageBoxEx(_("Could not get reply from fzputtygen."), _("Command failed"), wxICON_EXCLAMATION);
				m_process.reset();
				return failure;
			}

			input.append(buffer, read);
			continue;
		}

		// Strip everything behind first linebreak.
		if (!pos) {
			input = input.substr(1);
			continue;
		}

		char c = input[0];

		reply = fz::to_wstring_from_utf8(input.substr(1, pos - 1));
		input = input.substr(pos + 1);

		if (c == '0' || c == '1') {
			return success;
		}
		else if (c == '2') {
			return error;
		}
		// Ignore others
	}
}

bool CFZPuttyGenInterface::ProcessFailed() const
{
	return m_initialized && !m_process;
}
