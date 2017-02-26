#include <filezilla.h>

#include "rename.h"
#include "../directorycache.h"
#include "../pathcache.h"

enum renameStates
{
	rename_init,
	rename_waitcwd,
	rename_rnfrom,
	rename_rnto
};

int CFtpRenameOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRenameOpData::Send() in state %d", opState);

	switch (opState)
	{
	case rename_init:
		controlSocket_.ChangeDir(command_.GetFromPath());
		opState = rename_waitcwd;
		return FZ_REPLY_CONTINUE;
	case rename_rnfrom:
		return controlSocket_.SendCommand(L"RNFR " + command_.GetFromPath().FormatFilename(command_.GetFromFile(), !useAbsolute_));
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

		return controlSocket_.SendCommand(L"RNTO " + command_.GetToPath().FormatFilename(command_.GetToFile(), !useAbsolute_ && command_.GetFromPath() == command_.GetToPath()));
	}
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CFtpRenameOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRenameOpData::ParseResponse() in state %d", opState);

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
	LogMessage(MessageType::Debug_Verbose, L"CFtpRenameOpData::SubcommandResult() in state %d", opState);

	if (opState == rename_waitcwd) {
		if (prevResult != FZ_REPLY_OK) {
			useAbsolute_ = true;
		}

		opState = rename_rnfrom;
		return FZ_REPLY_CONTINUE;
	}
	else {
		return FZ_REPLY_INTERNALERROR;
	}
}
