#ifndef __STATE_H__
#define __STATE_H__

#include "local_path.h"
#include "sitemanager.h"

#include <memory>

enum t_statechange_notifications
{
	STATECHANGE_NONE, // Used to unregister all notifications

	STATECHANGE_REMOTE_DIR,
	STATECHANGE_REMOTE_DIR_OTHER,
	STATECHANGE_REMOTE_RECV,
	STATECHANGE_REMOTE_SEND,
	STATECHANGE_REMOTE_LINKNOTDIR,
	STATECHANGE_LOCAL_DIR,

	// data contains name (excluding path) of file to refresh
	STATECHANGE_LOCAL_REFRESH_FILE,

	STATECHANGE_APPLYFILTER,

	STATECHANGE_REMOTE_IDLE,
	STATECHANGE_SERVER,
	STATECHANGE_ENCRYPTION,

	STATECHANGE_SYNC_BROWSE,
	STATECHANGE_COMPARISON,

	STATECHANGE_REMOTE_RECURSION_STATUS,
	STATECHANGE_LOCAL_RECURSION_STATUS,

	STATECHANGE_LOCAL_RECURSION_LISTING,

	/* Global notifications */
	STATECHANGE_QUEUEPROCESSING,
	STATECHANGE_NEWCONTEXT, /* New context created */
	STATECHANGE_CHANGEDCONTEXT, /* Currently active context changed */
	STATECHANGE_REMOVECONTEXT, /* Right before deleting a context */
	STATECHANGE_GLOBALBOOKMARKS,

	STATECHANGE_MAX
};

class CDirectoryListing;
class CFileZillaEngine;
class CCommandQueue;
class CLocalRecursiveOperation;
class CMainFrame;
class CGlobalStateEventHandler;
class CStateEventHandler;
class CRemoteDataObject;
class CRemoteRecursiveOperation;
class CComparisonManager;

class CState;
class CContextManager final
{
	friend class CState;
public:
	// If current_only is set, only notifications from the current (at the time
	// of notification emission) context is dispatched to the handler.
	void RegisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification, bool current_only);
	void UnregisterHandler(CGlobalStateEventHandler* pHandler, t_statechange_notifications notification);

	size_t HandlerCount(t_statechange_notifications notification) const;

	CState* CreateState(CMainFrame &mainFrame);
	void DestroyState(CState* pState);
	void DestroyAllStates();

	CState* GetCurrentContext();
	const std::vector<CState*>* GetAllStates() { return &m_contexts; }

	static CContextManager* Get();

	void NotifyAllHandlers(t_statechange_notifications notification, wxString const& data = wxString(), void const* data2 = 0);
	void NotifyGlobalHandlers(t_statechange_notifications notification, wxString const& data = wxString(), void const* data2 = 0);

	void SetCurrentContext(CState* pState);

	void ProcessDirectoryListing(CServer const& server, std::shared_ptr<CDirectoryListing> const& listing, CState const* exempt);

protected:
	CContextManager();

	std::vector<CState*> m_contexts;
	int m_current_context;

	struct t_handler
	{
		CGlobalStateEventHandler* pHandler;
		bool current_only;
	};
	std::vector<t_handler> m_handlers[STATECHANGE_MAX];

	void NotifyHandlers(CState* pState, t_statechange_notifications notification, wxString const& data, void const* data2);

	static CContextManager m_the_context_manager;
};

class CState final
{
	friend class CCommandQueue;
public:
	CState(CMainFrame& mainFrame);
	~CState();

	CState(CState const&) = delete;
	CState& operator=(CState const&) = delete;

	bool CreateEngine();
	void DestroyEngine();

	CLocalPath GetLocalDir() const;
	bool SetLocalDir(CLocalPath const& dir, std::wstring *error = 0, bool rememberPreviousSubdir = true);
	bool SetLocalDir(std::wstring const& dir, std::wstring *error = 0, bool rememberPreviousSubdir = true);

	bool Connect(Site const& site, const CServerPath& path = CServerPath(), bool compare = false);
	bool Disconnect();

	bool ChangeRemoteDir(CServerPath const& path, std::wstring const& subdir = std::wstring(), int flags = 0, bool ignore_busy = false, bool compare = false);
	bool SetRemoteDir(std::shared_ptr<CDirectoryListing> const& pDirectoryListing, bool modified = false);
	std::shared_ptr<CDirectoryListing> GetRemoteDir() const;
	const CServerPath GetRemotePath() const;

	Site const& GetSite() const;
	CServer const* GetServer() const;
	wxString GetTitle() const;

	void RefreshLocal();
	void RefreshLocalFile(std::wstring const& file);
	void LocalDirCreated(const CLocalPath& path);

	bool RefreshRemote();

