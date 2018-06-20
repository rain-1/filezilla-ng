#ifndef FILEZILLA_ENGINE_STORJ_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_STORJ_FILETRANSFER_HEADER

#include "storjcontrolsocket.h"

class CStorjFileTransferOpData final : public CFileTransferOpData, public CStorjOpData
{
public:
	CStorjFileTransferOpData(CStorjControlSocket & controlSocket, bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path, CFileTransferCommand::t_transferSettings const& settings)
		: CFileTransferOpData(is_download, local_file, remote_file, remote_path, settings)
		, CStorjOpData(controlSocket)
	{
	}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

private:
	std::wstring bucket_;
	std::wstring fileId_;
};

#endif
