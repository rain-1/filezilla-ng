#ifndef FILEZILLA_ENGINE_SFTP_CWD_HEADER
#define FILEZILLA_ENGINE_SFTP_CWD_HEADER

#include "sftpcontrolsocket.h"

class CSftpChangeDirOpData final : public CChangeDirOpData, public CSftpOpData
{
public:
	CSftpChangeDirOpData(CSftpControlSocket & controlSocket)
		: CSftpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	
	virtual int SubcommandResult(int, COpData const&) override
	{
		LogMessage(MessageType::Debug_Verbose, L"CSftpChangeDirOpData::SubcommandResult()");
		return FZ_REPLY_CONTINUE;
	}
};

#endif