	void RegisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification, CStateEventHandler* insertBefore = 0);
	void UnregisterHandler(CStateEventHandler* pHandler, t_statechange_notifications notification);

	CFileZillaEngine* m_pEngine{};
	CCommandQueue* m_pCommandQueue{};
	CComparisonManager* GetComparisonManager() { return m_pComparisonManager; }

	void UploadDroppedFiles(const wxFileDataObject* pFileDataObject, const wxString& subdir, bool queueOnly);
	void UploadDroppedFiles(const wxFileDataObject* pFileDataObject, const CServerPath& path, bool queueOnly);
	void HandleDroppedFiles(const wxFileDataObject* pFileDataObject, const CLocalPath& path, bool copy);
	bool DownloadDroppedFiles(const CRemoteDataObject* pRemoteDataObject, const CLocalPath& path, bool queueOnly = false);

	static bool RecursiveCopy(CLocalPath source, const CLocalPath& targte);

	bool IsRemoteConnected() const;
	bool IsRemoteIdle(bool ignore_recursive = false) const;
	bool IsLocalIdle(bool ignore_recursive = false) const;

	CLocalRecursiveOperation* GetLocalRecursiveOperation() { return m_pLocalRecursiveOperation; }
	CRemoteRecursiveOperation* GetRemoteRecursiveOperation() { return m_pRemoteRecursiveOperation; }

	void NotifyHandlers(t_statechange_notifications notification, wxString const& data = wxString(), const void* data2 = 0);

	bool SuccessfulConnect() const { return m_successful_connect; }
	void SetSuccessfulConnect() { m_successful_connect = true; }

	void ListingFailed(int error);
	void LinkIsNotDir(const CServerPath& path, const wxString& subdir);

	bool SetSyncBrowse(bool enable, const CServerPath& assumed_remote_root = CServerPath());
	bool GetSyncBrowse() const { return !m_sync_browse.local_root.empty(); }

	Site GetLastSite() const { return m_last_site; }
	CServerPath GetLastServerPath() const { return m_last_path; }
	void SetLastSite(Site const& server, CServerPath const& path)
		{ m_last_site = server; m_last_path = path; }

	bool GetSecurityInfo(CCertificateNotification *& pInfo);
	bool GetSecurityInfo(CSftpEncryptionNotification *& pInfo);
	void SetSecurityInfo(CCertificateNotification const& info);
	void SetSecurityInfo(CSftpEncryptionNotification const& info);

	// If the previously selected directory was a direct child of the current directory, this
	// returns the relative name of the subdirectory.
	wxString GetPreviouslyVisitedLocalSubdir() const { return m_previouslyVisitedLocalSubdir; }
	wxString GetPreviouslyVisitedRemoteSubdir() const { return m_previouslyVisitedRemoteSubdir; }
	void ClearPreviouslyVisitedLocalSubdir() { m_previouslyVisitedLocalSubdir = _T(""); }
	void ClearPreviouslyVisitedRemoteSubdir() { m_previouslyVisitedRemoteSubdir = _T(""); }

	void UpdateSite(wxString const& oldPath, Site const& newSite);

protected:
	void SetSite(Site const& site, CServerPath const& path = CServerPath());

	void UpdateTitle();

	CLocalPath m_localDir;
	std::shared_ptr<CDirectoryListing> m_pDirectoryListing;

	Site m_site;

	wxString m_title;
	bool m_successful_connect{};

	Site m_last_site;
	CServerPath m_last_path;

	CMainFrame& m_mainFrame;

	CLocalRecursiveOperation* m_pLocalRecursiveOperation;
	CRemoteRecursiveOperation* m_pRemoteRecursiveOperation;

	CComparisonManager* m_pComparisonManager;

	struct t_handler
	{
		CStateEventHandler* pHandler;
	};
	std::vector<t_handler> m_handlers[STATECHANGE_MAX];

	CLocalPath GetSynchronizedDirectory(CServerPath remote_path);
	CServerPath GetSynchronizedDirectory(CLocalPath local_path);

	struct _sync_browse
	{
		CLocalPath local_root;
		CServerPath remote_root;

		// The target path when changing remote directory
		CServerPath target_path;
	} m_sync_browse;

	struct _post_setdir
	{
		bool compare{};
		bool syncbrowse{};
	} m_changeDirFlags;

	std::unique_ptr<CCertificateNotification> m_pCertificate;
	std::unique_ptr<CSftpEncryptionNotification> m_pSftpEncryptionInfo;

	wxString m_previouslyVisitedLocalSubdir;
	wxString m_previouslyVisitedRemoteSubdir;
};

class CGlobalStateEventHandler
{
public:
	CGlobalStateEventHandler() = default;
	virtual ~CGlobalStateEventHandler();

	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, const wxString& data, const void* data2) = 0;
};

class CStateEventHandler
{
public:
	CStateEventHandler(CState& state);
	virtual ~CStateEventHandler();

	CState& m_state;

	virtual void OnStateChange(t_statechange_notifications notification, const wxString& data, const void* data2) = 0;
};

#endif
