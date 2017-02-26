#include <filezilla.h>

#include "delete.h"
#include "../directorycache.h"

enum rmdStates
{
	del_init,
	del_waitcwd,
	del_del
};

int CFtpDeleteOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpDeleteOpData::Send() in state %d", opState);

	if (opState == del_init) {
		controlSocket_.ChangeDir(path_);
		opState = del_waitcwd;
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == del_del) {
		std::wstring const& file = files_.front();
		if (file.empty()) {
			LogMessage(MessageType::Debug_Info, L"Empty filename");
			return FZ_REPLY_INTERNALERROR;
		}

		std::wstring filename = path_.FormatFilename(file, omitPath_);
		if (filename.empty()) {
			LogMessage(MessageType::Error, _("Filename cannot be constructed for directory %s and filename %s"), path_.GetPath(), file);
			return FZ_REPLY_ERROR;
		}

		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);

		return controlSocket_.SendCommand(L"DELE " + filename);
	}

	LogMessage(MessageType::Debug_Warning, L"Unkown op state %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CFtpDeleteOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpDeleteOpData::ParseResponse() in state %d", opState);

	int code = controlSocket_.GetReplyCode();
	if (code != 2 && code != 3) {
		deleteFailed_ = true;
	}
	else {
		std::wstring const& file = files_.front();

		engine_.GetDirectoryCache().RemoveFile(currentServer_, path_, file);

		auto now = fz::monotonic_clock::now();
		if (time_ && (now - time_).get_seconds() >= 1) {
			controlSocket_.SendDirectoryListingNotification(path_, false, false);
			time_ = now;
			needSendListing_ = false;
		}
		else {
			needSendListing_ = true;
		}
	}

	files_.pop_front();

	if (!files_.empty()) {
		return FZ_REPLY_CONTINUE;
	}

	return deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK;
}

int CFtpDeleteOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpDeleteOpData::SubcommandResult() in state %d", opState);

	if (opState == del_waitcwd) {
		opState = del_del;

		if (prevResult != FZ_REPLY_OK) {
			omitPath_ = false;
		}

		time_ = fz::monotonic_clock::now();
		return FZ_REPLY_CONTINUE;
	}
	else {
		return FZ_REPLY_INTERNALERROR;
	}
}
