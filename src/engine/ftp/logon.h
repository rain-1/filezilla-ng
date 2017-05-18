#ifndef FILEZILLA_ENGINE_FTP_LOGON_HEADER
#define FILEZILLA_ENGINE_FTP_LOGON_HEADER

#include "ftpcontrolsocket.h"

enum loginStates
{
	LOGON_CONNECT,
	LOGON_WELCOME,
	LOGON_AUTH_TLS,
	LOGON_AUTH_SSL,
	LOGON_AUTH_WAIT,
	LOGON_LOGON,
	LOGON_SYST,
	LOGON_FEAT,
	LOGON_CLNT,
	LOGON_OPTSUTF8,
	LOGON_PBSZ,
	LOGON_PROT,
	LOGON_OPTSMLST,
	LOGON_CUSTOMCOMMANDS,
	LOGON_DONE
};


enum class loginCommandType
{
	user,
	pass,
	account,
	other
};

struct t_loginCommand
{
	bool optional;
	bool hide_arguments;
	loginCommandType type;

	std::wstring command;
};


class CFtpLogonOpData final : public COpData, public CFtpOpData
{
public:
	CFtpLogonOpData(CFtpControlSocket& controlSocket, Credentials const& credentials);

	virtual int Send() override;
	virtual int ParseResponse() override;

	void ParseFeat(std::wstring line);

	std::wstring challenge; // Used for interactive logons
	bool waitChallenge{};
	bool waitForAsyncRequest{};
	bool gotFirstWelcomeLine{};
	Credentials credentials_;

private:

	bool PrepareLoginSequence();

	std::wstring host_;
	unsigned int port_{};

	unsigned int customCommandIndex{};

	int neededCommands[LOGON_DONE];

	std::deque<t_loginCommand> loginSequence;

	int ftp_proxy_type_{};
};

#endif
