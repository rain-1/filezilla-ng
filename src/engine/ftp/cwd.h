#ifndef FILEZILLA_ENGINE_FTP_CWD_HEADER
#define FILEZILLA_ENGINE_FTP_CWD_HEADER

#include "ftpcontrolsocket.h"

enum cwdStates
{
	cwd_init = 0,
	cwd_pwd,
	cwd_cwd,
	cwd_pwd_cwd,
	cwd_cwd_subdir,
	cwd_pwd_subdir
};

class CFtpChangeDirOpData final : public CChangeDirOpData, public CFtpOpData
{
public:
	CFtpChangeDirOpData(CFtpControlSocket & controlSocket)
	    : CFtpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;

	virtual int SubcommandResult(int, COpData const&) override
	{
		LogMessage(MessageType::Debug_Verbose, L"CFtpChangeDirOpData::SubcommandResult()");
		return FZ_REPLY_CONTINUE;
	}

	bool tried_cdup{};
};

#endif
