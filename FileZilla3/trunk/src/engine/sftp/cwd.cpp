#include <filezilla.h>

#include "cwd.h"
#include "pathcache.h"

enum cwdStates
{
	cwd_init = 0,
	cwd_pwd,
	cwd_cwd,
	cwd_cwd_subdir
};

int CSftpChangeDirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpChangeDirOpData::Send() in state %d", opState);
	
	std::wstring cmd;
	switch (opState)
	{
	case cwd_init:
		if (path_.GetType() == DEFAULT) {
			path_.SetType(currentServer_.GetType());
		}

		if (path_.empty()) {
			if (currentPath_.empty()) {
				opState = cwd_pwd;
			}
			else {
				return FZ_REPLY_OK;
			}
		}
		else {
			if (!subDir_.empty()) {
				// Check if the target is in cache already
				target_ = engine_.GetPathCache().Lookup(currentServer_, path_, subDir_);
				if (!target_.empty()) {
					if (currentPath_ == target_) {
						return FZ_REPLY_OK;
					}

					path_ = target_;
					subDir_.clear();
					opState = cwd_cwd;
				}
				else {
					// Target unknown, check for the parent's target
					target_ = engine_.GetPathCache().Lookup(currentServer_, path_, L"");
					if (currentPath_ == path_ || (!target_.empty() && target_ == currentPath_)) {
						target_.clear();
						opState = cwd_cwd_subdir;
					}
					else {
						opState = cwd_cwd;
					}
				}
			}
			else {
				target_ = engine_.GetPathCache().Lookup(currentServer_, path_, L"");
				if (currentPath_ == path_ || (!target_.empty() && target_ == currentPath_)) {
					return FZ_REPLY_OK;
				}
				opState = cwd_cwd;
			}
		}
		return FZ_REPLY_CONTINUE;
	case cwd_pwd:
		cmd = L"pwd";
		break;
	case cwd_cwd:
		if (tryMkdOnFail_ && !holdsLock_) {
			if (controlSocket_.IsLocked(CSftpControlSocket::lock_mkdir, path_)) {
				// Some other engine is already creating this directory or
				// performing an action that will lead to its creation
				tryMkdOnFail_ = false;
			}
			if (!controlSocket_.TryLockCache(CSftpControlSocket::lock_mkdir, path_)) {
				return FZ_REPLY_WOULDBLOCK;
			}
		}
		cmd = L"cd " + controlSocket_.QuoteFilename(path_.GetPath());
		currentPath_.clear();
		break;
	case cwd_cwd_subdir:
		if (subDir_.empty()) {
			return FZ_REPLY_INTERNALERROR;
		}
		else {
			cmd = L"cd " + controlSocket_.QuoteFilename(subDir_);
		}
		currentPath_.clear();
		break;
	}

	if (!cmd.empty()) {
		return controlSocket_.SendCommand(cmd);
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CSftpChangeDirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpChangeDirOpData::ParseResponse() in state %d", opState);
	
	bool const successful = controlSocket_.result_ == FZ_REPLY_OK;
	switch (opState)
	{
	case cwd_pwd:
		if (!successful || controlSocket_.response_.empty()) {
			return FZ_REPLY_ERROR;
		}

		if (!controlSocket_.ParsePwdReply(controlSocket_.response_)) {
			return FZ_REPLY_ERROR;
		}

		return FZ_REPLY_OK;
	case cwd_cwd:
		if (!successful) {
			// Create remote directory if part of a file upload
			if (tryMkdOnFail_) {
				tryMkdOnFail_ = false;
				controlSocket_.Mkdir(path_);
				return FZ_REPLY_CONTINUE;
			}
			else {
				return FZ_REPLY_ERROR;
			}
		}
		else if (controlSocket_.response_.empty()) {
			return FZ_REPLY_ERROR;
		}
		else if (controlSocket_.ParsePwdReply(controlSocket_.response_)) {
			engine_.GetPathCache().Store(currentServer_, currentPath_, path_);

			if (subDir_.empty()) {
				return FZ_REPLY_OK;
			}

			target_.clear();
			opState = cwd_cwd_subdir;
			return FZ_REPLY_CONTINUE;
		}
		return FZ_REPLY_ERROR;
	case cwd_cwd_subdir:
		if (!successful || controlSocket_.response_.empty()) {
			if (link_discovery_) {
				LogMessage(MessageType::Debug_Info, L"Symlink does not link to a directory, probably a file");
				return FZ_REPLY_LINKNOTDIR;
			}
			else {
				return FZ_REPLY_ERROR;
			}
		}
		else if (controlSocket_.ParsePwdReply(controlSocket_.response_)) {
			engine_.GetPathCache().Store(currentServer_, currentPath_, path_, subDir_);

			return FZ_REPLY_OK;
		}
		return FZ_REPLY_ERROR;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown opState %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}
