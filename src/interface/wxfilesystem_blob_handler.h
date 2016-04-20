#ifndef FILEZILLA_INTERFACE_WXFILESYSTEM_BLOB_HANDLER_HEADER
#define FILEZILLA_INTERFACE_WXFILESYSTEM_BLOB_HANDLER_HEADER

#include <wx/filesys.h>

// A handler for wxFileSystem where the filename is the hex-encoded file content.
class wxFileSystemBlobHandler : public wxFileSystemHandler
{
public:
	wxFileSystemBlobHandler() = default;

	virtual bool CanOpen(wxString const& location);

	virtual wxFSFile* OpenFile(wxFileSystem& fs, wxString const& location);

	virtual wxString FindFirst(wxString const& wildcard, int flags);
};

#endif