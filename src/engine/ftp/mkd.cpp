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
			controlSocket_.m_CurrentPath = currentPath;
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
				if (controlSocket_.engine_.GetDirectoryCache().LookupFile(entry, currentServer(), currentPath, segments.back(), tmp, tmp) && !entry.is_dir()) {
					result = FZ_REPLY_ERROR;
				}
			}

			controlSocket_.engine_.GetDirectoryCache().UpdateFile(currentServer(), currentPath, segments.back(), true, CDirectoryCache::dir);
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
			controlSocket_.m_CurrentPath = currentPath;
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
	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::Send");

	LogMessage(MessageType::Debug_Debug, L"  state = %d", opState);

	if (!holdsLock) {
		if (!controlSocket_.TryLockCache(CFtpControlSocket::lock_mkdir, path)) {
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	bool res;
	switch (opState)
	{
	case mkd_findparent:
	case mkd_cwdsub:
		controlSocket_.m_CurrentPath.clear();
		res = controlSocket_.SendCommand(L"CWD " + currentPath.GetPath());
		break;
	case mkd_mkdsub:
		res = controlSocket_.SendCommand(L"MKD " + segments.back());
		break;
	case mkd_tryfull:
		res = controlSocket_.SendCommand(L"MKD " + path.GetPath());
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}

	if (!res) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_WOULDBLOCK;
}
