#include <filezilla.h>

#include "rawcommand.h"
#include "../directorycache.h"
#include "../pathcache.h"

int CFtpRawCommandOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRawCommandOpData::Send() in state %d", opState);

	engine_.GetDirectoryCache().InvalidateServer(currentServer_);
	engine_.GetPathCache().InvalidateServer(currentServer_);
	currentPath_.clear();

	controlSocket_.m_lastTypeBinary = -1;

	return controlSocket_.SendCommand(command_, false, false);
}

int CFtpRawCommandOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRawCommandOpData::ParseResponse() in state %d", opState);

	int code = controlSocket_.GetReplyCode();
	if (code == 2 || code == 3) {
		return FZ_REPLY_OK;
	}
	else {
		return FZ_REPLY_ERROR;
	}
}
