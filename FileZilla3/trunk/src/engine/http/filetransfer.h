#ifndef FILEZILLA_ENGINE_HTTP_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_HTTP_FILETRANSFER_HEADER

#include "httpcontrolsocket.h"

class CServerPath;

class CHttpFileTransferOpData final : public CFileTransferOpData, public CHttpOpData
{
public:
	CHttpFileTransferOpData(CHttpControlSocket & controlSocket, bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path)
		: CFileTransferOpData(is_download, local_file, remote_file, remote_path)
		, CHttpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse();

	HttpRequest req_;
};

#endif
