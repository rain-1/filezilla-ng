#ifndef __QUEUEVIEW_H__
#define __QUEUEVIEW_H__

#include "dndobjects.h"
#include "queue.h"

#include <libfilezilla_engine.h>
#include <option_change_event_handler.h>

#include <set>
#include <wx/progdlg.h>

#include "queue_storage.h"
#include "local_recursive_operation.h"
#include "notification.h"


namespace ActionAfterState {
enum type {
	None,
	ShowNotification,
	RequestAttention,
	Close,
	RunCommand,
	PlaySound,

	// On Windows and OS X, wx can reboot or shutdown the system as well.
	Reboot,
	Shutdown,
	Sleep,

	Count
};
}

class CStatusLineCtrl;
class CFileItem;
struct t_EngineData final
{
	t_EngineData()
		: pEngine()
		, active()
		, transient()
		, state(t_EngineData::none)
		, pItem()
		, pStatusLineCtrl()
		, m_idleDisconnectTimer()
	{
	}

	~t_EngineData()
	{
		wxASSERT(!active);
		if (!transient)
			delete pEngine;
		delete m_idleDisconnectTimer;
	}

	CFileZillaEngine* pEngine;
	bool active;
	bool transient;

	enum EngineDataState
	{
		none,
		cancel,
		disconnect,
		connect,
		transfer,
		list,
		mkdir,
		askpassword,
		waitprimary
	} state;

	CFileItem* pItem;
	CServer lastServer;
	CStatusLineCtrl* pStatusLineCtrl;
	wxTimer* m_idleDisconnectTimer;
};

class CMainFrame;
class CStatusLineCtrl;
class CAsyncRequestQueue;
class CQueue;
#if WITH_LIBDBUS
class CDesktopNotification;
#elif defined(__WXGTK__) || defined(__WXMSW__)
class wxNotificationMessage;
#endif

class CActionAfterBlocker final
{
public:
	CActionAfterBlocker(CQueueView& queueView)
		: queueView_(queueView)
	{
	}

	~CActionAfterBlocker();

private:
	friend class CQueueView;
	CQueueView& queueView_;
	bool trigger_{};
};

class CQueueView final : public CQueueViewBase, public COptionChangeEventHandler, private EngineNotificationHandler
{
	friend class CQueueViewDropTarget;
	friend class CQueueViewFailed;
	friend class CActionAfterBlocker;

public:
	CQueueView(CQueue* parent, int index, CMainFrame* pMainFrame, CAsyncRequestQueue* pAsyncRequestQueue);
	virtual ~CQueueView();

	bool QueueFile(const bool queueOnly, const bool download,
		std::wstring const& localFile, std::wstring const& remoteFile,
		const CLocalPath& localPath, const CServerPath& remotePath,
		const CServer& server, int64_t size, CEditHandler::fileType edit = CEditHandler::none,
		QueuePriority priority = QueuePriority::normal);

	void QueueFile_Finish(const bool start); // Need to be called after QueueFile
	bool QueueFiles(const bool queueOnly, CLocalPath const& localPath, const CRemoteDataObject& dataObject);
	bool QueueFiles(const bool queueOnly, CServer const& server, CLocalRecursiveOperation::listing const& listing);

	bool empty() const;
	int IsActive() const { return m_activeMode; }
	bool SetActive(bool active = true);
	bool Quit();

	// This sets the default file exists action for all files currently in queue.
	void SetDefaultFileExistsAction(CFileExistsNotification::OverwriteAction action, const TransferDirection direction);

	void UpdateItemSize(CFileItem* pItem, int64_t size);

	void RemoveAll();

	void LoadQueue();
	void LoadQueueFromXML();
	void ImportQueue(pugi::xml_node element, bool updateSelections);

	virtual void InsertItem(CServerItem* pServerItem, CQueueItem* pItem);

	virtual void CommitChanges();

	void ProcessNotification(CFileZillaEngine* pEngine, std::unique_ptr<CNotification>&& pNotification);

	void RenameFileInTransfer(CFileZillaEngine *pEngine, const wxString& newName, bool local);

	static std::wstring ReplaceInvalidCharacters(std::wstring const& filename);

	// Get the current download speed as the sum of all active downloads.
	// Unit is byte/s.
	wxFileOffset GetCurrentDownloadSpeed();

	// Get the current upload speed as the sum of all active uploads.
	// Unit is byte/s.
	wxFileOffset GetCurrentUploadSpeed();

	std::shared_ptr<CActionAfterBlocker> GetActionAfterBlocker();

protected:

#ifdef __WXMSW__
	WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam);
