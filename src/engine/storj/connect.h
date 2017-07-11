#ifndef FILEZILLA_ENGINE_STORJ_CONNECT_HEADER
#define FILEZILLA_ENGINE_STORJ_CONNECT_HEADER

#include "storjcontrolsocket.h"

enum connectStates
{
	connect_init,
	connect_timeout,
	connect_proxy,
	connect_host,
	connect_user,
	connect_pass,
	connect_key
};

class CStorjConnectOpData final : public COpData, public CStorjOpData
{
public:
	CStorjConnectOpData(CStorjControlSocket & controlSocket, Credentials const& credentials)
		: COpData(Command::connect)
		, CStorjOpData(controlSocket)
		, credentials_(credentials)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;

private:
	Credentials const credentials_;
};

#endif
