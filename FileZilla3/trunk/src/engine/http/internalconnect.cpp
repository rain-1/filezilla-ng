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
	controlSocket_.m_pBackend = new CSocketBackend(&controlSocket_, *controlSocket_.socket_, engine_.GetRateLimiter());

	return controlSocket_.DoConnect(host_, port_);
}
