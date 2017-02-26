#include <filezilla.h>

#include "cwd.h"
#include "../pathcache.h"

enum cwdStates
{
	cwd_init = 0,
	cwd_pwd,
	cwd_cwd,
	cwd_pwd_cwd,
	cwd_cwd_subdir,
	cwd_pwd_subdir
};

int CFtpChangeDirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpChangeDirOpData::Send() in state %d", opState);

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
	case cwd_pwd_cwd:
	case cwd_pwd_subdir:
		cmd = L"PWD";
		break;
	case cwd_cwd:
		if (tryMkdOnFail_ && !holdsLock_) {
			if (controlSocket_.IsLocked(CFtpControlSocket::lock_mkdir, path_)) {
				// Some other engine is already creating this directory or
				// performing an action that will lead to its creation
				tryMkdOnFail_ = false;
			}
			if (!controlSocket_.TryLockCache(CFtpControlSocket::lock_mkdir, path_)) {
				return FZ_REPLY_WOULDBLOCK;
			}
		}
		cmd = L"CWD " + path_.GetPath();
		currentPath_.clear();
		break;
	case cwd_cwd_subdir:
		if (subDir_.empty()) {
			return FZ_REPLY_INTERNALERROR;
		}
		else if (subDir_ == L".." && !tried_cdup_) {
			cmd = L"CDUP";
		}
		else {
			cmd = L"CWD " + path_.FormatSubdir(subDir_);
		}
		currentPath_.clear();
		break;
	}

	if (!cmd.empty()) {
		return controlSocket_.SendCommand(cmd);
	}

	return FZ_REPLY_WOULDBLOCK;
}


int CFtpChangeDirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpChangeDirOpData::ParseResponse() in state %d", opState);

	std::wstring const& response = controlSocket_.m_Response;
	int code = controlSocket_.GetReplyCode();

	bool error = false;
	switch (opState)
	{
	case cwd_pwd:
		if (code != 2 && code != 3) {
			error = true;
		}
		else if (controlSocket_.ParsePwdReply(response)) {
			return FZ_REPLY_OK;
		}
		else {
			error = true;
		}
		break;
	case cwd_cwd:
		if (code != 2 && code != 3) {
			// Create remote directory if part of a file upload
			if (tryMkdOnFail_) {
				tryMkdOnFail_ = false;

				controlSocket_.Mkdir(path_);
				return FZ_REPLY_CONTINUE;
			}
			else {
				error = true;
			}
		}
		else {
			if (target_.empty()) {
				opState = cwd_pwd_cwd;
			}
			else {
				currentPath_ = target_;
				if (subDir_.empty()) {
					return FZ_REPLY_OK;
				}

				target_.clear();
				opState = cwd_cwd_subdir;
			}
		}
		break;
	case cwd_pwd_cwd:
		if (code != 2 && code != 3) {
			LogMessage(MessageType::Debug_Warning, L"PWD failed, assuming path is '%s'.", path_.GetPath());
			currentPath_ = path_;

			if (target_.empty()) {
				engine_.GetPathCache().Store(currentServer_, currentPath_, path_);
			}

			if (subDir_.empty()) {
				return FZ_REPLY_OK;
			}
			else {
				opState = cwd_cwd_subdir;
			}
		}
		else if (controlSocket_.ParsePwdReply(response, false, path_)) {
			if (target_.empty()) {
				engine_.GetPathCache().Store(currentServer_, currentPath_, path_);
			}
			if (subDir_.empty()) {
				return FZ_REPLY_OK;
			}
			else {
				opState = cwd_cwd_subdir;
			}
		}
		else {
			error = true;
		}
		break;
	case cwd_cwd_subdir:
		if (code != 2 && code != 3) {
			if (subDir_ == L".." && !tried_cdup_ && response.substr(0, 2) == L"50") {
				// CDUP command not implemented, try again using CWD ..
				tried_cdup_ = true;
			}
			else if (link_discovery_) {
				LogMessage(MessageType::Debug_Info, L"Symlink does not link to a directory, probably a file");
				return FZ_REPLY_LINKNOTDIR;
			}
			else {
				error = true;
			}
		}
		else {
			opState = cwd_pwd_subdir;
		}
		break;
	case cwd_pwd_subdir:
	    {
		    CServerPath assumedPath(path_);
			if (subDir_ == L"..") {
				if (!assumedPath.HasParent()) {
					assumedPath.clear();
				}
				else {
					assumedPath = assumedPath.GetParent();
				}
			}
			else {
				assumedPath.AddSegment(subDir_);
			}

			if (code != 2 && code != 3) {
				if (!assumedPath.empty()) {
					LogMessage(MessageType::Debug_Warning, L"PWD failed, assuming path is '%s'.", assumedPath.GetPath());
					currentPath_ = assumedPath;

					if (target_.empty()) {
						engine_.GetPathCache().Store(currentServer_, currentPath_, path_, subDir_);
					}

					return FZ_REPLY_OK;
				}
				else {
					LogMessage(MessageType::Debug_Warning, L"PWD failed, unable to guess current path.");
					error = true;
				}
			}
			else if (controlSocket_.ParsePwdReply(response, false, assumedPath)) {
				if (target_.empty()) {
					engine_.GetPathCache().Store(currentServer_, currentPath_, path_, subDir_);
				}

				return FZ_REPLY_OK;
			}
			else {
				error = true;
			}
	    }
		break;
	}

	if (error) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_CONTINUE;
}
