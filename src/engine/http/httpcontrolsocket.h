#ifndef FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_HTTP_HTTPCONTROLSOCKET_HEADER

#include "ControlSocket.h"
#include "httpheaders.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/uri.hpp>

namespace PrivCommand {
auto const http_request = Command::private1;
auto const http_connect = Command::private2;
}

class request_body
{
public:
	virtual ~request_body() = default;

	virtual uint64_t size() const = 0;

	// data_request must write up to len bytes into the provided buffer,
	// and update len with the amount written.
	// Must return FZ_REPLY_CONTINUE or FZ_REPLY_ERROR
	virtual int data_request(unsigned char* data, unsigned int & len) = 0;

	// reset() must return FZ_REPLY_CONTINUE or FZ_REPLY_ERROR
	virtual int rewind() = 0;
};

class simple_body final : public request_body
{
public:
	simple_body() = default;
	explicit simple_body(std::string const& body)
		: body_(body)
	{}

	void set(std::string const& body)
	{
		body_ = body;
		written_ = 0;
	}

	virtual int data_request(unsigned char* data, unsigned int & len) override;

	virtual int rewind() override
	{
		written_ = 0;
		return FZ_REPLY_CONTINUE;
	}

	virtual uint64_t size() const override { return body_.size(); }

private:
	size_t written_{};
	std::string body_;
};


class file_body final : public request_body
{
public:
	file_body(fz::file & file, uint64_t start, uint64_t size, CLogging & logger);

	virtual uint64_t size() const override { return size_; }

	virtual int data_request(unsigned char* data, unsigned int & len) override;

	virtual int rewind() override;

	std::function<void(int64_t)> progress_callback_;

private:
	fz::file & file_;
	uint64_t start_{};
	uint64_t written_{};
	uint64_t size_{};

	CLogging & logger_;
};

#define HEADER_NAME_CONTENT_LENGTH "Content-Length"
#define HEADER_NAME_CONTENT_TYPE "Content-Type"
class WithHeaders
{
public:
	virtual ~WithHeaders() = default;

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

	void set_content_type(std::string const& content_type)
	{
		headers_[HEADER_NAME_CONTENT_TYPE] = content_type;
	}

	std::string get_header(std::string const& key) const
	{
		auto it = headers_.find(key);
		if (it != headers_.end()) {
			return it->second;
		}
		return std::string();
	}

	bool keep_alive() const
	{
		return fz::str_tolower_ascii(get_header("Connection")) != "close";
	}

	HttpHeaders headers_;
};

class HttpRequest : public WithHeaders
{
public:
	fz::uri uri_;
	std::string verb_;

	enum flags {
		flag_sent_header = 0x01,
		flag_sent_body = 0x02
	};
	int flags_{};

	std::unique_ptr<request_body> body_;

	virtual int reset();
};

class HttpResponse : public WithHeaders
{
public:
	virtual ~HttpResponse() {};
	unsigned int code_{};

	enum flags {
		flag_got_code = 0x01,
		flag_got_header = 0x02,
		flag_got_body = 0x04,
		flag_no_body = 0x08, // e.g. on HEAD requests, or 204/304 responses
		flag_ignore_body = 0x10 // If set, on_data_ isn't called
	};
	int flags_{};

	bool got_code() const { return flags_ & flag_got_code; }
	bool got_header() const { return flags_ & flag_got_header; }
	bool got_body() const { return flags_ & flag_got_body; }
	bool no_body() const { return flags_ & flag_no_body; }

	// Called once the complete header has been received.
	// Return one of:
	//   FZ_REPLY_CONTINUE: All is well
	//   FZ_REPLY_OK: We're not interested in the request body, but continue
	//   FZ_REPLY_ERROR: Abort connection
	std::function<int()> on_header_;

	// Is only called after the on_header_ callback.
	// Callback must return FZ_REPLY_CONTINUE or FZ_REPLY_ERROR
	std::function<int(unsigned char const* data, unsigned int len)> on_data_;

	bool success() const {
		return code_ >= 200 && code_ < 300;
	}

	bool code_prohobits_body() const {
		return (code_ >= 100 && code_ < 200) || code_ == 304 || code_ == 204;
	}

	virtual int reset();
};

class HttpRequestResponseInterface
{
public:
	virtual ~HttpRequestResponseInterface() = default;

	virtual HttpRequest & request() = 0;
	virtual HttpResponse & response() = 0;
};

template<typename Request, typename Response>
class ProtocolRequestResponse : public HttpRequestResponseInterface
{
public:
	virtual HttpRequest & request() override final {
		return request_;
	}

	virtual HttpResponse & response() override final {
		return response_;
	}

	Request request_;
	Response response_;
};

typedef ProtocolRequestResponse<HttpRequest, HttpResponse> HttpRequestResponse;

template<typename T> void null_deleter(T *) {}

template<typename R = HttpRequestResponseInterface, typename T>
std::shared_ptr<R> make_simple_rr(T * rr)
{
	return std::shared_ptr<R>(rr, &null_deleter<R>);
}

class CTlsSocket;
class CHttpControlSocket : public CRealControlSocket
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

	void Request(std::shared_ptr<HttpRequestResponseInterface> const& request);
	void Request(std::deque<std::shared_ptr<HttpRequestResponseInterface>> && requests);

	template<typename T>
	void RequestMany(T && requests)
	{
		std::deque<std::shared_ptr<HttpRequestResponseInterface>> rrs;
		for (auto const& request : requests) {
			rrs.push_back(request);
		}
		Request(std::move(rrs));
	}

	// FZ_REPLY_OK: Re-using existing connection
	// FZ_REPLY_WOULDBLOCK: Cannot perform action right now (!allowDisconnect)
	// FZ_REPLY_CONTINUE: Connection operation pusehd to stack
	int InternalConnect(std::wstring const& host, unsigned short port, bool tls, bool allowDisconnect);
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
};

typedef CProtocolOpData<CHttpControlSocket> CHttpOpData;

#endif
