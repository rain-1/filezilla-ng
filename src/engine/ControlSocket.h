#ifndef FILEZILLA_ENGINE_CONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_CONTROLSOCKET_HEADER

#include "socket.h"
#include "logging_private.h"

class COpData
{
public:
	explicit COpData(Command op_Id)
		: opId(op_Id)
	{}

	virtual ~COpData() = default;

	COpData(COpData const&) = delete;
	COpData& operator=(COpData const&) = delete;

	// Functions here must return one of '4' values:
	// - FZ_REPLY_OK, operation succeeded
	// - FZ_REPLY_ERROR (possibly with flags)
	// - FZ_REPLY_WOULDBLOCK, waiting on some exvent
	// - FZ_REPLY_CONTINUE, caller should issue the next command

	virtual int Send() = 0;
	virtual int ParseResponse() = 0;

	virtual int SubcommandResult(int, COpData const&) { return FZ_REPLY_INTERNALERROR; }

	int opState{};
	Command const opId;

	bool waitForAsyncRequest{};
	bool holdsLock_{};
};

template<typename T>
class CProtocolOpData
{
public:
	CProtocolOpData(T & controlSocket)
		: controlSocket_(controlSocket)
		, engine_(controlSocket.engine_)
		, currentServer_(controlSocket.currentServer_)
		, currentPath_(controlSocket.currentPath_)
	{
	}

	virtual ~CProtocolOpData() = default;

	template<typename...Args>
	void LogMessage(Args&& ...args) const {
		controlSocket_.LogMessage(std::forward<Args>(args)...);
	}

	T & controlSocket_;
	CFileZillaEnginePrivate & engine_;
	CServer & currentServer_;
	CServerPath& currentPath_;
};

class CNotSupportedOpData : public COpData
{
public:
	CNotSupportedOpData()
		: COpData(Command::none)
	{}

	virtual int Send() { return FZ_REPLY_NOTSUPPORTED; }
	virtual int ParseResponse() { return FZ_REPLY_INTERNALERROR; }
};

class CConnectOpData : public COpData
{
public:
	CConnectOpData(CServer const& server)
		: COpData(Command::connect)
		, server_(server)
	{
	}

	// What to connect the socket to,
	// can be different from server_ if using
	// a proxy
	std::wstring host_;
	unsigned int port_{};

	// Target server
	CServer server_;
};

class CFileTransferOpData : public COpData
{
public:
	CFileTransferOpData(bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path);

	// Transfer data
	std::wstring localFile_, remoteFile_;
	CServerPath remotePath_;
	bool const download_;

	fz::datetime fileTime_;
	int64_t localFileSize_{-1};
	int64_t remoteFileSize_{-1};

	bool tryAbsolutePath_{};
	bool resume_{};

	CFileTransferCommand::t_transferSettings transferSettings_;

	// Set to true when sending the command which
	// starts the actual transfer
	bool transferInitiated_{};
};

class CMkdirOpData : public COpData
{
public:
	CMkdirOpData()
		: COpData(Command::mkdir)
	{
	}

	CServerPath path_;
	CServerPath currentMkdPath_;
	CServerPath commonParent_;
	std::vector<std::wstring> segments_;
};

class CChangeDirOpData : public COpData
{
public:
	CChangeDirOpData()
		: COpData(Command::cwd)
	{
	}

	CServerPath path_;
	std::wstring subDir_;
	bool tryMkdOnFail_{};
	CServerPath target_;

	bool link_discovery_{};
};

enum class TransferEndReason
{
	none,
	successful,
	timeout,
	transfer_failure,					// Error during transfer, like lost connection. Retry automatically
	transfer_failure_critical,			// Error during transfer like lack of diskspace. Needs user interaction
	pre_transfer_command_failure,		// If a command fails prior to sending the transfer command
	transfer_command_failure_immediate,	// Used if server does not send the 150 reply after the transfer command
	transfer_command_failure,			// Used if the transfer command fails, but after receiving a 150 first
	failure,							// Other unspecific failure
	failed_resumetest
};

class CBackend;
class CTransferStatus;
class CControlSocket: public CLogging, public fz::event_handler
{
public:
	CControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CControlSocket();

	CControlSocket(CControlSocket const&) = delete;
	CControlSocket& operator=(CControlSocket const&) = delete;

	virtual int Disconnect();

	virtual void Cancel();

