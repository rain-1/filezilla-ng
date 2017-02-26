#include <filezilla.h>

#include "directorycache.h"
#include "mkd.h"

enum mkdStates
{
	mkd_init = 0,
	mkd_findparent,
	mkd_mkdsub,
	mkd_cwdsub,
	mkd_tryfull
};

int CSftpMkdirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpMkdirOpData::Send() in state %d", opState);
	
	if (!holdsLock_) {
		if (!controlSocket_.TryLockCache(CSftpControlSocket::lock_mkdir, path_)) {
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	switch (opState)
	{
	case mkd_init:
		if (!currentPath_.empty()) {
			// Unless the server is broken, a directory already exists if current directory is a subdir of it.
			if (currentPath_ == path_ || currentPath_.IsSubdirOf(path_, false)) {
				return FZ_REPLY_OK;
			}

			if (currentPath_.IsParentOf(path_, false)) {
				commonParent_ = currentPath_;
			}
			else {
				commonParent_ = path_.GetCommonParent(currentPath_);
			}
		}

		if (!path_.HasParent()) {
			opState = mkd_tryfull;
		}
		else {
			currentMkdPath_ = path_.GetParent();
			segments_.push_back(path_.GetLastSegment());

			if (currentMkdPath_ == currentPath_) {
				opState = mkd_mkdsub;
			}
			else {
				opState = mkd_findparent;
			}
		}
		return FZ_REPLY_CONTINUE;
	case mkd_findparent:
	case mkd_cwdsub:
		currentPath_.clear();
		return controlSocket_.SendCommand(L"cd " + controlSocket_.QuoteFilename(currentMkdPath_.GetPath()));
	case mkd_mkdsub:
		return controlSocket_.SendCommand(L"mkdir " + controlSocket_.QuoteFilename(segments_.back()));
	case mkd_tryfull:
		return controlSocket_.SendCommand(L"mkdir " + controlSocket_.QuoteFilename(path_.GetPath()));
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
	}

	return FZ_REPLY_INTERNALERROR;
}

int CSftpMkdirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpMkdirOpData::ParseResponse() in state %d", opState);
	
	bool successful = controlSocket_.result_ == FZ_REPLY_OK;
	switch (opState)
	{
	case mkd_findparent:
		if (successful) {
			currentPath_ = currentMkdPath_;
			opState = mkd_mkdsub;
		}
		else if (currentMkdPath_ == commonParent_) {
			opState = mkd_tryfull;
		}
		else if (currentMkdPath_.HasParent()) {
			segments_.push_back(currentMkdPath_.GetLastSegment());
			currentMkdPath_ = currentMkdPath_.GetParent();
		}
		else {
			opState = mkd_tryfull;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_mkdsub:
		if (successful) {
			if (segments_.empty()) {
				LogMessage(MessageType::Debug_Warning, L"  segments_ is empty");
				return FZ_REPLY_INTERNALERROR;
			}
			engine_.GetDirectoryCache().UpdateFile(currentServer_, currentMkdPath_, segments_.back(), true, CDirectoryCache::dir);
			controlSocket_.SendDirectoryListingNotification(currentMkdPath_, false, false);

			currentMkdPath_.AddSegment(segments_.back());
			segments_.pop_back();

			if (segments_.empty()) {
				return FZ_REPLY_OK;
			}
			else {
				opState = mkd_cwdsub;
			}
		}
		else {
			opState = mkd_tryfull;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_cwdsub:
		if (successful) {
			currentPath_ = currentMkdPath_;
			opState = mkd_mkdsub;
		}
		else {
			opState = mkd_tryfull;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_tryfull:
		return successful ? FZ_REPLY_OK : FZ_REPLY_ERROR;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
	}

	return FZ_REPLY_INTERNALERROR;
}
