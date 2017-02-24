#ifndef FILEZILLA_ENGINE_FTP_RENAME_HEADER
#define FILEZILLA_ENGINE_FTP_RENAME_HEADER

#include "ftpcontrolsocket.h"

class CFtpRenameOpData final : public COpData, public CFtpOpData
{
public:
	CFtpRenameOpData(CFtpControlSocket & controlSocket, CRenameCommand const& command)
	    : COpData(Command::rename)
		, CFtpOpData(controlSocket)
		, command_(command)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;

	CRenameCommand const command_;
	bool useAbsolute_{};
};

#endif
