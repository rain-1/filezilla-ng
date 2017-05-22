#ifndef FILEZILLA_INTERFACE_EDITHANDLER_HEADER
#define FILEZILLA_INTERFACE_EDITHANDLER_HEADER

#include "dialogex.h"
#include "serverdata.h"

#include <wx/timer.h>

// Handles all aspects about remote file viewing/editing

namespace edit_choices {
enum type
{
	edit_existing_action = 0x1,
	edit_existing_always = 0x2
};
}

class CQueueView;
class CEditHandler final : protected wxEvtHandler
{
public:
	enum fileState
	{
		unknown = -1,
		edit,
		download,
		upload,
		upload_and_remove,
		upload_and_remove_failed,
		removing
	};

	enum fileType : signed char
	{
		none = -1,
		local,
		remote
	};

	static CEditHandler* Create();
	static CEditHandler* Get();

	std::wstring GetLocalDirectory();

	// This tries to deletes all temporary files.
	// If files are locked, they won't be removed though
	void Release();

	fileState GetFileState(wxString const& fileName) const; // Local files
	fileState GetFileState(wxString const& fileName, CServerPath const& remotePath, ServerWithCredentials const& server) const; // Remote files

	// Returns the number of files in given state
	// pServer may be set only if state isn't unknown
	int GetFileCount(fileType type, fileState state, ServerWithCredentials const& server = ServerWithCredentials()) const;

	// Starts editing the given file, queues it if needed. For local files, fileName must include local path.
	// Can be used to edit files already being added, user is prompted for action.
	bool Edit(CEditHandler::fileType type, std::wstring const& fileName, CServerPath const& path, ServerWithCredentials const& server, int64_t size, wxWindow* parent);

	class FileData final {
	public:
		FileData() = default;
		FileData(std::wstring const& n, int64_t s)
			: name(n), size(s) {}

		std::wstring name;
		int64_t size{};
	};
	bool Edit(CEditHandler::fileType type, std::vector<FileData> const& data, CServerPath const& path, ServerWithCredentials const& server, wxWindow* parent);

	// Adds the file that doesn't exist yet. (Has to be in unknown state)
	// The initial state will be download
	bool AddFile(fileType type, std::wstring& fileName, CServerPath const& remotePath, ServerWithCredentials const& server);

	// Tries to unedit and remove file
	bool Remove(wxString const& fileName); // Local files
	bool Remove(wxString const& fileName, CServerPath const& remotePath, ServerWithCredentials const& server); // Remote files
	bool RemoveAll(bool force);
	bool RemoveAll(fileState state, ServerWithCredentials const& server = ServerWithCredentials());

	void FinishTransfer(bool successful, wxString const& fileName);
	void FinishTransfer(bool successful, wxString const& fileName, CServerPath const& remotePath, ServerWithCredentials const& server);

	void CheckForModifications(bool emitEvent = false);

	void SetQueue(CQueueView* pQueue) { m_pQueue = pQueue; }

	/* Checks if file can be opened. One of these conditions has to be true:
	 * - Filetype association of system has to exist
	 * - Custom association for that filetype
	 * - Default editor set
	 *
	 * The dangerous argument will be set to true on some filetypes,
	 * e.g. executables.
	 */
	wxString CanOpen(fileType type, wxString const& fileName, bool &dangerous, bool& program_exists);
	bool StartEditing(wxString const& file);
	bool StartEditing(wxString const& file, CServerPath const& remotePath, ServerWithCredentials const& server);

	struct t_fileData
	{
		std::wstring name; // The name of the file
		std::wstring file; // The actual local filename
		fileState state;
		fz::datetime modificationTime;
		CServerPath remotePath;
		ServerWithCredentials server;
	};

	const std::list<t_fileData>& GetFiles(fileType type) const { wxASSERT(type != none); return m_fileDataList[(type == local) ? 0 : 1]; }

	bool UploadFile(wxString const& file, bool unedit);
	bool UploadFile(wxString const& file, CServerPath const& remotePath, ServerWithCredentials const& server, bool unedit);

	// Returns command to open the file. If association is set but
	// program does not exist, program_exists is set to false.
	wxString GetOpenCommand(wxString const& file, bool& program_exists);

protected:
	bool DoEdit(CEditHandler::fileType type, FileData const& file, CServerPath const& path, ServerWithCredentials const& server, wxWindow* parent, size_t fileCount, int & already_editing_action);

	CEditHandler();

	static CEditHandler* m_pEditHandler;

	std::wstring m_localDir;

	bool StartEditing(fileType type, t_fileData &data);

	wxString GetCustomOpenCommand(wxString const& file, bool& program_exists);

	void SetTimerState();

	bool UploadFile(fileType type, std::list<t_fileData>::iterator iter, bool unedit);

	std::list<t_fileData> m_fileDataList[2];

	std::list<t_fileData>::iterator GetFile(wxString const& fileName);
	std::list<t_fileData>::const_iterator GetFile(wxString const& fileName) const;
	std::list<t_fileData>::iterator GetFile(wxString const& fileName, CServerPath const& remotePath, ServerWithCredentials const& server);
	std::list<t_fileData>::const_iterator GetFile(wxString const& fileName, CServerPath const& remotePath, ServerWithCredentials const& server) const;

	CQueueView* m_pQueue;

	wxTimer m_timer;
	wxTimer m_busyTimer;

	void RemoveTemporaryFiles(wxString const& temp);
	void RemoveTemporaryFilesInSpecificDir(wxString const& temp);

	std::wstring GetTemporaryFile(std::wstring name);
	wxString TruncateFilename(wxString const path, wxString const& name, int max);
	bool FilenameExists(wxString const& file);

	int DisplayChangeNotification(fileType type, std::list<t_fileData>::const_iterator iter, bool& remove);

#ifdef __WXMSW__
	HANDLE m_lockfile_handle;
#else
	int m_lockfile_descriptor;
#endif

	DECLARE_EVENT_TABLE()
	void OnTimerEvent(wxTimerEvent& event);
	void OnChangedFileEvent(wxCommandEvent& event);
};

class CWindowStateManager;
class CEditHandlerStatusDialog final : protected wxDialogEx
{
public:
	CEditHandlerStatusDialog(wxWindow* parent);
	virtual ~CEditHandlerStatusDialog();

	virtual int ShowModal();

protected:
	void SetCtrlState();

	CEditHandler::t_fileData* GetDataFromItem(int item, CEditHandler::fileType &type);

	wxWindow* m_pParent;

	CWindowStateManager* m_pWindowStateManager;

	DECLARE_EVENT_TABLE()
	void OnSelectionChanged(wxListEvent& event);
	void OnUnedit(wxCommandEvent& event);
	void OnUpload(wxCommandEvent& event);
	void OnEdit(wxCommandEvent& event);
};

class CNewAssociationDialog final : protected wxDialogEx
{
public:
	CNewAssociationDialog(wxWindow* parent);

	bool Run(const wxString& file);

protected:
	void SetCtrlState();
	wxWindow* m_pParent;
	wxString m_ext;

	DECLARE_EVENT_TABLE()
	void OnRadioButton(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnBrowseEditor(wxCommandEvent& event);
};

#endif