#endif

	virtual void OnOptionsChanged(changed_options_t const& options);

	void AdvanceQueue(bool refresh = true);
	bool TryStartNextTransfer();

	// Called from TryStartNextTransfer(), checks
	// whether it is allowed to start another transfer on that server item
	bool CanStartTransfer(const CServerItem& server_item, t_EngineData *&pEngineData);

	void ProcessReply(t_EngineData* pEngineData, COperationNotification const& notification);
	void SendNextCommand(t_EngineData& engineData);

	enum ResetReason
	{
		success,
		failure,
		reset,
		retry,
		remove
	};

	void ResetEngine(t_EngineData& data, const ResetReason reason);
	void DeleteEngines();

	virtual bool RemoveItem(CQueueItem* item, bool destroy, bool updateItemCount = true, bool updateSelections = true, bool forward = true);

	// Stops processing of given item
	// Returns true on success, false if it would block
	bool StopItem(CFileItem* item);
	bool StopItem(CServerItem* pServerItem, bool updateSelections);

	void CheckQueueState();
	bool IncreaseErrorCount(t_EngineData& engineData);
	void UpdateStatusLinePositions();
	void CalculateQueueSize();
	void DisplayQueueSize();
	void SaveQueue();

	bool IsActionAfter(ActionAfterState::type);
	void ActionAfter(bool warned = false);
#if defined(__WXMSW__) || defined(__WXMAC__)
	void ActionAfterWarnUser(ActionAfterState::type s);
#endif

	void ProcessNotification(t_EngineData* pEngineData, std::unique_ptr<CNotification> && pNotification);

	// Tries to refresh the current remote directory listing
	// if there's an idle engine connected to the current server of
	// the primary connection.
	void TryRefreshListings();
	CServer m_last_refresh_server;
	CServerPath m_last_refresh_path;
	fz::monotonic_clock m_last_refresh_listing_time;

	// Called from Process Reply.
	// After a disconnect, check if there's another idle engine that
	// is already connected.
	bool SwitchEngine(t_EngineData** ppEngineData);

	bool IsOtherEngineConnected(t_EngineData* pEngineData);

	t_EngineData* GetIdleEngine(const CServer* pServer = 0, bool allowTransient = false);
	t_EngineData* GetEngineData(const CFileZillaEngine* pEngine);

	std::vector<t_EngineData*> m_engineData;
	std::list<CStatusLineCtrl*> m_statusLineList;

	/*
	 * Don't update status line positions if m_waitStatusLineUpdate is true.
	 * This assures we are updating the status line positions only once,
	 * and not multiple times (for example inside a loop).
	 */
	bool m_waitStatusLineUpdate{};

	// Remember last top item in UpdateStatusLinePositions()
	int m_lastTopItem{-1};

	int m_activeCount{};
	int m_activeCountDown{};
	int m_activeCountUp{};
	int m_activeMode{}; // 0 inactive, 1 only immediate transfers, 2 all
	int m_quit{};

	ActionAfterState::type m_actionAfterState;
#if defined(__WXMSW__) || defined(__WXMAC__)
	wxTimer* m_actionAfterTimer{};
	wxProgressDialog* m_actionAfterWarnDialog{};
	int m_actionAfterTimerCount{};
	int m_actionAfterTimerId{-1};
#endif

	int64_t m_totalQueueSize{};
	int m_filesWithUnknownSize{};

	CMainFrame* m_pMainFrame;
	CAsyncRequestQueue* m_pAsyncRequestQueue;

	std::list<CFileZillaEngine*> m_waitingForPassword;

	virtual void OnPostScroll();

	int GetLineHeight();
	int m_line_height;
#ifdef __WXMSW__
	int m_header_height;
#endif

	wxTimer m_resize_timer;

	void ReleaseExclusiveEngineLock(CFileZillaEngine* pEngine);

#if WITH_LIBDBUS
	std::unique_ptr<CDesktopNotification> m_desktop_notification;
#elif defined(__WXGTK__) || defined(__WXMSW__)
	std::unique_ptr<wxNotificationMessage> m_desktop_notification;
#endif

	CQueueStorage m_queue_storage;

	// Get the current transfer speed.
	// Unit is byte/s.
	wxFileOffset GetCurrentSpeed(bool countDownload, bool countUpload);

	virtual void OnEngineEvent(CFileZillaEngine* engine);
	void DoOnEngineEvent(CFileZillaEngine* engine);

	void OnAskPassword();

	std::weak_ptr<CActionAfterBlocker> m_actionAfterBlocker;

	DECLARE_EVENT_TABLE()
	void OnChar(wxKeyEvent& event);

	// Context menu handlers
	void OnContextMenu(wxContextMenuEvent& event);
	void OnProcessQueue(wxCommandEvent& event);
	void OnStopAndClear(wxCommandEvent& event);
	void OnRemoveSelected(wxCommandEvent& event);
	void OnSetDefaultFileExistsAction(wxCommandEvent& event);

	void OnTimer(wxTimerEvent& evnet);

	void OnSetPriority(wxCommandEvent& event);

	void OnExclusiveEngineRequestGranted(wxCommandEvent& event);

	void OnActionAfter(wxCommandEvent& event);
	void OnActionAfterTimerTick();

	void OnSize(wxSizeEvent& event);
};

#endif
