#ifndef FILEZILLA_ENGINE_HTTP_CONNECT_HEADER
#define FILEZILLA_ENGINE_HTTP_CONNECT_HEADER

#include "httpcontrolsocket.h"

// Connect is special for HTTP: It is done on a per-command basis, so we need
// to establish a connection before each command.
// The general connect of the control socket is a NOOP.
class CHttpConnectOpData final : public COpData, public CHttpOpData
{
public:
	CHttpConnectOpData(CHttpControlSocket & controlSocket)
		: COpData(Command::connect)
		, CHttpOpData(controlSocket)
	{}

	virtual int Send() override { return FZ_REPLY_OK; }
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
};

#endif
