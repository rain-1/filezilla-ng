#ifndef FILEZILLA_ENGINE_FTP_RAWCOMMAND_HEADER
#define FILEZILLA_ENGINE_FTP_RAWCOMMAND_HEADER

#include "ftpcontrolsocket.h"

class CFtpRawCommandOpData final : public COpData, public CFtpOpData
{
public:
	CFtpRawCommandOpData(CFtpControlSocket & controlSocket, std::wstring const& command)
	    : COpData(Command::raw)
		, CFtpOpData(controlSocket)
		, command_(command)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;

private:
	std::wstring const command_;
};

#endif
