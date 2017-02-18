#ifndef FILEZILLA_ENGINE_HTTP_INTERNALCONNECT_HEADER
#define FILEZILLA_ENGINE_HTTP_INTERNALCONNECT_HEADER

#include "httpcontrolsocket.h"

// Connect is special for HTTP: It is done on a per-command basis, so we need
// to establish a connection before each command.
// The general connect of the control socket is a NOOP.
class CHttpInternalConnectOpData final : public COpData, public CHttpOpData
{
public:
	CHttpInternalConnectOpData(CHttpControlSocket & controlSocket, std::wstring const& host, unsigned short port, bool tls)
		: COpData(PrivCommand::http_connect)
		, CHttpOpData(controlSocket)
		, host_(host)
		, port_(port)
		, tls_(tls)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	std::wstring host_;
	unsigned short port_;
	bool tls_;
};

#endif
