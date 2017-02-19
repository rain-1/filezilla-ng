#ifndef FILEZILLA_ENGINE_SFTP_SFTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_SFTP_SFTPCONTROLSOCKET_HEADER

#include "ControlSocket.h"

namespace fz {
class process;
}

class CSftpInputThread;

struct sftp_message;
class CSftpControlSocket final : public CControlSocket, public CRateLimiterObject
{
public:
	CSftpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CSftpControlSocket();

	virtual void Connect(CServer const& server) override;
	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0) override;
	virtual int Delete(const CServerPath& path, std::deque<std::wstring>&& files) override;
	virtual int RemoveDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring());
	virtual int Mkdir(const CServerPath& path);
	virtual int Rename(const CRenameCommand& command);
	virtual int Chmod(const CChmodCommand& command);
	virtual void Cancel();

	virtual bool Connected() const override { return input_thread_.operator bool(); }

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification);

protected:
	// Replaces filename"with"quotes with
	// "filename""with""quotes"
	std::wstring QuoteFilename(std::wstring const& filename);

	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED) override;

	virtual int ResetOperation(int nErrorCode) override;

	void ProcessReply(int result, std::wstring const& reply);

	virtual int FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
							 std::wstring const& remoteFile, bool download,
							 CFileTransferCommand::t_transferSettings const& transferSettings);
	int FileTransferSubcommandResult(int prevResult);
	int FileTransferSend();
	int FileTransferParseResponse(int result, std::wstring const& reply);

	int ChangeDir(CServerPath path = CServerPath(), std::wstring subDir = std::wstring(), bool link_discovery = false);
	int ChangeDirParseResponse(bool successful, std::wstring const& reply);
	int ChangeDirSubcommandResult(int prevResult);
	int ChangeDirSend();

	int MkdirParseResponse(bool successful, std::wstring const& reply);
	int MkdirSend();

	int DeleteParseResponse(bool successful, std::wstring const& reply);
	int DeleteSend();

	int RemoveDirParseResponse(bool successful, std::wstring const& reply);

	int ChmodParseResponse(bool successful, std::wstring const& reply);
	int ChmodSubcommandResult(int prevResult);
	int ChmodSend();

	int RenameParseResponse(bool successful, std::wstring const& reply);
	int RenameSubcommandResult(int prevResult);
	int RenameSend();

	int SendCommand(std::wstring const& cmd, std::wstring const& show = std::wstring());
	int AddToStream(std::wstring const& cmd, bool force_utf8 = false);

	virtual void OnRateAvailable(CRateLimiter::rate_direction direction);
	void OnQuotaRequest(CRateLimiter::rate_direction direction);

	// see src/putty/wildcard.c
	std::wstring WildcardEscape(std::wstring const& file);

	std::unique_ptr<fz::process> process_;
	std::unique_ptr<CSftpInputThread> input_thread_;

	virtual void operator()(fz::event_base const& ev);
	void OnSftpEvent(sftp_message const& message);
	void OnTerminate(std::wstring const& error);

	std::wstring m_requestPreamble;
	std::wstring m_requestInstruction;

	CSftpEncryptionNotification m_sftpEncryptionDetails;

	int result_;
	std::wstring response_;

	friend class CSftpConnectOpData;
	friend class CSftpListOpData;
	friend class CSftpOpData;
};


class CSftpOpData
{
public:
	CSftpOpData(CSftpControlSocket & controlSocket)
		: controlSocket_(controlSocket)
		, engine_(controlSocket.engine_)
		, currentServer_(controlSocket.currentServer_)
	{
	}

	virtual ~CSftpOpData() = default;

	template<typename...Args>
	void LogMessage(Args&& ...args) const {
		controlSocket_.LogMessage(std::forward<Args>(args)...);
	}

	CSftpControlSocket & controlSocket_;
	CFileZillaEnginePrivate & engine_;
	CServer & currentServer_;
};

#endif
