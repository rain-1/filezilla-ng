#include <filezilla.h>

#include "directorycache.h"
#include "list.h"

enum listStates
{
	list_init = 0,
	list_waitcwd,
	list_waitlock,
	list_list
};

int CSftpListOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpListOpData::Send() in state %d", opState);

	if (opState == list_init) {
		if (!currentServer_) {
			LogMessage(MessageType::Debug_Warning, L"currenServer_ is empty");
			return FZ_REPLY_INTERNALERROR;
		}

		if (path_.GetType() == DEFAULT) {
			path_.SetType(currentServer_.GetType());
		}
		refresh_ = (flags_ & LIST_FLAG_REFRESH) != 0;
		fallback_to_current_ = !path_.empty() && (flags_ & LIST_FLAG_FALLBACK_CURRENT) != 0;

		controlSocket_.ChangeDir(path_, subDir_, (flags_ & LIST_FLAG_LINK) != 0);
		opState = list_waitcwd;
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == list_waitlock) {
		assert(subDir_.empty()); // We did do ChangeDir before trying to lock

		// Check if we can use already existing listing
		CDirectoryListing listing;
		bool is_outdated = false;
		bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, false, is_outdated);
		if (found && !is_outdated &&
			(!refresh_ || (holdsLock_ && listing.m_firstListTime >= time_before_locking_)))
		{
			controlSocket_.SendDirectoryListingNotification(listing.path, topLevel_, false);
			return FZ_REPLY_OK;
		}

		if (!holdsLock_) {
			if (!controlSocket_.TryLockCache(CSftpControlSocket::lock_list, currentPath_)) {
				time_before_locking_ = fz::monotonic_clock::now();
				return FZ_REPLY_WOULDBLOCK;
			}
		}

		opState = list_list;
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == list_list) {
		listing_parser_ = std::make_unique<CDirectoryListingParser>(&controlSocket_, currentServer_, listingEncoding::unknown);
		return controlSocket_.SendCommand(L"ls");
	}

	LogMessage(MessageType::Debug_Warning, L"Unknown opState in CSftpControlSocket::ListSend()");
	return FZ_REPLY_INTERNALERROR;
}

int CSftpListOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpListOpData::ParseResponse() in state %d", opState);

	if (opState == list_list) {
		if (controlSocket_.result_ != FZ_REPLY_OK) {
			return FZ_REPLY_ERROR;
		}

		if (!listing_parser_) {
			LogMessage(MessageType::Debug_Warning, L"listing_parser_ is empty");
			return FZ_REPLY_INTERNALERROR;
		}

		directoryListing_ = listing_parser_->Parse(currentPath_);
		engine_.GetDirectoryCache().Store(directoryListing_, currentServer_);
		controlSocket_.SendDirectoryListingNotification(currentPath_, topLevel_, false);

		return FZ_REPLY_OK;
	}

	LogMessage(MessageType::Debug_Warning, L"ListParseResponse called at inproper time: %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CSftpListOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpListOpData::SubcommandResult() in state %d", opState);

	if (opState != list_waitcwd) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (prevResult != FZ_REPLY_OK) {
		if (fallback_to_current_) {
			// List current directory instead
			fallback_to_current_ = false;
			path_.clear();
			subDir_.clear();
			controlSocket_.ChangeDir();
			return FZ_REPLY_CONTINUE;
		}
		else {
			return prevResult;
		}
	}

	path_ = currentPath_;
	subDir_.clear();
	opState = list_waitlock;
	return FZ_REPLY_CONTINUE;
}

int CSftpListOpData::ParseEntry(std::wstring && entry, std::wstring const& stime, std::wstring && name)
{
	if (opState != list_list) {
		controlSocket_.LogMessageRaw(MessageType::RawList, entry);
		LogMessage(MessageType::Debug_Warning, L"ListParseResponse called at inproper time: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}

	if (!listing_parser_) {
		controlSocket_.LogMessageRaw(MessageType::RawList, entry);
		LogMessage(MessageType::Debug_Warning, L"listing_parser_ is null");
		return FZ_REPLY_INTERNALERROR;
	}

	fz::datetime time;
	if (!stime.empty()) {
		int64_t t = std::wcstoll(stime.c_str(), 0, 10);
		if (t > 0) {
			time = fz::datetime(static_cast<time_t>(t), fz::datetime::seconds);
		}
	}
	listing_parser_->AddLine(std::move(entry), std::move(name), time);

	return FZ_REPLY_WOULDBLOCK;
}
