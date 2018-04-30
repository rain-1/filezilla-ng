#include <filezilla.h>
#include "dndobjects.h"

#if FZ3_USESHELLEXT

#include <initguid.h>
#include "../fzshellext/shellext.h"
#include <shlobj.h>
#include <wx/stdpaths.h>

CShellExtensionInterface::CShellExtensionInterface()
{
	m_shellExtension = 0;

	int res = CoCreateInstance(CLSID_ShellExtension, NULL,
	  CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IUnknown,
	  &m_shellExtension);
	if (res != S_OK) {
		m_shellExtension = 0;
	}

	if (m_shellExtension) {
		m_hMutex = CreateMutex(0, false, _T("FileZilla3DragDropExtLogMutex"));
	}
	else {
		m_hMutex = 0;
	}

	m_hMapping = 0;
}

CShellExtensionInterface::~CShellExtensionInterface()
{
	if (m_shellExtension) {
		((IUnknown*)m_shellExtension)->Release();
		CoFreeUnusedLibraries();
	}

	if (m_hMapping) {
		CloseHandle(m_hMapping);
	}

	if (m_hMutex) {
		CloseHandle(m_hMutex);
	}

	if (!m_dragDirectory.empty()) {
		RemoveDirectory(m_dragDirectory.c_str());
	}
}

wxString CShellExtensionInterface::InitDrag()
{
	if (!m_shellExtension) {
		return wxString();
	}

	if (!m_hMutex) {
		return wxString();
	}

	if (!CreateDragDirectory()) {
		return wxString();
	}

	m_hMapping = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, DRAG_EXT_MAPPING_LENGTH, DRAG_EXT_MAPPING);
	if (!m_hMapping) {
		return wxString();
	}

	char* data = (char*)MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DRAG_EXT_MAPPING_LENGTH);
	if (!data) {
		CloseHandle(m_hMapping);
		m_hMapping = 0;
		return wxString();
	}

	DWORD result = WaitForSingleObject(m_hMutex, 250);
	if (result != WAIT_OBJECT_0) {
		UnmapViewOfFile(data);
		return wxString();
	}

	*data = DRAG_EXT_VERSION;
	data[1] = 1;
	wcscpy((wchar_t*)(data + 2), m_dragDirectory.c_str());

	ReleaseMutex(m_hMutex);

	UnmapViewOfFile(data);

	return m_dragDirectory;
}

wxString CShellExtensionInterface::GetTarget()
{
	if (!m_shellExtension) {
		return wxString();
	}

	if (!m_hMutex) {
		return wxString();
	}

	if (!m_hMapping) {
		return wxString();
	}

	char* data = (char*)MapViewOfFile(m_hMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, DRAG_EXT_MAPPING_LENGTH);
	if (!data) {
		CloseHandle(m_hMapping);
		m_hMapping = 0;
		return wxString();
	}

	DWORD result = WaitForSingleObject(m_hMutex, 250);
	if (result != WAIT_OBJECT_0) {
		UnmapViewOfFile(data);
		return wxString();
	}

	wxString target;
	const char reply = data[1];
	if (reply == 2) {
		data[DRAG_EXT_MAPPING_LENGTH - 1] = 0;
		data[DRAG_EXT_MAPPING_LENGTH - 2] = 0;
		target = (wchar_t*)(data + 2);
	}

	ReleaseMutex(m_hMutex);

	UnmapViewOfFile(data);

	if (target.empty()) {
		return target;
	}

	if (target.Last() == '\\') {
		target.RemoveLast();
	}
	int pos = target.Find('\\', true);
	if (pos < 1) {
		return wxString();
	}
	target = target.Left(pos + 1);

	return target;
}

bool CShellExtensionInterface::CreateDragDirectory()
{
	for (int i = 0; i < 10; ++i) {
		auto const now = fz::datetime::now();
		int64_t value = now.get_time_t();
		value *= 1000;
		value += now.get_milliseconds();
		value *= 10;
		value += i;

		wxFileName dirname(wxStandardPaths::Get().GetTempDir(), DRAG_EXT_DUMMY_DIR_PREFIX + std::to_wstring(value));
		dirname.Normalize();
		std::wstring dir = dirname.GetFullPath().ToStdWstring();

		if (dir.size() > DRAG_EXT_MAX_PATH) {
			return false;
		}

		if (CreateDirectory(dir.c_str(), 0)) {
			m_dragDirectory = dir;
			return true;
		}
	}

	return true;
}

