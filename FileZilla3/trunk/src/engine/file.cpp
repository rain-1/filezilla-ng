#include <filezilla.h>

#include "file.h"

CFile::CFile()
{
}

CFile::CFile(wxString const& f, mode m, disposition d)
{
	Open(f, m);
}

CFile::~CFile()
{
	Close();
}

#ifdef __WXMSW__
bool CFile::Open(wxString const& f, mode m, disposition d)
{
	Close();
	
	DWORD dispositionFlags;
	if (m == write) {
		if (d == truncate) {
			dispositionFlags = CREATE_ALWAYS;
		}
		else {
			dispositionFlags = OPEN_ALWAYS;
		}
	}
	else {
		dispositionFlags = OPEN_EXISTING;
	}
	hFile_ = CreateFile(f, (m == read) ? GENERIC_READ : GENERIC_WRITE, FILE_SHARE_READ, 0, dispositionFlags, FILE_FLAG_SEQUENTIAL_SCAN, 0);

	return hFile_ != INVALID_HANDLE_VALUE;
}

void CFile::Close()
{
	if (hFile_ != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile_);
		hFile_ = 0;
	}
}

wxFileOffset CFile::Length() const
{
	wxFileOffset ret = -1;

	LARGE_INTEGER size{};
	if (GetFileSizeEx(hFile_, &size)) {
		ret = size.QuadPart;
	}
	return ret;
}

wxFileOffset CFile::Seek(wxFileOffset offset, seekMode m)
{
	wxFileOffset ret = -1;

	LARGE_INTEGER dist{};
	dist.QuadPart = offset;

	DWORD method = FILE_BEGIN;
	if (m == current) {
		method = FILE_CURRENT;
	}
	else if (m == end) {
		method = FILE_END;
	}

	LARGE_INTEGER newPos{};
	if (SetFilePointerEx(hFile_, dist, &newPos, method)) {
		ret = newPos.QuadPart;
	}
	return ret;
}

ssize_t CFile::Read(void *buf, size_t count)
{
	ssize_t ret = -1;

	DWORD read = 0;
	if (ReadFile(hFile_, buf, count, &read, 0)) {
		ret = static_cast<ssize_t>(read);
	}

	return ret;
}

ssize_t CFile::Write(void const* buf, size_t count)
{
	ssize_t ret = -1;

	DWORD written = 0;
	if (WriteFile(hFile_, buf, count, &written, 0)) {
		ret = static_cast<ssize_t>(written);
	}

	return ret;
}

#endif