#include <filezilla.h>

#include "directorycache.h"
#include "mkd.h"


int CFtpMkdirOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpMkdirOpData::ParseResonse");

	LogMessage(MessageType::Debug_Debug, L"  state = %d", opState);

	int code = controlSocket_.GetReplyCode();
	bool error = false;
	switch (opState) {
	case mkd_findparent:
		if (code == 2 || code == 3) {
			currentPath_ = currentPath;
			opState = mkd_mkdsub;
		}
		else if (currentPath == commonParent) {
			opState = mkd_tryfull;
		}
		else if (currentPath.HasParent()) {
			const CServerPath& parent = currentPath.GetParent();
			segments.push_back(currentPath.GetLastSegment());
			currentPath = parent;
		}
		else {
			opState = mkd_tryfull;
		}
		break;
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
			std::wstring const p = fz::str_tolower_ascii(path.GetPath());
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
			if (segments.empty()) {
				LogMessage(MessageType::Debug_Warning, L"  segments is empty");
				return FZ_REPLY_INTERNALERROR;
			}

			// If entry did exist and is a file instead of a directory, report failure.
			int result = FZ_REPLY_OK;
			if (code != 2 && code != 3) {
				CDirentry entry;
				bool tmp;
				if (engine_.GetDirectoryCache().LookupFile(entry, currentServer_, currentPath, segments.back(), tmp, tmp) && !entry.is_dir()) {
					result = FZ_REPLY_ERROR;
				}
			}

			engine_.GetDirectoryCache().UpdateFile(currentServer_, currentPath, segments.back(), true, CDirectoryCache::dir);
			controlSocket_.SendDirectoryListingNotification(currentPath, false, false);

			currentPath.AddSegment(segments.back());
			segments.pop_back();

			if (segments.empty() || result != FZ_REPLY_OK) {
				return result;
			}
			else {
				opState = mkd_cwdsub;
			}
		}
		break;
	case mkd_cwdsub:
		if (code == 2 || code == 3) {
			currentPath_ = currentPath;
			opState = mkd_mkdsub;
		}
		else {
			opState = mkd_tryfull;
		}
		break;
	case mkd_tryfull:
		if (code != 2 && code != 3) {
			error = true;
		}
		else {
			return FZ_REPLY_OK;
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}

	if (error) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_CONTINUE;
}

int CFtpMkdirOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpMkdirOpData::Send()");

	LogMessage(MessageType::Debug_Debug, L"  state = %d", opState);

	if (!holdsLock_) {
		if (!controlSocket_.TryLockCache(CFtpControlSocket::lock_mkdir, path)) {
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	switch (opState)
	{
	case mkd_init:
		if (!currentPath_.empty()) {
			// Unless the server is broken, a directory already exists if current directory is a subdir of it.
			if (currentPath_ == path || currentPath_.IsSubdirOf(path, false)) {
				return FZ_REPLY_OK;
			}

			if (currentPath_.IsParentOf(path, false)) {
				commonParent = currentPath_;
			}
			else {
				commonParent = path.GetCommonParent(currentPath_);
			}
		}

		if (!path.HasParent()) {
			opState = mkd_tryfull;
		}
		else {
			currentPath = path.GetParent();
			segments.push_back(path.GetLastSegment());

			if (currentPath == currentPath_) {
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
		return controlSocket_.SendCommand(L"CWD " + currentPath.GetPath());
	case mkd_mkdsub:
		return controlSocket_.SendCommand(L"MKD " + segments.back());
	case mkd_tryfull:
		return controlSocket_.SendCommand(L"MKD " + path.GetPath());
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}