std::unique_ptr<CShellExtensionInterface> CShellExtensionInterface::CreateInitialized()
{
	auto ret = std::make_unique<CShellExtensionInterface>();
	if (!ret->IsLoaded() || ret->InitDrag().empty()) {
		ret.reset();
	}

	return ret;
}

#endif

CRemoteDataObject::CRemoteDataObject(ServerWithCredentials const& server, const CServerPath& path)
	: wxDataObjectSimple(wxDataFormat(_T("FileZilla3RemoteDataObject")))
	, server_(server)
	, m_path(path)
	, m_processId(wxGetProcessId())
{
}

CRemoteDataObject::CRemoteDataObject()
	: wxDataObjectSimple(wxDataFormat(_T("FileZilla3RemoteDataObject")))
	, m_processId(wxGetProcessId())
{
}

size_t CRemoteDataObject::GetDataSize() const
{
	wxASSERT(!m_path.empty());

	wxCHECK(m_xmlFile.GetElement(), 0);

	m_expectedSize = m_xmlFile.GetRawDataLength() + 1;

	return m_expectedSize;
}

bool CRemoteDataObject::GetDataHere(void *buf) const
{
	wxASSERT(!m_path.empty());

	wxCHECK(m_xmlFile.GetElement(), false);

	m_xmlFile.GetRawDataHere((char*)buf, m_expectedSize);
	if (m_expectedSize > 0) {
		static_cast<char*>(buf)[m_expectedSize - 1] = 0;
	}

	const_cast<CRemoteDataObject*>(this)->m_didSendData = true;
	return true;
}

void CRemoteDataObject::Finalize()
{
	// Convert data into XML
	auto element = m_xmlFile.CreateEmpty();
	element = element.append_child("RemoteDataObject");

	AddTextElement(element, "ProcessId", m_processId);

	AddTextElement(element, "Count", m_fileList.size());

	auto xServer = element.append_child("Server");
	SetServer(xServer, server_);

	AddTextElement(element, "Path", m_path.GetSafePath());

	auto files = element.append_child("Files");
	for (auto const& info : m_fileList) {
		auto file = files.append_child("File");
		AddTextElement(file, "Name", info.name);
		AddTextElement(file, "Dir", info.dir ? 1 : 0);
		AddTextElement(file, "Size", info.size);
		AddTextElement(file, "Link", info.link ? 1 : 0);
	}
}

bool CRemoteDataObject::SetData(size_t len, const void* buf)
{
	char* data = (char*)buf;
	if (!len || data[len - 1] != 0) {
		return false;
	}

	if (!m_xmlFile.ParseData(data)) {
		return false;
	}

	auto element = m_xmlFile.GetElement();
	if (!element || !(element = element.child("RemoteDataObject"))) {
		return false;
	}

	m_processId = GetTextElementInt(element, "ProcessId", -1);
	if (m_processId == -1) {
		return false;
	}

	int64_t count = GetTextElementInt(element, "Count", -1);
	if (count > 0) {
		try {
			m_fileList.reserve(static_cast<size_t>(count));
		}
		catch(std::exception const&) {
			return false;
		}
	}

	auto serverElement = element.child("Server");
	if (!serverElement || !::GetServer(serverElement, server_)) {
		return false;
	}

	std::wstring path = GetTextElement(element, "Path");
	if (path.empty() || !m_path.SetSafePath(path)) {
		return false;
	}

	m_fileList.clear();
	auto files = element.child("Files");
	if (!files) {
		return false;
	}

	for (auto file = files.child("File"); file; file = file.next_sibling("File")) {
		t_fileInfo info;
		info.name = GetTextElement(file, "Name");
		if (info.name.empty()) {
			return false;
		}

		const int dir = GetTextElementInt(file, "Dir", -1);
		if (dir == -1) {
			return false;
		}
		info.dir = dir == 1;

		info.size = GetTextElementInt(file, "Size", -2);
		if (info.size <= -2) {
			return false;
		}

		info.link = GetTextElementBool(file, "Link", false);

		m_fileList.push_back(info);
	}

	return true;
}

void CRemoteDataObject::Reserve(size_t count)
{
	m_fileList.reserve(count);
}

void CRemoteDataObject::AddFile(const wxString& name, bool dir, int64_t size, bool link)
{
	t_fileInfo info;
	info.name = name;
	info.dir = dir;
	info.size = size;
	info.link = link;

	m_fileList.push_back(info);
}
