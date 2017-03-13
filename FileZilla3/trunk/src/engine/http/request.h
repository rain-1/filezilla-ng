#ifndef FILEZILLA_ENGINE_HTTP_REQUEST_HEADER
#define FILEZILLA_ENGINE_HTTP_REQUEST_HEADER

#include "httpcontrolsocket.h"

class CServerPath;

enum requestStates
{
	request_init = 0,
	request_wait_connect,
	request_send_header,
	request_send,
	request_read
};

class CHttpRequestOpData final : public COpData, public CHttpOpData
{
public:
	CHttpRequestOpData(CHttpControlSocket & controlSocket, HttpRequest& request, HttpResponse& response)
		: COpData(PrivCommand::http_request)
		, CHttpOpData(controlSocket)
		, request_(request)
		, response_(response)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	int OnReceive();
	int OnClose();

private:
	int ParseHeader();
	int ProcessCompleteHeader();
	int ParseChunkedData();
	int ProcessData(unsigned char* data, unsigned int len);

	HttpRequest & request_;
	HttpResponse & response_;

	std::unique_ptr<unsigned char[]> recv_buffer_;
	unsigned int m_recvBufferPos{};
	static const unsigned int m_recvBufferLen = 8192;
	bool got_header_{};

	enum transferEncodings
	{
		identity,
		chunked,
		unknown
	};
	transferEncodings transfer_encoding_{unknown};

	struct t_chunkData
	{
		bool getTrailer{};
		bool terminateChunk{};
		int64_t size{};
	} chunk_data_;

	uint64_t dataToSend_{};

	int64_t responseContentLength_{-1};
	int64_t receivedData_{};
};

#endif
