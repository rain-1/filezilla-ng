#ifndef FILEZILLA_ENGINEPRIVATE_HEADER
#define FILEZILLA_ENGINEPRIVATE_HEADER

#include <libfilezilla/event.hpp>
#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/mutex.hpp>
#include <libfilezilla/time.hpp>

#include "engine_context.h"
#include "FileZillaEngine.h"
#include "option_change_event_handler.h"

#include <atomic>

class CControlSocket;
class CLogging;
class CRateLimiter;

enum EngineNotificationType
{
	engineCancel,
};

struct filezilla_engine_event_type;
typedef fz::simple_event<filezilla_engine_event_type, EngineNotificationType> CFileZillaEngineEvent;

class CTransferStatusManager final
{
public:
	CTransferStatusManager(CFileZillaEnginePrivate& engine);

	CTransferStatusManager(CTransferStatusManager const&) = delete;
	CTransferStatusManager& operator=(CTransferStatusManager const&) = delete;

	bool empty();

	void Init(int64_t totalSize, int64_t startOffset, bool list);
	void Reset();
	void SetStartTime();
	void SetMadeProgress();
	void Update(int64_t transferredBytes);

	CTransferStatus Get(bool &changed);

protected:
	fz::mutex mutex_;

	CTransferStatus status_;
	std::atomic<int64_t> currentOffset_{};
	int send_state_{};

	CFileZillaEnginePrivate& engine_;
};

class CFileZillaEnginePrivate final : public fz::event_handler, COptionChangeEventHandler
{
public:
	CFileZillaEnginePrivate(CFileZillaEngineContext& engine_context, CFileZillaEngine& parent, EngineNotificationHandler& notificationHandler);
	virtual ~CFileZillaEnginePrivate();

	int Execute(CCommand const& command);
	int Cancel();
	int ResetOperation(int nErrorCode);

	const CCommand *GetCurrentCommand() const;
	Command GetCurrentCommandId() const;

	bool IsBusy() const;
	bool IsConnected() const;

	bool IsPendingAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> const& pNotification);
	bool SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification);
	unsigned int GetNextAsyncRequestNumber();

	CTransferStatus GetTransferStatus(bool &changed);

	int CacheLookup(CServerPath const& path, CDirectoryListing& listing);

	static bool IsActive(CFileZillaEngine::_direction direction);
	void SetActive(int direction);

	// Add new pending notification
	void AddNotification(fz::scoped_lock& lock, CNotification *pNotification); // note: Unlocks the mutex!
	void AddNotification(CNotification *pNotification);
	void AddLogNotification(CLogmsgNotification *pNotification);
	std::unique_ptr<CNotification> GetNextNotification();

	COptionsBase& GetOptions() { return m_options; }
	CRateLimiter& GetRateLimiter() { return m_rateLimiter; }
	CDirectoryCache& GetDirectoryCache() { return directory_cache_; }
	CPathCache& GetPathCache() { return path_cache_; }
	fz::thread_pool& GetThreadPool() { return thread_pool_; }

	// If deleting or renaming a directory, it could be possible that another
	// engine's CControlSocket instance still has that directory as
	// current working directory (m_CurrentPath)
	// Since this would cause problems, this function interate over all engines
	// connected ot the same server and invalidates the current working
	// directories if they match or if it is a subdirectory of the changed
	// directory.
	void InvalidateCurrentWorkingDirs(const CServerPath& path);

	unsigned int GetEngineId() const {return m_engine_id; }

	CTransferStatusManager transfer_status_;

	CustomEncodingConverterBase const& GetEncodingConverter() const { return encoding_converter_; }
protected:
	virtual void OnOptionsChanged(changed_options_t const& options);

	void SendQueuedLogs(bool reset_flag = false);
	void ClearQueuedLogs(bool reset_flag);
	void ClearQueuedLogs(fz::scoped_lock& lock, bool reset_flag);
	bool ShouldQueueLogsFromOptions() const;

	int CheckCommandPreconditions(CCommand const& command, bool checkBusy);


	bool CheckAsyncRequestReplyPreconditions(std::unique_ptr<CAsyncRequestNotification> const& reply);
	void OnSetAsyncRequestReplyEvent(std::unique_ptr<CAsyncRequestNotification> const& reply);

	// Command handlers, only called by CFileZillaEngine::Command
	int Connect(CConnectCommand const& command);
	int Disconnect(CDisconnectCommand const& command);
	int List(CListCommand const&command);
	int FileTransfer(CFileTransferCommand const& command);
	int RawCommand(CRawCommand const& command);
	int Delete(CDeleteCommand& command);
	int RemoveDir(CRemoveDirCommand const& command);
	int Mkdir(CMkdirCommand const& command);
	int Rename(CRenameCommand const& command);
	int Chmod(CChmodCommand const& command);

	void DoCancel();

	int ContinueConnect();

	void operator()(fz::event_base const& ev);
	void OnEngineEvent(EngineNotificationType type);
	void OnTimer(fz::timer_id);
	void OnCommandEvent();

	// General mutex for operations on the engine
	// Todo: More fine-grained locking, a global mutex isn't nice
	static fz::mutex mutex_;

	// Used to synchronize access to the notification list
	fz::mutex notification_mutex_{false};

	EngineNotificationHandler& notification_handler_;

	unsigned int const m_engine_id;

	static std::vector<CFileZillaEnginePrivate*> m_engineList;

	// Indicicates if data has been received/sent and whether to send any notifications
	static std::atomic_int m_activeStatus[2];

	std::unique_ptr<CControlSocket> m_pControlSocket;

	std::unique_ptr<CCommand> m_pCurrentCommand;

	// Protect access to these three with notification_mutex_
	std::list<CNotification*> m_NotificationList;
	bool m_maySendNotificationEvent{true};
	unsigned int m_asyncRequestCounter{};

	bool m_bIsInCommand{}; //true if Command is on the callstack
	int m_nControlSocketError{};

	COptionsBase& m_options;

	CLogging* m_pLogging;

	// Everything related to the retry code
	// ------------------------------------

	void RegisterFailedLoginAttempt(const CServer& server, bool critical);

	// Get the amount of time to wait till next reconnection attempt
	fz::duration GetRemainingReconnectDelay(CServer const& server);

	struct t_failedLogins final
	{
		CServer server;
		fz::monotonic_clock time;
		bool critical{};
	};
	static std::list<t_failedLogins> m_failedLogins;
	int m_retryCount{};
	fz::timer_id m_retryTimer{};

	CRateLimiter& m_rateLimiter;
	CDirectoryCache& directory_cache_;
	CPathCache& path_cache_;

	CFileZillaEngine& parent_;

	bool queue_logs_{true};
	std::deque<CLogmsgNotification*> queued_logs_;

	fz::thread_pool & thread_pool_;

	CustomEncodingConverterBase const& encoding_converter_;
};

struct command_event_type{};
typedef fz::simple_event<command_event_type> CCommandEvent;

struct async_request_reply_event_type{};
typedef fz::simple_event<async_request_reply_event_type, std::unique_ptr<CAsyncRequestNotification>> CAsyncRequestReplyEvent;

#endif
