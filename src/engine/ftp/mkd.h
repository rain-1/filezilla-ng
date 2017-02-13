#ifndef FILEZILLA_ENGINE_FTP_MKD_HEADER
#define FILEZILLA_ENGINE_FTP_MKD_HEADER

#include "ftpcontrolsocket.h"


enum mkdStates
{
	mkd_init = 0,
	mkd_findparent,
	mkd_mkdsub,
	mkd_cwdsub,
	mkd_tryfull
};

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