	// Implicit FZ_REPLY_CONTINUE
	virtual void Connect(CServer const& server) = 0;
	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0);

	virtual void FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
							 std::wstring const& remoteFile, bool download,
							 CFileTransferCommand::t_transferSettings const& transferSettings) = 0;
	virtual void RawCommand(std::wstring const& command = std::wstring());
	virtual void Delete(CServerPath const& path, std::deque<std::wstring>&& files);
	virtual void RemoveDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring());
	virtual void Mkdir(CServerPath const& path);
	virtual void Rename(CRenameCommand const& command);
	virtual void Chmod(CChmodCommand const& command);

	virtual bool Connected() const = 0;

	// If m_pCurrentOpData is zero, this function returns the current command
	// from the engine.
	Command GetCurrentCommandId() const;

	void SendAsyncRequest(CAsyncRequestNotification* pNotification);
	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) = 0;
	bool SetFileExistsAction(CFileExistsNotification *pFileExistsNotification);

	CServer const& GetCurrentServer() const;

	// Conversion function which convert between local and server charset.
	std::wstring ConvToLocal(char const* buffer, size_t len);
	std::string ConvToServer(std::wstring const&, bool force_utf8 = false);

	void SetActive(CFileZillaEngine::_direction direction);

	// ---
	// The following two functions control the timeout behaviour:
	// ---

	// Call this if data could be sent or retrieved
	void SetAlive();

	// Set to true if waiting for data
	void SetWait(bool waiting);

	CFileZillaEnginePrivate& GetEngine() { return engine_; }

	// Only called from the engine, see there for description
	void InvalidateCurrentWorkingDir(const CServerPath& path);

	virtual bool CanSendNextCommand() const { return true; }
	int SendNextCommand();

protected:
	void SendDirectoryListingNotification(CServerPath const& path, bool onList, bool failed);

	fz::duration GetTimezoneOffset() const;

	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR);
	bool m_closed{};

	virtual int ResetOperation(int nErrorCode);

	void LogTransferResultMessage(int nErrorCode, CFileTransferOpData *pData);

	// Called by ResetOperation if there's a queued operation
	int ParseSubcommandResult(int prevResult, COpData const& previousOperation);

	std::wstring ConvertDomainName(std::wstring const& domain);

	int CheckOverwriteFile();

	void CreateLocalDir(std::wstring const& local_file);

	bool ParsePwdReply(std::wstring reply, bool unquoted = false, const CServerPath& defaultPath = CServerPath());

	void Push(std::unique_ptr<COpData> && pNewOpData);

	std::vector<std::unique_ptr<COpData>> operations_;
	CFileZillaEnginePrivate & engine_;
	CServer currentServer_;

	CServerPath currentPath_;

	bool m_useUTF8{};

	// Timeout data
	fz::timer_id m_timer{};
	fz::monotonic_clock m_lastActivity;

	// -------------------------
	// Begin cache locking stuff
	// -------------------------

	enum locking_reason
	{
		lock_unknown = -1,
		lock_list,
		lock_mkdir
	};

	// Tries to obtain lock. Returns true on success.
	// On failure, caller has to pass control.
	// SendNextCommand will be called once the lock gets available
	// and engine could obtain it.
	// Lock is recursive. Lock counter increases on suboperations.
	bool TryLockCache(locking_reason reason, CServerPath const& directory);
	bool IsLocked(locking_reason reason, CServerPath const& directory);

	// Unlocks the cache. Can be called if not holding the lock
	// Doesn't need reason as one engine can at most hold one lock
	void UnlockCache();

	// Called from the fzOBTAINLOCK event.
	// Returns reason != unknown iff engine is the first waiting engine
	// and obtains the lock.
	// On failure, the engine was not waiting for a lock.
	locking_reason ObtainLockFromEvent();

	bool IsWaitingForLock();

	struct t_lockInfo
	{
		CControlSocket* pControlSocket;
		CServerPath directory;
		locking_reason reason;
		bool waiting;
		int lockcount;
	};
	static std::list<t_lockInfo> m_lockInfoList;

	const std::list<t_lockInfo>::iterator GetLockStatus();

	// -----------------------
	// End cache locking stuff
	// -----------------------

	bool m_invalidateCurrentPath{};

	virtual void operator()(fz::event_base const& ev);

	void OnTimer(fz::timer_id id);
	void OnObtainLock();
};

class CProxySocket;
class CRealControlSocket : public CControlSocket
{
public:
	CRealControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CRealControlSocket();

	int DoConnect(CServer const& server);
	virtual int ContinueConnect();

	virtual bool Connected() const override;

protected:
	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR) override;
	virtual void ResetSocket();

	virtual void operator()(fz::event_base const& ev) override;
	void OnSocketEvent(CSocketEventSource* source, SocketEventType t, int error);
	void OnHostAddress(CSocketEventSource* source, std::string const& address);

	virtual void OnConnect();
	virtual void OnReceive();
	virtual int OnSend();
	virtual void OnClose(int error);

	int Send(unsigned char const* buffer, unsigned int len);
	int Send(char const* buffer, unsigned int len) {
		return Send(reinterpret_cast<unsigned char const*>(buffer), len);
	}

	CSocket* m_pSocket;

	CBackend* m_pBackend;
	CProxySocket* m_pProxyBackend{};

	void SendBufferReserve(unsigned int len);
	void AppendToSendBuffer(unsigned char const* data, unsigned int len);
	unsigned char* sendBuffer_{};
	unsigned int sendBufferCapacity_{};
	unsigned int sendBufferPos_{};
	unsigned int sendBufferSize_{};
};

#endif
