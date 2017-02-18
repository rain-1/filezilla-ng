#ifndef FILEZILLA_ENGINE_FTP_RAWTRANSFER_HEADER
#define FILEZILLA_ENGINE_FTP_RAWTRANSFER_HEADER

#include "ftpcontrolsocket.h"

enum rawtransferStates
{
	rawtransfer_init = 0,
	rawtransfer_type,
	rawtransfer_port_pasv,
	rawtransfer_rest,
	rawtransfer_transfer,
	rawtransfer_waitfinish,
	rawtransfer_waittransferpre,
	rawtransfer_waittransfer,
	rawtransfer_waitsocket
};

class CFtpRawTransferOpData final : public COpData, public CFtpOpData
{
public:
	CFtpRawTransferOpData(CFtpControlSocket& controlSocket)
		: COpData(PrivCommand::rawtransfer)
		, CFtpOpData(controlSocket)
	{
	}

	virtual int Send() override;
	virtual int ParseResponse() override;

	std::wstring GetPassiveCommand();
	bool ParsePasvResponse();
	bool ParseEpsvResponse();

	std::wstring cmd_;

	CFtpTransferOpData* pOldData{};

	bool bPasv{true};
	bool bTriedPasv{};
	bool bTriedActive{};

	std::wstring host_;
	int port_{};
};

#endif
