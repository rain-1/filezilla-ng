#include <filezilla.h>

#include "directorycache.h"
#include "list.h"
#include "rmd.h"

enum mkdStates
{
	rmd_init = 0,
	rmd_resolve,
	rmd_rmbucket,
	rmd_list,
	rmd_rmdir
};


int CStorjRemoveDirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjRemoveDirOpData::Send() in state %d", opState);

	switch (opState) {
	case rmd_init:
		if (path_.SegmentCount() < 1) {
			LogMessage(MessageType::Error, _("Invalid path"));
			return FZ_REPLY_CRITICALERROR;
		}
		controlSocket_.Resolve(path_, std::wstring(), bucket_);
		opState = rmd_resolve;
		return FZ_REPLY_CONTINUE;
	case rmd_rmbucket:
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, CServerPath(L"/"), path_.GetFirstSegment());

		engine_.InvalidateCurrentWorkingDirs(path_);

		return controlSocket_.SendCommand(L"rmbucket " + bucket_);
	case rmd_rmdir:
		assert(!pathId_.empty());
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_.GetParent(), path_.GetLastSegment());
		return controlSocket_.SendCommand(L"rm " + bucket_ + L" " + pathId_);
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjRemoveDirOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjRemoveDirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjRemoveDirOpData::ParseResponse() in state %d", opState);


	switch (opState) {
	case rmd_rmbucket:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			engine_.GetDirectoryCache().RemoveDir(currentServer_, CServerPath(L"/"), path_.GetFirstSegment(), CServerPath());
			controlSocket_.SendDirectoryListingNotification(CServerPath(L"/"), false, false);
		}

		return controlSocket_.result_;
	case rmd_rmdir:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			engine_.GetDirectoryCache().RemoveDir(currentServer_, path_.GetParent(), path_.GetLastSegment(), CServerPath());
			controlSocket_.SendDirectoryListingNotification(path_.GetParent(), false, false);
		}
		return controlSocket_.result_;
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjRemoveDirOpData::ParseResponse()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjRemoveDirOpData::SubcommandResult(int prevResult, COpData const& previousOperation)
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjRemoveDirOpData::SubcommandResult() in state %d", opState);

	switch (opState) {
	case rmd_resolve:
		if (prevResult != FZ_REPLY_OK) {
			return prevResult;
		}

		if (path_.SegmentCount() == 1) {
			opState = rmd_rmbucket;
		}
		else {
			controlSocket_.List(path_, std::wstring(), LIST_FLAG_REFRESH);
			opState = rmd_list;
		}
		return FZ_REPLY_CONTINUE;
	case rmd_list:
		if (prevResult != FZ_REPLY_OK) {
			return prevResult;
		}

		auto const& listData = static_cast<CStorjListOpData const&>(previousOperation);
		pathId_ = listData.GetPathId();
		if (pathId_.empty()) {
			return FZ_REPLY_ERROR;
		}
		opState = rmd_rmdir;
		return FZ_REPLY_CONTINUE;
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjRemoveDirOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}
