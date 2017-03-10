#ifndef FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER

#include "ControlSocket.h"
#include "uri.h"

#include <libfilezilla/file.hpp>

namespace PrivCommand {
auto const http_request = Command::private1;
auto const http_connect = Command::private2;
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
	std::function<int(unsigned char* data, unsigned int &len)> data_request_;

	std::string get_header(std::string const& key) const
	{
		auto it = headers_.find(key);
		if (it != headers_.end()) {
			return it->second;
		}
		return std::string();
	}
};

class HttpResponse
{
public:
	unsigned int code_{};
	Headers headers_;

	// Called once the complete header has been received.
	std::function<int()> on_header_;

	// Is only called after the on_header_ callback.
	// Callback must return FZ_REPLY_CONTINUE or FZ_REPLY_ERROR
	std::function<int(unsigned char const* data, unsigned int len)> on_data_;

	std::string get_header(std::string const& key) const
	{
		auto it = headers_.find(key);
		if (it != headers_.end()) {
			return it->second;
		}
		return std::string();
	}
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
	virtual void FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
		std::wstring const& remoteFile, bool download,
		CFileTransferCommand::t_transferSettings const& transferSettings) override;
	void Request(HttpRequest & request, HttpResponse & response);
	void InternalConnect(std::wstring const& host, unsigned short port, bool tls);
	virtual int Disconnect() override;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;

	CTlsSocket* m_pTlsSocket{};

	virtual void OnConnect() override;
	virtual void OnClose(int error) override;
	virtual void OnReceive() override;
	virtual int OnSend() override;
	
	virtual int ResetOperation(int nErrorCode) override;

	virtual void ResetSocket() override;
	
	friend class CProtocolOpData<CHttpControlSocket>;
	friend class CHttpFileTransferOpData;
	friend class CHttpInternalConnectOpData;
	friend class CHttpRequestOpData;
};

typedef CProtocolOpData<CHttpControlSocket> CHttpOpData;

#endif
