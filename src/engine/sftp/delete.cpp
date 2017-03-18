#include <filezilla.h>

#include "delete.h"
#include "directorycache.h"

int CSftpDeleteOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpDeleteOpData::Send() in state %d", opState);
	
	std::wstring const& file = files_.front();
	if (file.empty()) {
		LogMessage(MessageType::Debug_Info, L"Empty filename");
		return FZ_REPLY_INTERNALERROR;
	}

	std::wstring filename = path_.FormatFilename(file);
	if (filename.empty()) {
		LogMessage(MessageType::Error, _("Filename cannot be constructed for directory %s and filename %s"), path_.GetPath(), file);
		return FZ_REPLY_ERROR;
	}

	if (time_.empty()) {
		time_ = fz::datetime::now();
	}

	engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);

	return controlSocket_.SendCommand(L"rm " + controlSocket_.WildcardEscape(controlSocket_.QuoteFilename(filename)), L"rm " + controlSocket_.QuoteFilename(filename));
}

int CSftpDeleteOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpDeleteOpData::ParseResponse() in state %d", opState);

	if (controlSocket_.result_ != FZ_REPLY_OK) {
		deleteFailed_ = true;
	}
	else {
		std::wstring const& file = files_.front();

		engine_.GetDirectoryCache().RemoveFile(currentServer_, path_, file);

		auto const now = fz::datetime::now();
		if (!time_.empty() && (now - time_).get_seconds() >= 1) {
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

int CSftpDeleteOpData::SubcommandResult(int, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpDeleteOpData::SubcommandResult() in state %d", opState);
	return FZ_REPLY_INTERNALERROR;
}
