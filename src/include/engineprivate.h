#ifndef __FILEZILLAENGINEPRIVATE_H__
#define __FILEZILLAENGINEPRIVATE_H__

#include "timeex.h"

#include "engine_context.h"
#include "event.h"
#include "event_handler.h"

class CControlSocket;
class CLogging;
class CRateLimiter;
class CSocketEventDispatcher;

enum EngineNotificationType
{
	engineCancel,
	engineTransferEnd
};

struct filezilla_engine_event_type;
typedef CEvent<filezilla_engine_event_type, EngineNotificationType> CFileZillaEngineEvent;

class CFileZillaEnginePrivate : public CEventHandler
{
public:
	int ResetOperation(int nErrorCode);
	void SetActive(int direction);

	// Add new pending notification
	void AddNotification(CNotification *pNotification);

	unsigned int GetNextAsyncRequestNumber();

	bool IsBusy() const;
	bool IsConnected() const;

	const CCommand *GetCurrentCommand() const;
	Command GetCurrentCommandId() const;

	COptionsBase& GetOptions() { return m_options; }
	CRateLimiter& GetRateLimiter() { return m_rateLimiter; }
	CDirectoryCache& GetDirectoryCache() { return directory_cache_; }
	CPathCache& GetPathCache() { return path_cache_; }

	void SendDirectoryListingNotification(const CServerPath& path, bool onList, bool modified, bool failed);

	// If deleting or renaming a directory, it could be possible that another
	// engine's CControlSocket instance still has that directory as
	// current working directory (m_CurrentPath)
	// Since this would cause problems, this function interate over all engines
	// connected ot the same server and invalidates the current working
	// directories if they match or if it is a subdirectory of the changed
	// directory.
	void InvalidateCurrentWorkingDirs(const CServerPath& path);

	int GetEngineId() const {return m_engine_id; }

	CEventLoop& event_loop_;
	CSocketEventDispatcher& socket_event_dispatcher_;

protected:
	CFileZillaEnginePrivate(CFileZillaEngineContext& engine_context);
	virtual ~CFileZillaEnginePrivate();

	// Command handlers, only called by CFileZillaEngine::Command
	int Connect(const CConnectCommand &command);
	int Disconnect(const CDisconnectCommand &command);
	int Cancel(const CCancelCommand &command);
	int List(const CListCommand &command);
	int FileTransfer(const CFileTransferCommand &command);
	int RawCommand(const CRawCommand& command);
	int Delete(const CDeleteCommand& command);
	int RemoveDir(const CRemoveDirCommand& command);
	int Mkdir(const CMkdirCommand& command);
	int Rename(const CRenameCommand& command);
	int Chmod(const CChmodCommand& command);

	int ContinueConnect();

	void operator()(CEventBase const& ev);
	void OnEngineEvent(EngineNotificationType type);
	void OnTimer(int timer_id);

	// General mutex for operations on the engine
	// Todo: More fine-grained locking, a global mutex isn't nice
	static wxCriticalSection mutex_;

	// Used to synchronize access to the notification list
	wxCriticalSection notification_mutex_;

	wxEvtHandler *m_pEventHandler{};

	int m_engine_id;

	static std::list<CFileZillaEnginePrivate*> m_engineList;

	// Indicicates if data has been received/sent and whether to send any notifications
	static int m_activeStatus[2];

	// Remember last path used in a dirlisting.
	CServerPath m_lastListDir;
	CMonotonicTime m_lastListTime;

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

	// Get the amount of time to wait till next reconnection attempt in milliseconds
	unsigned int GetRemainingReconnectDelay(const CServer& server);

	struct t_failedLogins final
	{
		CServer server;
		wxDateTime time;
		bool critical{};
	};
	static std::list<t_failedLogins> m_failedLogins;
	int m_retryCount{};
	int m_retryTimer{-1};

	CRateLimiter& m_rateLimiter;
	CDirectoryCache& directory_cache_;
	CPathCache& path_cache_;
};

#endif //__FILEZILLAENGINEPRIVATE_H__
