#ifndef FILEZILLA_ENGINE_SFTP_CHMOD_HEADER
#define FILEZILLA_ENGINE_SFTP_CHMOD_HEADER

#include "sftpcontrolsocket.h"

class CSftpChmodOpData final : public COpData, public CSftpOpData
{
public:
	CSftpChmodOpData(CSftpControlSocket & controlSocket, CChmodCommand const& command)
		: COpData(Command::chmod)
		, CSftpOpData(controlSocket)
		, command_(command)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int, COpData const&) override;

private:
	CChmodCommand command_;
	bool useAbsolute_{};
};

#endif
