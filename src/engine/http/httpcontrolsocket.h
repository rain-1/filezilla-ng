#ifndef FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER

#include "ControlSocket.h"
#include "uri.h"

struct HeaderCmp
{
	template<typename T>
	bool operator()(T const& a, T const& b) {
		return fz::tolower_ascii(lhs) < fz::tolower_ascii(rhs);
	}
};

typedef std::map<std::string, std::string, HeaderCmp> Headers;


class HttpRequest
{

public:
	fz::uri uri_;
	std::string verb_;
	Headers request_headers_;

	// localFile_ and payload_ cannot both be set.
	std::string localFile_;
	std::string payload_;
};

class CTlsSocket;
class CHttpControlSocket final : public CRealControlSocket
{
public:
	CHttpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CHttpControlSocket();

	virtual bool Connected() const override { return static_cast<bool>(currentServer_); }
protected:
	virtual void Connect(CServer const& server) override;
	virtual int FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
		std::wstring const& remoteFile, bool download,
		CFileTransferCommand::t_transferSettings const& transferSettings) override;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification);

	virtual int SendNextCommand() override;

	CTlsSocket* m_pTlsSocket{};


/*	virtual int ContinueConnect();
	virtual bool Connected() { return static_cast<bool>(currentServer_); }

	
	virtual int ParseSubcommandResult(int prevResult, COpData const& previousOperation);

	int InternalConnect(std::wstring host, unsigned short port, bool tls);
	int DoInternalConnect();

	virtual void OnConnect();
	virtual void OnClose(int error);
	virtual void OnReceive();
	int DoReceive();

	virtual int Disconnect();

	virtual int ResetOperation(int nErrorCode);

	virtual void ResetSocket();
	virtual void ResetHttpData(CHttpOpData* pData);

	int OpenFile( CHttpFileTransferOpData* pData);

	int ParseHeader(CHttpOpData* pData);
	int OnChunkedData(CHttpOpData* pData);

	int ProcessData(char* p, int len);

	char* m_pRecvBuffer{};
	unsigned int m_recvBufferPos{};
	static const unsigned int m_recvBufferLen = 4096;

	fz::uri m_current_uri;
	*/

	friend class CHttpOpData;
};

class CHttpOpData
{
public:
	CHttpOpData(CHttpControlSocket & controlSocket)
		: controlSocket_(controlSocket)
		, engine_(controlSocket.engine_)
		, currentServer_(controlSocket.currentServer_)
	{
	}

	virtual ~CHttpOpData() = default;

	template<typename...Args>
	void LogMessage(Args&& ...args) const {
		controlSocket_.LogMessage(std::forward<Args>(args)...);
	}

	CHttpControlSocket & controlSocket_;
	CFileZillaEnginePrivate & engine_;
	CServer & currentServer_;
};

#endif
