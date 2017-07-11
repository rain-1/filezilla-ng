#ifndef FILEZILLA_ENGINE_STORJ_DELETE_HEADER
#define FILEZILLA_ENGINE_STORJ_DELETE_HEADER

#include "storjcontrolsocket.h"

class CStorjDeleteOpData final : public COpData, public CStorjOpData
{
public:
	CStorjDeleteOpData(CStorjControlSocket & controlSocket, CServerPath const& path, std::deque<std::wstring> && files)
		: COpData(Command::del)
		, CStorjOpData(controlSocket)
		, path_(path)
		, files_(files)
	{
	}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	CServerPath path_;
	std::deque<std::wstring> files_;
	std::deque<std::wstring> fileIds_;

	// Set to fz::datetime::Now initially and after
	// sending an updated listing to the UI.
	fz::datetime time_;

	bool needSendListing_{};

	// Set to true if deletion of at least one file failed
	bool deleteFailed_{};

	std::wstring bucket_;
};

#endif
