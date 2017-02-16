#ifndef FILEZILLA_ENGINE_FTP_CHMOD_HEADER
#define FILEZILLA_ENGINE_FTP_CHMOD_HEADER

#include "ftpcontrolsocket.h"

class CFtpChmodOpData final : public COpData, public CFtpOpData
{
public:
	CFtpChmodOpData(CFtpControlSocket & controlSocket, CChmodCommand const& command)
	    : COpData(Command::chmod)
		, CFtpOpData(controlSocket)
		, command_(command)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;

	CChmodCommand command_;
	bool useAbsolute_{};
};

#endif
