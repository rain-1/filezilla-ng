#include <filezilla.h>

#include "cwd.h"
#include "../pathcache.h"

int CFtpChangeDirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, "CFtpChangeDirOpData::Send()");

	std::wstring cmd;
	switch (opState)
	{
	case cwd_pwd:
	case cwd_pwd_cwd:
	case cwd_pwd_subdir:
		cmd = L"PWD";
		break;
	case cwd_cwd:
		if (tryMkdOnFail && !holdsLock) {
			if (controlSocket_.IsLocked(CFtpControlSocket::lock_mkdir, path)) {
				// Some other engine is already creating this directory or
				// performing an action that will lead to its creation
				tryMkdOnFail = false;
			}
			if (!controlSocket_.TryLockCache(CFtpControlSocket::lock_mkdir, path)) {
				return FZ_REPLY_WOULDBLOCK;
			}
		}
		cmd = L"CWD " + path.GetPath();
		controlSocket_.m_CurrentPath.clear();
		break;
	case cwd_cwd_subdir:
		if (subDir.empty()) {
			return FZ_REPLY_INTERNALERROR;
		}
		else if (subDir == L".." && !tried_cdup) {
			cmd = L"CDUP";
		}
		else {
			cmd = L"CWD " + path.FormatSubdir(subDir);
		}
		controlSocket_.m_CurrentPath.clear();
		break;
	}

	if (!cmd.empty()) {
		if (!controlSocket_.SendCommand(cmd)) {
			return FZ_REPLY_ERROR;
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}


int CFtpChangeDirOpData::ParseResponse()
{
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
			if (tryMkdOnFail) {
				tryMkdOnFail = false;

				// FIXME
				int res = controlSocket_.Mkdir(path);
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
			else {
				error = true;
			}
		}
		else {
			if (target.empty()) {
				opState = cwd_pwd_cwd;
			}
			else {
				controlSocket_.m_CurrentPath = target;
				if (subDir.empty()) {
					return FZ_REPLY_OK;
				}

				target.clear();
				opState = cwd_cwd_subdir;
			}
		}
		break;
	case cwd_pwd_cwd:
		if (code != 2 && code != 3) {
			LogMessage(MessageType::Debug_Warning, L"PWD failed, assuming path is '%s'.", path.GetPath());
			controlSocket_.m_CurrentPath = path;

			if (target.empty()) {
				controlSocket_.engine_.GetPathCache().Store(*currentServer(), controlSocket_.m_CurrentPath, path);
			}

			if (subDir.empty()) {
				return FZ_REPLY_OK;
			}
			else {
				opState = cwd_cwd_subdir;
			}
		}
		else if (controlSocket_.ParsePwdReply(response, false, path)) {
			if (target.empty()) {
				controlSocket_.engine_.GetPathCache().Store(*currentServer(), controlSocket_.m_CurrentPath, path);
			}
			if (subDir.empty()) {
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
			if (subDir == L".." && !tried_cdup && response.substr(0, 2) == L"50") {
				// CDUP command not implemented, try again using CWD ..
				tried_cdup = true;
			}
			else if (link_discovery) {
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
		    CServerPath assumedPath(path);
			if (subDir == L"..") {
				if (!assumedPath.HasParent()) {
					assumedPath.clear();
				}
				else {
					assumedPath = assumedPath.GetParent();
				}
			}
			else {
				assumedPath.AddSegment(subDir);
			}

			if (code != 2 && code != 3) {
				if (!assumedPath.empty()) {
					LogMessage(MessageType::Debug_Warning, L"PWD failed, assuming path is '%s'.", assumedPath.GetPath());
					controlSocket_.m_CurrentPath = assumedPath;

					if (target.empty()) {
						controlSocket_.engine_.GetPathCache().Store(*currentServer(), controlSocket_.m_CurrentPath, path, subDir);
					}

					return FZ_REPLY_OK;
				}
				else {
					LogMessage(MessageType::Debug_Warning, L"PWD failed, unable to guess current path.");
					error = true;
				}
			}
			else if (controlSocket_.ParsePwdReply(response, false, assumedPath)) {
				if (target.empty()) {
					controlSocket_.engine_.GetPathCache().Store(*currentServer(), controlSocket_.m_CurrentPath, path, subDir);
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
