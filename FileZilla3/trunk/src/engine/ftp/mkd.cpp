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

int CFtpMkdirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpMkdirOpData::Send() in state %d", opState);

	if (!holdsLock_) {
		if (!controlSocket_.TryLockCache(CFtpControlSocket::lock_mkdir, path_)) {
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
		return controlSocket_.SendCommand(L"CWD " + currentMkdPath_.GetPath());
	case mkd_mkdsub:
		return controlSocket_.SendCommand(L"MKD " + segments_.back());
	case mkd_tryfull:
		return controlSocket_.SendCommand(L"MKD " + path_.GetPath());
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CFtpMkdirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpMkdirOpData::ParseResonse() in state %d", opState);

	int code = controlSocket_.GetReplyCode();
	switch (opState) {
	case mkd_findparent:
		if (code == 2 || code == 3) {
			currentPath_ = currentMkdPath_;
			opState = mkd_mkdsub;
		}
		else if (currentMkdPath_ == commonParent_) {
			opState = mkd_tryfull;
		}
		else if (currentMkdPath_.HasParent()) {
			CServerPath const parent = currentMkdPath_.GetParent();
			segments_.push_back(currentMkdPath_.GetLastSegment());
			currentMkdPath_ = parent;
		}
		else {
			opState = mkd_tryfull;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_mkdsub:
		if (code != 2 && code != 3) {
			// Don't fall back to using the full path if the error message
			// is "already exists".
			// Case 1: Full response a known "already exists" message.
			// Case 2: Substrng of response contains "already exists". path may not
			//         contain this substring as the path might be returned in the reply.
			// Case 3: Substrng of response contains "file exists". path may not
			//         contain this substring as the path might be returned in the reply.
			std::wstring const response = fz::str_tolower_ascii(controlSocket_.m_Response.substr(4));
			std::wstring const p = fz::str_tolower_ascii(path_.GetPath());
			if (response != L"directory already exists" &&
				(p.find(L"already exists") != std::wstring::npos ||
					response.find(L"already exists") == std::wstring::npos) &&
					(p.find(L"file exists") != std::wstring::npos ||
						response.find(L"file exists") == std::wstring::npos)
				)
			{
				opState = mkd_tryfull;
				break;
			}
		}

		{
			if (segments_.empty()) {
				LogMessage(MessageType::Debug_Warning, L"  segments is empty");
				return FZ_REPLY_INTERNALERROR;
			}

			// If entry did exist and is a file instead of a directory, report failure.
			int result = FZ_REPLY_OK;
			if (code != 2 && code != 3) {
				CDirentry entry;
				bool tmp;
				if (engine_.GetDirectoryCache().LookupFile(entry, currentServer_, currentMkdPath_, segments_.back(), tmp, tmp) && !entry.is_dir()) {
					result = FZ_REPLY_ERROR;
				}
			}

			engine_.GetDirectoryCache().UpdateFile(currentServer_, currentMkdPath_, segments_.back(), true, CDirectoryCache::dir);
			controlSocket_.SendDirectoryListingNotification(currentMkdPath_, false, false);

			currentMkdPath_.AddSegment(segments_.back());
			segments_.pop_back();

			if (segments_.empty() || result != FZ_REPLY_OK) {
				return result;
			}
			else {
				opState = mkd_cwdsub;
			}
		}
		return FZ_REPLY_CONTINUE;
	case mkd_cwdsub:
		if (code == 2 || code == 3) {
			currentPath_ = currentMkdPath_;
			opState = mkd_mkdsub;
		}
		else {
			opState = mkd_tryfull;
		}
		return FZ_REPLY_CONTINUE;
	case mkd_tryfull:
		if (code != 2 && code != 3) {
			return FZ_REPLY_ERROR;
		}
		else {
			return FZ_REPLY_OK;
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
	}

	return FZ_REPLY_INTERNALERROR;
}
