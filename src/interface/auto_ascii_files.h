#ifndef FILEZILLA_INTERFACE_AUTO_ASCII_FILES_HEADER
#define FILEZILLA_INTERFACE_AUTO_ASCII_FILES_HEADER

class CAutoAsciiFiles final
{
public:
	static bool TransferLocalAsAscii(wxString const& local_file, ServerType server_type);
	static bool TransferRemoteAsAscii(wxString const& remote_file, ServerType server_type);

	static void SettingsChanged();
protected:
	static bool IsAsciiExtension(wxString const& ext);
	static std::vector<wxString> m_ascii_extensions;
};

#endif
