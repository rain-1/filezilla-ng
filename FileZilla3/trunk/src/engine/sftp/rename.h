#ifndef FILEZILLA_ENGINE_SFTP_RENAME_HEADER
#define FILEZILLA_ENGINE_SFTP_RENAME_HEADER

#include "sftpcontrolsocket.h"

class CSftpRenameOpData final : public COpData, public CSftpOpData
{
public:
	CSftpRenameOpData(CSftpControlSocket & controlSocket, CRenameCommand const& command)
		: COpData(Command::rename)
		, CSftpOpData(controlSocket)
		, command_(command)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int, COpData const&) override;

	CRenameCommand command_;
	bool useAbsolute_{};
};

#endif
