#ifndef FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER

#include "ControlSocket.h"
#include "uri.h"

#include <libfilezilla/file.hpp>

namespace PrivCommand {
auto const http_connect = Command::private3;
}

struct HeaderCmp
{
	template<typename T>
	bool operator()(T const& lhs, T const& rhs) const {
		return fz::str_tolower_ascii(lhs) < fz::str_tolower_ascii(rhs);
	}
};

typedef std::map<std::string, std::string, HeaderCmp> Headers;

class HttpRequest
{
public:
	fz::uri uri_;
	std::string verb_;
	Headers headers_;

	// Gets called for the request body data.
	// If set, the headers_ must include a valid Content-Length.
	// Callback must write up to len bytes into the provided buffer,
	// and update len with the amount written.
	// Callback must return FZ_REPLY_CONTINUE or FZ_REPLY_ERROR
	std::function<int(unsigned char* data, unsigned int &len)> _data_request_;
};

class HttpResponse
{
public:
	unsigned int code_{};
	Headers headers_;

	// If this callback is called, code_ and headers_ are already filled.
	// Callback must return FZ_REPLY_CONTINUE or FZ_REPLY_ERROR
	std::function<int(unsigned char const* data, unsigned int len)> on_data_;
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
	void Request(HttpRequest & request, HttpResponse & response);
	void InternalConnect(std::wstring const& host, unsigned short port, bool tls);
	virtual int Disconnect() override;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification);

	virtual int SendNextCommand() override;

	CTlsSocket* m_pTlsSocket{};

	virtual int ParseSubcommandResult(int prevResult, COpData const& previousOperation);

/*	virtual int ContinueConnect();
	virtual bool Connected() { return static_cast<bool>(currentServer_); }

	

	int InternalConnect(std::wstring host, unsigned short port, bool tls);
	int DoInternalConnect();
	*/
	virtual void OnConnect();
	/*
	virtual void OnClose(int error);
	virtual void OnReceive();
	int DoReceive();

	
	virtual int ResetOperation(int nErrorCode);

	virtual void ResetSocket();
	virtual void ResetHttpData(CHttpOpData* pData);

	int ParseHeader(CHttpOpData* pData);
	int OnChunkedData(CHttpOpData* pData);

	int ProcessData(char* p, int len);

	char* m_pRecvBuffer{};
	unsigned int m_recvBufferPos{};
	static const unsigned int m_recvBufferLen = 4096;

	fz::uri m_current_uri;
	*/

	friend class CHttpFileTransferOpData;
	friend class CHttpInternalConnectOpData;
	friend class CHttpOpData;
	friend class CHttpRequestOpData;
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
