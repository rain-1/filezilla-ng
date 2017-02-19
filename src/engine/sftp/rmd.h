#ifndef FILEZILLA_ENGINE_SFTP_RMD_HEADER
#define FILEZILLA_ENGINE_SFTP_RMD_HEADER

#include "sftpcontrolsocket.h"

class CSftpRemoveDirOpData final : public COpData, public CSftpOpData
{
public:
	CSftpRemoveDirOpData(CSftpControlSocket & controlSocket)
		: COpData(Command::removedir)
		, CSftpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;

	CServerPath path_;
	std::wstring subDir_;
};

#endif
