#ifndef FILEZILLA_ENGINE_STORJ_RMD_HEADER
#define FILEZILLA_ENGINE_STORJ_RMD_HEADER

#include "storjcontrolsocket.h"

class CStorjRemoveDirOpData final : public COpData, public CStorjOpData
{
public:
	CStorjRemoveDirOpData(CStorjControlSocket & controlSocket)
		: COpData(Command::removedir)
		, CStorjOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	CServerPath path_;

	std::wstring bucket_;
	std::wstring pathId_;
};

#endif
