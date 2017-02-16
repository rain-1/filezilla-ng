#ifndef FILEZILLA_ENGINE_FTP_RMD_HEADER
#define FILEZILLA_ENGINE_FTP_RMD_HEADER

#include "ftpcontrolsocket.h"
#include "serverpath.h"

class CFtpRemoveDirOpData final : public COpData, public CFtpOpData
{
public:
	CFtpRemoveDirOpData(CFtpControlSocket & controlSocket)
	    : COpData(Command::removedir)
		, CFtpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;

	CServerPath path_;
	CServerPath fullPath_;
	std::wstring subDir_;
	bool omitPath_{};
};

#endif
