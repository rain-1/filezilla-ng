#include <filezilla.h>

#include "chmod.h"
#include "../directorycache.h"

int CFtpChmodOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpChmodOpData::Send");

	if (!controlSocket_.SendCommand(L"SITE CHMOD " + command_.GetPermission() + L" " + command_.GetPath().FormatFilename(command_.GetFile(), !useAbsolute_))) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CFtpChmodOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpChmodOpData::ParseResponse");

	int code = controlSocket_.GetReplyCode();
	if (code != 2 && code != 3) {
		return FZ_REPLY_ERROR;
	}

	engine_.GetDirectoryCache().UpdateFile(currentServer_, command_.GetPath(), command_.GetFile(), false, CDirectoryCache::unknown);

	return FZ_REPLY_OK;
}

int CFtpChmodOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpChmodOpData::SubcommandResult");

	if (prevResult != FZ_REPLY_OK) {
		useAbsolute_ = true;
	}

	return FZ_REPLY_CONTINUE;
}
