#ifndef FILEZILLA_ENGINE_FTP_MKD_HEADER
#define FILEZILLA_ENGINE_FTP_MKD_HEADER

#include "ftpcontrolsocket.h"

class CFtpMkdirOpData final : public CMkdirOpData, public CFtpOpData
{
public:
	CFtpMkdirOpData(CFtpControlSocket & controlSocket)
		: CFtpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
};

#endif
