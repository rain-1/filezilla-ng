#include <filezilla.h>

#include "directorycache.h"
#include "list.h"

enum listStates
{
	list_init = 0,
	list_waitresolve,
	list_waitlock,
	list_list
};

int CStorjListOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjListOpData::Send() in state %d", opState);

	switch (opState) {
	case list_init:
		if (!subDir_.empty()) {
			LogMessage(MessageType::Error, _("Invalid path"));
			return FZ_REPLY_ERROR;
		}

		if (path_.empty()) {
			path_ = CServerPath(L"/");
		}

		currentPath_ = path_;

		if (!currentServer_) {
			LogMessage(MessageType::Debug_Warning, L"CStorjControlSocket::List called with m_pCurrenServer == 0");
			return FZ_REPLY_INTERNALERROR;
		}

		if (currentPath_.GetType() != ServerType::UNIX) {
			LogMessage(MessageType::Debug_Warning, L"CStorControlSocket::List called with incompatible server type %d in path", currentPath_.GetType());
			return FZ_REPLY_INTERNALERROR;
		}

		opState = list_waitresolve;
		controlSocket_.Resolve(path_, std::wstring(), bucket_);
		return FZ_REPLY_CONTINUE;
	case list_waitlock:
		if (!holdsLock_) {
			LogMessage(MessageType::Debug_Warning, L"Not holding the lock as expected");
			return FZ_REPLY_INTERNALERROR;
		}

		{
			// Check if we can use already existing listing
			CDirectoryListing listing;
			bool is_outdated = false;
			bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, false, is_outdated);
			if (found && !is_outdated &&
				listing.m_firstListTime >= time_before_locking_)
			{
				controlSocket_.SendDirectoryListingNotification(listing.path, topLevel_, false);
				return FZ_REPLY_OK;
			}
		}
		opState = list_list;
		return FZ_REPLY_CONTINUE;
	case list_list:
		if (bucket_.empty()) {
			return controlSocket_.SendCommand(L"list-buckets");
		}
		else {
			std::wstring path = path_.GetPath();
			auto pos = path.find('/', 1);
			if (pos == std::string::npos) {
				path.clear();
			}
			else {
				path = controlSocket_.QuoteFilename(path.substr(pos + 1) + L"/");
			}

			return controlSocket_.SendCommand(L"list " + bucket_ + L" " + path);
		}
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjListOpData::ListSend()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjListOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjListOpData::ParseResponse() in state %d", opState);

	if (opState == list_list) {
		if (controlSocket_.result_ != FZ_REPLY_OK) {
			return controlSocket_.result_;
		}
		directoryListing_.path = path_;

		directoryListing_.m_firstListTime = fz::monotonic_clock::now();

		engine_.GetDirectoryCache().Store(directoryListing_, currentServer_);
		controlSocket_.SendDirectoryListingNotification(directoryListing_.path, topLevel_, false);

		currentPath_ = path_;
		return FZ_REPLY_OK;
	}

	LogMessage(MessageType::Debug_Warning, L"ListParseResponse called at inproper time: %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CStorjListOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjListOpData::SubcommandResult() in state %d", opState);

	if (prevResult != FZ_REPLY_OK) {
		return prevResult;
	}

	switch (opState) {
	case list_waitresolve:
		opState = list_waitlock;
		if (!controlSocket_.TryLockCache(CStorjControlSocket::lock_list, path_)) {
			time_before_locking_ = fz::monotonic_clock::now();
			return FZ_REPLY_WOULDBLOCK;
		}

		opState = list_list;
		return FZ_REPLY_CONTINUE;
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CStorjListOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjListOpData::ParseEntry(std::wstring && name, std::wstring const& size, std::wstring && id, std::wstring const& created)
{
	if (opState != list_list) {
		LogMessage(MessageType::Debug_Warning, L"ListParseResponse called at inproper time: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}

	if (name == L".") {
		pathId_ = id;
		return FZ_REPLY_WOULDBLOCK;
	}

	CDirentry entry;
	entry.name = name;
	entry.ownerGroup.get() = id;
	if (bucket_.empty()) {
		entry.flags = CDirentry::flag_dir;
	}
	else {
		if (!entry.name.empty() && entry.name.back() == '/') {
			entry.flags = CDirentry::flag_dir;
			entry.name.pop_back();
		}
		else {
			entry.flags = 0;
		}
	}

	if (entry.is_dir()) {
		entry.size = -1;
	}
	else {
		entry.size = fz::to_integral<int64_t>(size, -1);
	}

	entry.time.set(created, fz::datetime::utc);

	if (!entry.name.empty()) {
		directoryListing_.Append(std::move(entry));
	}

	return FZ_REPLY_WOULDBLOCK;
}
