#include <filezilla.h>

#include "internalconnect.h"

#include "backend.h"

int CHttpInternalConnectOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpInternalConnectOpData::Send() in state %d", opState);

	if (!port_) {
		port_ = tls_ ? 443 : 80;
	}

	delete controlSocket_.m_pBackend;
	controlSocket_.m_pBackend = new CSocketBackend(&controlSocket_, *controlSocket_.m_pSocket, engine_.GetRateLimiter());

	int res = controlSocket_.m_pSocket->Connect(fz::to_native(host_), port_);
	if (!res) {
		return FZ_REPLY_OK;
	}

	if (res && res != EINPROGRESS) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}
