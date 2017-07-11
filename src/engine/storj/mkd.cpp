#include <filezilla.h>

#include "directorycache.h"
#include "mkd.h"

enum mkdStates
{
	mkd_init = 0,
	mkd_mkbucket,
	mkd_resolve,
	mkd_put
};


int CStorjMkdirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjMkdirOpData::Send() in state %d", opState);

	switch (opState) {
	case mkd_init:
		if (path_.SegmentCount() < 1) {
			LogMessage(MessageType::Error, _("Invalid path"));
			return FZ_REPLY_CRITICALERROR;
		}
		opState = mkd_mkbucket;
		return FZ_REPLY_CONTINUE;
	case mkd_mkbucket:
		return controlSocket_.SendCommand(L"mkbucket " + controlSocket_.QuoteFilename(path_.GetFirstSegment()));
	case mkd_put:
		{
			std::wstring path = path_.GetPath();
			auto pos = path.find('/', 1);
			if (pos == std::string::npos) {
				return FZ_REPLY_INTERNALERROR;
			}
			else {
				path = path.substr(pos + 1) + L"/";
			}
			return controlSocket_.SendCommand(L"put " + bucket_ + L" \"null\" " + controlSocket_.QuoteFilename(path));
		}
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjMkdirOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjMkdirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjMkdirOpData::ParseResponse() in state %d", opState);

	switch (opState) {
	case mkd_mkbucket:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			engine_.GetDirectoryCache().UpdateFile(currentServer_, CServerPath(L"/"), path_.GetFirstSegment(), true, CDirectoryCache::dir);
			controlSocket_.SendDirectoryListingNotification(CServerPath(L"/"), false, false);
		}

		if (path_.SegmentCount() > 1) {
			opState = mkd_resolve;
			controlSocket_.Resolve(path_, std::wstring(), bucket_, 0);
			return FZ_REPLY_CONTINUE;
		}
		else {
			return controlSocket_.result_;
		}
	case mkd_put:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			CServerPath path = path_;
			while (path.SegmentCount() > 1) {
				CServerPath parent = path.GetParent();
				engine_.GetDirectoryCache().UpdateFile(currentServer_, parent, path.GetLastSegment(), true, CDirectoryCache::dir);
				controlSocket_.SendDirectoryListingNotification(parent, false, false);
				path = parent;
			}
		}
		return controlSocket_.result_;
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjMkdirOpData::ParseResponse()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjMkdirOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjMkdirOpData::SubcommandResult() in state %d", opState);

	if (prevResult != FZ_REPLY_OK) {
		return prevResult;
	}

	switch (opState) {
	case mkd_resolve:
		opState = mkd_put;
		return FZ_REPLY_CONTINUE;
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjMkdirOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}
