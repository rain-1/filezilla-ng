#ifndef FILEZILLA_ENGINE_HTTP_REQUEST_HEADER
#define FILEZILLA_ENGINE_HTTP_REQUEST_HEADER

#include "httpcontrolsocket.h"

class CServerPath;

class CHttpRequestOpData final : public COpData, public CHttpOpData
{
public:
	CHttpRequestOpData(CHttpControlSocket & controlSocket, HttpRequest& request, HttpResponse& response)
		: COpData(Command::rawtransfer)
		, CHttpOpData(controlSocket)
		, request_(request)
		, response_(response)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

private:
	HttpRequest & request_;
	HttpResponse & response_;
};

#endif
