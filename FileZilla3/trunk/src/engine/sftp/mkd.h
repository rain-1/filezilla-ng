#ifndef FILEZILLA_ENGINE_SFTP_MKD_HEADER
#define FILEZILLA_ENGINE_SFTP_MKD_HEADER

#include "sftpcontrolsocket.h"

class CSftpMkdirOpData final : public CMkdirOpData, public CSftpOpData
{
public:
	CSftpMkdirOpData(CSftpControlSocket & controlSocket)
		: CSftpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
};

#endif
