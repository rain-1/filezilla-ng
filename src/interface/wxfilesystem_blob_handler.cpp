#include <filezilla.h>
#include "wxfilesystem_blob_handler.h"

#include <wx/mstream.h>

bool wxFileSystemBlobHandler::CanOpen(wxString const& location)
{
	return GetProtocol(location) == _T("blob");
}

wxFSFile* wxFileSystemBlobHandler::OpenFile(wxFileSystem& fs, const wxString& location)
{
	auto pos = location.Find(':');
	if (pos != 4) {
		return 0;
	}
	wxString data = location.Mid(pos + 1);
	if (data.size() % 2) {
		return 0;
	}

	wxChar const* str = data.c_str();

	unsigned char* buf = static_cast<unsigned char*>(malloc(data.size() / 2));
	for (size_t i = 0; i < data.size() / 2; ++i) {
		buf[i] = static_cast<unsigned char>(fz::hex_char_to_int(str[i * 2]) * 16 + fz::hex_char_to_int(str[i * 2 + 1]));
	}

	// Whoever came up with the API for the wx streams obviously didn't ever use it.
	// Why else wouldn't it have an ownership-taking constructor?
	auto stream = new wxMemoryInputStream(0, data.size() / 2); 

	// Or why does it expect a buffer allocated with malloc?
	stream->GetInputStreamBuffer()->SetBufferIO(buf, data.size() / 2, true);

	return new wxFSFile(stream, location, _T(""), _T(""), wxDateTime());
}
