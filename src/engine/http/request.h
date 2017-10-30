#ifndef FILEZILLA_ENGINE_HTTP_REQUEST_HEADER
#define FILEZILLA_ENGINE_HTTP_REQUEST_HEADER

#include "httpcontrolsocket.h"

#include <libfilezilla/buffer.hpp>

class CServerPath;

enum requestStates
{
	request_init = 0,
	request_wait_connect,
	request_send_header,
	request_send,
	request_read,
	request_done
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
	virtual int Reset(int result) override;

	int OnReceive();
	int OnClose();

private:
	int ParseHeader();
	int ProcessCompleteHeader();
	int ParseChunkedData();
	int ProcessData(unsigned char* data, unsigned int len);

	HttpRequest & request_;
	HttpResponse & response_;

	fz::buffer recv_buffer_;
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
		uint64_t size{};
	} chunk_data_;

	uint64_t dataToSend_{};

	int64_t responseContentLength_{-1};
	int64_t receivedData_{};
};

#endif
