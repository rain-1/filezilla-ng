#include <filezilla.h>

#include "directorycache.h"
#include "delete.h"

enum DeleteStates
{
	delete_init,
	delete_resolve,
	delete_delete
};

int CStorjDeleteOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjDeleteOpData::Send() in state %d", opState);

	switch (opState) {
	case delete_init:
		if (files_.empty()) {
			return FZ_REPLY_CRITICALERROR;
		}

		opState = delete_resolve;
		return FZ_REPLY_CONTINUE;
	case delete_resolve:
		opState = delete_resolve;
		controlSocket_.Resolve(path_, files_, bucket_, fileIds_);
		return FZ_REPLY_CONTINUE;
	case delete_delete:
		if (files_.empty()) {
			return FZ_REPLY_OK;
		}

		std::wstring const& file = files_.front();
		std::wstring const& id = fileIds_.front();
		if (id.empty()) {
			files_.pop_front();
			fileIds_.pop_front();
			return FZ_REPLY_CONTINUE;
		}

		if (time_.empty()) {
			time_ = fz::datetime::now();
		}

		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);

		return controlSocket_.SendCommand(L"rm " + bucket_ + L" " + id);
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjDeleteOpData::FileTransferSend()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjDeleteOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjDeleteOpData::ParseResponse() in state %d", opState);

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
	fileIds_.pop_front();

	if (!files_.empty()) {
		return FZ_REPLY_CONTINUE;
	}

	return deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK;
}

int CStorjDeleteOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjDeleteOpData::SubcommandResult() in state %d", opState);

	if (prevResult != FZ_REPLY_OK) {
		return prevResult;
	}

	if (files_.size() != fileIds_.size()) {
		return FZ_REPLY_INTERNALERROR;
	}

	opState = delete_delete;
	return FZ_REPLY_CONTINUE;
}
