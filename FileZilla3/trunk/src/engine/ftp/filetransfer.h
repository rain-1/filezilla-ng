#ifndef FILEZILLA_ENGINE_FTP_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_FTP_FILETRANSFER_HEADER

#include "ftpcontrolsocket.h"

#include "iothread.h"

enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_waitcwd,
	filetransfer_waitlist,
	filetransfer_size,
	filetransfer_mdtm,
	filetransfer_resumetest,
	filetransfer_transfer,
	filetransfer_waittransfer,
	filetransfer_waitresumetest,
	filetransfer_mfmt
};

class CFtpFileTransferOpData final : public CFileTransferOpData, public CFtpTransferOpData, public CFtpOpData
{
public:
	CFtpFileTransferOpData(CFtpControlSocket& controlSocket, bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path);

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;

	int TestResumeCapability();

	std::unique_ptr<CIOThread> ioThread_;
	bool fileDidExist_{true};
};

#endif
