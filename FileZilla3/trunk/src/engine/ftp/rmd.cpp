#include <filezilla.h>

#include "rmd.h"
#include "../directorycache.h"
#include "../pathcache.h"

int CFtpRemoveDirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRemoveDirOpData::Send");

	engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, subDir_);

	CServerPath path(engine_.GetPathCache().Lookup(currentServer_, path_, subDir_));
	if (path.empty()) {
		path = path;
		path.AddSegment(subDir_);
	}
	engine_.InvalidateCurrentWorkingDirs(path);

	engine_.GetPathCache().InvalidatePath(currentServer_, path, subDir_);

	if (omitPath_) {
		return controlSocket_.SendCommand(L"RMD " + subDir_);
	}
	else {
		if (!fullPath_.AddSegment(subDir_)) {
			LogMessage(MessageType::Error, _("Path cannot be constructed for directory %s and subdir %s"), path_.GetPath(), subDir_);
			return FZ_REPLY_ERROR;
		}

		return controlSocket_.SendCommand(L"RMD " + fullPath_.GetPath());
	}
}

int CFtpRemoveDirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRemoveDirOpData::ParseResponse");

	int code = controlSocket_.GetReplyCode();
	if (code != 2 && code != 3) {
		return FZ_REPLY_ERROR;
	}

	engine_.GetDirectoryCache().RemoveDir(currentServer_, path_, subDir_, engine_.GetPathCache().Lookup(currentServer_, path_, subDir_));
	controlSocket_.SendDirectoryListingNotification(path_, false, false);

	return FZ_REPLY_OK;
}

int CFtpRemoveDirOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpRemoveDirOpData::SubcommandResult");

	if (prevResult != FZ_REPLY_OK) {
		omitPath_ = false;
	}
	else {
		path_ = currentPath_;
	}

	return FZ_REPLY_CONTINUE;
}
