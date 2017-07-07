#ifndef FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER

#include "ControlSocket.h"
#include "uri.h"
#include "httpheaders.h"

#include <libfilezilla/file.hpp>

namespace PrivCommand {
auto const http_request = Command::private1;
auto const http_connect = Command::private2;
}

class HttpRequest
{
public:
	#define HEADER_NAME_CONTENT_LENGTH "Content-Length"

	fz::uri uri_;
	std::string verb_;
	HttpHeaders headers_;

	// Gets called for the request body data.
	// If set, the headers_ must include a valid Content-Length.
	// Callback must write up to len bytes into the provided buffer,
	// and update len with the amount written.
	// Callback must return FZ_REPLY_CONTINUE or FZ_REPLY_ERROR
	std::function<int(unsigned char* data, unsigned int &len)> data_request_;
	
	int64_t get_content_length() const
	{
		int64_t result = 0;
		auto value = get_header(HEADER_NAME_CONTENT_LENGTH);
		if (!value.empty()) {
			result = fz::to_integral<int64_t>(value);
		}
		return result;
	}

	void set_content_length(int64_t length)
	{
		headers_[HEADER_NAME_CONTENT_LENGTH] = fz::to_string(length);
	}

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
	HttpHeaders headers_;

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

	bool success() const {
		return code_ >= 200 && code_ < 300;
	}

	bool code_prohobits_body() const {
		return (code_ >= 100 && code_ < 200) || code_ == 304 || code_ == 204;
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
	virtual void Connect(CServer const& server, Credentials const& credentials) override;
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
	
	virtual void ResetSocket() override;
	
	friend class CProtocolOpData<CHttpControlSocket>;
	friend class CHttpFileTransferOpData;
	friend class CHttpInternalConnectOpData;
	friend class CHttpRequestOpData;
private:
	std::wstring	connected_host_;
	unsigned short	connected_port_{};
	bool			connected_tls_{};
	bool			is_reusing_ = {};
};

typedef CProtocolOpData<CHttpControlSocket> CHttpOpData;

#endif
