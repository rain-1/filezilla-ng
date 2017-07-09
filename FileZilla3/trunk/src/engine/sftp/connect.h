#ifndef FILEZILLA_ENGINE_SFTP_CONNECT_HEADER
#define FILEZILLA_ENGINE_SFTP_CONNECT_HEADER

#include "sftpcontrolsocket.h"

enum connectStates
{
	connect_init,
	connect_proxy,
	connect_keys,
	connect_open
};

class CSftpConnectOpData final : public COpData, public CSftpOpData
{
public:
	CSftpConnectOpData(CSftpControlSocket & controlSocket, Credentials const& credentials)
		: COpData(Command::connect)
		, CSftpOpData(controlSocket)
		, credentials_(credentials)
		, keyfile_(keyfiles_.cend())
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int Reset(int result) override;

	std::wstring lastChallenge;
	CInteractiveLoginNotification::type lastChallengeType{ CInteractiveLoginNotification::interactive };
	bool criticalFailure{};

	Credentials credentials_;

	std::vector<std::wstring> keyfiles_;
	std::vector<std::wstring>::const_iterator keyfile_;
};

#endif
