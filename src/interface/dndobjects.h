#ifndef FILEZILLA_INTERFACE_DNDOBJECTS_HEADER
#define FILEZILLA_INTERFACE_DNDOBJECTS_HEADER

#ifdef __WXMSW__
#define FZ3_USESHELLEXT 1
#else
#define FZ3_USESHELLEXT 0
#endif

#include "xmlfunctions.h"

#include <wx/dnd.h>

#include <memory>

class wxRemoteDataFormat final : public wxDataFormat
{
public:
	wxRemoteDataFormat()
		: wxDataFormat(_T("FileZilla3 remote data format v1"))
	{
	}
};

class CRemoteDataObject final : public wxDataObjectSimple
{
public:
	CRemoteDataObject(ServerWithCredentials const& server, const CServerPath& path);
	CRemoteDataObject();

	virtual size_t GetDataSize() const;
	virtual bool GetDataHere(void *buf ) const;

	virtual bool SetData(size_t len, const void* buf);

	// Finalize has to be called prior to calling wxDropSource::DoDragDrop
	void Finalize();

	bool DidSendData() const { return m_didSendData; }

	ServerWithCredentials const& GetServer() const { return server_; }
	const CServerPath& GetServerPath() const { return m_path; }
	int GetProcessId() const { return m_processId; }

	struct t_fileInfo
	{
		std::wstring name;
		int64_t size;
		bool dir;
		bool link;
	};

	const std::vector<t_fileInfo>& GetFiles() const { return m_fileList; }

	void Reserve(size_t count);
	void AddFile(const wxString& name, bool dir, int64_t size, bool link);

protected:
	ServerWithCredentials server_;
	CServerPath m_path;

	mutable CXmlFile m_xmlFile;

	bool m_didSendData{};

	int m_processId;

	std::vector<t_fileInfo> m_fileList;

	mutable size_t m_expectedSize{};
};

#if FZ3_USESHELLEXT

// This class checks if the shell extension is installed and
// communicates with it.
class CShellExtensionInterface final
{
public:
	CShellExtensionInterface();
	~CShellExtensionInterface();

	bool IsLoaded() const { return m_shellExtension != 0; }

	wxString InitDrag();

	wxString GetTarget();

	wxString GetDragDirectory() const { return m_dragDirectory; }

	static std::unique_ptr<CShellExtensionInterface> CreateInitialized();

protected:
	bool CreateDragDirectory();

	void* m_shellExtension;
	HANDLE m_hMutex;
	HANDLE m_hMapping;

	std::wstring m_dragDirectory;
};

#endif

#endif
