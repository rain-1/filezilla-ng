#include <filezilla.h>
#include "wxfilesystem_blob_handler.h"

#include <wx/mstream.h>

bool wxFileSystemBlobHandler::CanOpen(wxString const& location)
{
	auto const proto = GetProtocol(location);
	return proto == _T("blob") || proto == _T("blob64");
}

wxString wxFileSystemBlobHandler::FindFirst(wxString const& wildcard, int flags)
{
	if ((flags & wxFILE) != wxFILE) {
		return wxString();
	}

	return wildcard;
}

wxFSFile* wxFileSystemBlobHandler::OpenFile(wxFileSystem&, const wxString& location)
{
	unsigned char* buf{};
	size_t buf_len{};

	auto pos = location.Find(':');
	if (pos == 4) {
		// hex
		wxString data = location.Mid(pos + 1);
		if (data.size() % 2) {
			return 0;
		}

		wxChar const* str = data.c_str();

		buf = static_cast<unsigned char*>(malloc(data.size() / 2));
		for (size_t i = 0; i < data.size() / 2; ++i) {
			buf[i] = static_cast<unsigned char>(fz::hex_char_to_int(str[i * 2]) * 16 + fz::hex_char_to_int(str[i * 2 + 1]));
		}

		buf_len = data.size() / 2;
	}
	else if (pos == 6) {
		// base64
		std::string data = fz::base64_decode(fz::to_utf8(location.Mid(pos + 1)));
		if (!data.empty()) {
			buf = static_cast<unsigned char*>(malloc(data.size()));
			memcpy(buf, data.c_str(), data.size());
			buf_len = data.size();
		}
	}

	if (buf) {
		// Whoever came up with the API for the wx streams obviously didn't ever use it.
		// Why else wouldn't it have an ownership-taking constructor?
		auto stream = new wxMemoryInputStream(0, buf_len);

		// Or why does it expect a buffer allocated with malloc?
		stream->GetInputStreamBuffer()->SetBufferIO(buf, buf_len, true);

		return new wxFSFile(stream, location, _T(""), _T(""), wxDateTime());
	}

	return 0;
}
