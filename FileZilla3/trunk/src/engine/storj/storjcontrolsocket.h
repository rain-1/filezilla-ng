#ifndef FILEZILLA_ENGINE_STORJCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_STORJCONTROLSOCKET_HEADER

#include "ControlSocket.h"

#include "backend.h"

namespace fz {
class process;
}

namespace PrivCommand {
auto const resolve = Command::private1;
}

class CStorjInputThread;

struct storj_message;
class CStorjControlSocket final : public CControlSocket, public CRateLimiterObject
{
public:
	CStorjControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CStorjControlSocket();

	virtual void Connect(CServer const &server, Credentials const& credentials) override;

	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0) override;
	virtual void FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
							 std::wstring const& remoteFile, bool download,
							 CFileTransferCommand::t_transferSettings const& transferSettings) override;
	void Resolve(CServerPath const& path, std::wstring const& file, std::wstring & bucket, std::wstring * fileId = 0, bool ignore_missing_file = false);
	void Resolve(CServerPath const& path, std::deque<std::wstring> const& files, std::wstring & bucket, std::deque<std::wstring> & fileIds);
	virtual void Delete(CServerPath const& path, std::deque<std::wstring>&& files) override;
	virtual void Mkdir(const CServerPath& path) override;
	virtual void RemoveDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring()) override;
	/*virtual void Rename(const CRenameCommand& command) override;*/
	virtual void Cancel() override;

	virtual bool Connected() const override { return input_thread_.operator bool(); }

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;

protected:
	// Replaces filename"with"quotes with
	// "filename""with""quotes"
	std::wstring QuoteFilename(std::wstring const& filename);

	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED) override;

	virtual int ResetOperation(int nErrorCode) override;

	void ProcessReply(int result, std::wstring const& reply);

	int SendCommand(std::wstring const& cmd, std::wstring const& show = std::wstring());
	int AddToStream(std::wstring const& cmd);

	virtual void OnRateAvailable(CRateLimiter::rate_direction direction) override;
	void OnQuotaRequest(CRateLimiter::rate_direction direction);

	std::unique_ptr<fz::process> process_;
	std::unique_ptr<CStorjInputThread> input_thread_;

	virtual void operator()(fz::event_base const& ev) override;
	void OnStorjEvent(storj_message const& message);
	void OnTerminate(std::wstring const& error);

	int result_{};
	std::wstring response_;

	friend class CProtocolOpData<CStorjControlSocket>;
	friend class CStorjConnectOpData;
	friend class CStorjDeleteOpData;
	friend class CStorjFileTransferOpData;
	friend class CStorjListOpData;
	friend class CStorjMkdirOpData;
	friend class CStorjRemoveDirOpData;
	friend class CStorjResolveOpData;
	friend class CStorjResolveManyOpData;
};

typedef CProtocolOpData<CStorjControlSocket> CStorjOpData;

#endif
