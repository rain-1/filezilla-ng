#include <filezilla.h>
#include "auto_ascii_files.h"
#include "Options.h"

#include <libfilezilla/local_filesys.hpp>

std::vector<wxString> CAutoAsciiFiles::m_ascii_extensions;

void CAutoAsciiFiles::SettingsChanged()
{
	m_ascii_extensions.clear();
	wxString extensions = COptions::Get()->GetOption(OPTION_ASCIIFILES);
	wxString ext;
	int pos = extensions.Find(_T("|"));
	while (pos != -1) {
		if (!pos) {
			if (!ext.empty()) {
				ext.Replace(_T("\\\\"), _T("\\"));
				m_ascii_extensions.push_back(ext);
				ext = _T("");
			}
		}
		else if (extensions.c_str()[pos - 1] != '\\') {
			ext += extensions.Left(pos);
			ext.Replace(_T("\\\\"), _T("\\"));
			m_ascii_extensions.push_back(ext);
			ext = _T("");
		}
		else {
			ext += extensions.Left(pos - 1) + _T("|");
		}
		extensions = extensions.Mid(pos + 1);
		pos = extensions.Find(_T("|"));
	}
	ext += extensions;
	ext.Replace(_T("\\\\"), _T("\\"));
	m_ascii_extensions.push_back(ext);
}

// Defined in RemoteListView.cpp
std::wstring StripVMSRevision(std::wstring const& name);

bool CAutoAsciiFiles::TransferLocalAsAscii(wxString const& local_file, ServerType server_type)
{
	int pos = local_file.Find(fz::local_filesys::path_separator, true);

	// Identical implementation, only difference is for the local one to strip path.
	return TransferRemoteAsAscii(
		(pos != -1) ? local_file.Mid(pos + 1) : local_file,
		server_type
	);
}

bool CAutoAsciiFiles::TransferRemoteAsAscii(wxString const& remote_file, ServerType server_type)
{
	int mode = COptions::Get()->GetOptionVal(OPTION_ASCIIBINARY);
	if (mode == 1)
		return true;
	else if (mode == 2)
		return false;

	if (server_type == VMS) {
		return TransferRemoteAsAscii(StripVMSRevision(remote_file.ToStdWstring()), DEFAULT);
	}

	if (!remote_file.empty() && remote_file[0] == '.')
		return COptions::Get()->GetOptionVal(OPTION_ASCIIDOTFILE) != 0;

	int pos = remote_file.Find('.', true);
	if (pos < 0 || static_cast<unsigned int>(pos) + 1 == remote_file.size()) {
		return COptions::Get()->GetOptionVal(OPTION_ASCIINOEXT) != 0;
	}
	wxString ext = remote_file.Mid(pos + 1);

	for (auto const& ascii_ext : m_ascii_extensions) {
		if (!ext.CmpNoCase(ascii_ext)) {
			return true;
		}
	}

	return false;
}
