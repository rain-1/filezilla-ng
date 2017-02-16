#include <filezilla.h>

#include "rename.h"
#include "../directorycache.h"
#include "../pathcache.h"

int CFtpRenameOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRenameOpData::Send");

	bool res;
	switch (opState)
	{
	case rename_rnfrom:
		res = controlSocket_.SendCommand(L"RNFR " + command_.GetFromPath().FormatFilename(command_.GetFromFile(), !useAbsolute_));
		break;
	case rename_rnto:
	{
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, command_.GetFromPath(), command_.GetFromFile());
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, command_.GetToPath(), command_.GetToFile());

		CServerPath path(engine_.GetPathCache().Lookup(currentServer_, command_.GetFromPath(), command_.GetFromFile()));
		if (path.empty()) {
			path = command_.GetFromPath();
			path.AddSegment(command_.GetFromFile());
		}
		engine_.InvalidateCurrentWorkingDirs(path);

		engine_.GetPathCache().InvalidatePath(currentServer_, command_.GetFromPath(), command_.GetFromFile());
		engine_.GetPathCache().InvalidatePath(currentServer_, command_.GetToPath(), command_.GetToFile());

		res = controlSocket_.SendCommand(L"RNTO " + command_.GetToPath().FormatFilename(command_.GetToFile(), !useAbsolute_ && command_.GetFromPath() == command_.GetToPath()));
		break;
	}
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}

	if (!res) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CFtpRenameOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRenameOpData::ParseResponse");

	int code = controlSocket_.GetReplyCode();
	if (code != 2 && code != 3) {
		return FZ_REPLY_ERROR;
	}

	if (opState == rename_rnfrom) {
		opState = rename_rnto;
	}
	else {
		const CServerPath& fromPath = command_.GetFromPath();
		const CServerPath& toPath = command_.GetToPath();
		engine_.GetDirectoryCache().Rename(currentServer_, fromPath, command_.GetFromFile(), toPath, command_.GetToFile());

		controlSocket_.SendDirectoryListingNotification(fromPath, false, false);
		if (fromPath != toPath) {
			controlSocket_.SendDirectoryListingNotification(toPath, false, false);
		}

		return FZ_REPLY_OK;
	}

	return FZ_REPLY_CONTINUE;
}

int CFtpRenameOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRenameOpData::SubcommandResult");

	if (prevResult != FZ_REPLY_OK) {
		useAbsolute_ = true;
	}

	return FZ_REPLY_CONTINUE;
}