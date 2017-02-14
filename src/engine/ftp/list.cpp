#include <filezilla.h>

#include "../directorycache.h"
#include "../servercapabilities.h"
#include "list.h"
#include "transfersocket.h"

CFtpListOpData::CFtpListOpData(CFtpControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int flags)
    : COpData(Command::list)
    , CFtpOpData(controlSocket)
    , path_(path)
    , subDir_(subDir)
    , flags_(flags)
{
	opState = list_waitcwd;

	if (path_.GetType() == DEFAULT) {
		path_.SetType(currentServer().GetType());
	}
	refresh = (flags & LIST_FLAG_REFRESH) != 0;
	fallback_to_current = !path.empty() && (flags & LIST_FLAG_FALLBACK_CURRENT) != 0;
}

int CFtpListOpData::Send()
{
	controlSocket_.LogMessage(MessageType::Debug_Verbose, L"CFtpListOpData::ListSend()");
	controlSocket_.LogMessage(MessageType::Debug_Debug, L"  state = %d", opState);

	if (opState == list_waitcwd) {
		// FIXME
		int res = controlSocket_.ChangeDir(path_, subDir_, (flags_ & LIST_FLAG_LINK));
		return res;
	}
	if (opState == list_waitlock) {
		if (!holdsLock) {
			LogMessage(MessageType::Debug_Warning, L"CFtpListOpData::ListSend(): Not holding the lock as expected");
			return FZ_REPLY_INTERNALERROR;
		}

		// Check if we can use already existing listing
		CDirectoryListing listing;
		bool is_outdated = false;
		assert(subDir_.empty()); // Did do ChangeDir before trying to lock
		bool found = controlSocket_.engine_.GetDirectoryCache().Lookup(listing, currentServer(), path_, true, is_outdated);
		if (found && !is_outdated && !listing.get_unsure_flags() &&
		    listing.m_firstListTime >= m_time_before_locking)
		{
			controlSocket_.SendDirectoryListingNotification(listing.path, !pNextOpData, false);
			return FZ_REPLY_OK;
		}

		opState = list_waitcwd;

		// FIXME
		return FZ_REPLY_INTERNALERROR;
		//return ListSubcommandResult(FZ_REPLY_OK);
	}
	if (opState == list_mdtm) {
		LogMessage(MessageType::Status, _("Calculating timezone offset of server..."));
		std::wstring cmd = L"MDTM " + controlSocket_.m_CurrentPath.FormatFilename(directoryListing[mdtm_index].name, true);
		if (!controlSocket_.SendCommand(cmd)) {
			return FZ_REPLY_ERROR;
		}
		else {
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	LogMessage(MessageType::Debug_Warning, L"invalid opstate %d", opState);
	return FZ_REPLY_INTERNALERROR;
}


int CFtpListOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpListOpData::ParseResponse()");

	if (opState != list_mdtm) {
		LogMessage(MessageType::Debug_Warning, "CFtpListOpData::ParseResponse should never be called if opState != list_mdtm");
		return FZ_REPLY_INTERNALERROR;
	}

	std::wstring const& response = controlSocket_.m_Response;

	// First condition prevents problems with concurrent MDTM
	if (CServerCapabilities::GetCapability(currentServer(), timezone_offset) == unknown &&
	    response.substr(0, 4) == L"213 " && response.size() > 16)
	{
		fz::datetime date(response.substr(4), fz::datetime::utc);
		if (!date.empty()) {
			assert(directoryListing[mdtm_index].has_date());
			fz::datetime listTime = directoryListing[mdtm_index].time;
			listTime -= fz::duration::from_minutes(currentServer().GetTimezoneOffset());

			int serveroffset = static_cast<int>((date - listTime).get_seconds());
			if (!directoryListing[mdtm_index].has_seconds()) {
				// Round offset to full minutes
				if (serveroffset < 0) {
					serveroffset -= 59;
				}
				serveroffset -= serveroffset % 60;
			}

			LogMessage(MessageType::Status, L"Timezone offset of server is %d seconds.", -serveroffset);

			fz::duration span = fz::duration::from_seconds(serveroffset);
			const int count = directoryListing.GetCount();
			for (int i = 0; i < count; ++i) {
				CDirentry& entry = directoryListing.get(i);
				entry.time += span;
			}

			// TODO: Correct cached listings

			CServerCapabilities::SetCapability(currentServer(), timezone_offset, yes, serveroffset);
		}
		else {
			CServerCapabilities::SetCapability(currentServer(), mdtm_command, no);
			CServerCapabilities::SetCapability(currentServer(), timezone_offset, no);
		}
	}
	else {
		CServerCapabilities::SetCapability(currentServer(), timezone_offset, no);
	}

	controlSocket_.engine_.GetDirectoryCache().Store(directoryListing, currentServer());

	controlSocket_.SendDirectoryListingNotification(controlSocket_.m_CurrentPath, !pNextOpData, false);

	return FZ_REPLY_OK;
}


int CFtpListOpData::SubcommandResult(int prevResult, COpData const& previousOperation)
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpControlSocket::ListSubcommandResult()");

	LogMessage(MessageType::Debug_Debug, L"  state = %d", opState);

	if (opState == list_waitcwd) {
		if (prevResult != FZ_REPLY_OK) {
			if ((prevResult & FZ_REPLY_LINKNOTDIR) == FZ_REPLY_LINKNOTDIR) {
				return prevResult;
			}

			if (fallback_to_current) {
				// List current directory instead
				fallback_to_current = false;
				path_.clear();
				subDir_.clear();
				int res = controlSocket_.ChangeDir();
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
			else {
				return prevResult;
			}
		}
		if (path_.empty()) {
			path_ = controlSocket_.m_CurrentPath;
			assert(subDir_.empty());
			assert(!path_.empty());
		}

		if (!refresh) {
			assert(!pNextOpData);

			// Do a cache lookup now that we know the correct directory
			int hasUnsureEntries;
			bool is_outdated = false;
			bool found = controlSocket_.engine_.GetDirectoryCache().DoesExist(currentServer(), controlSocket_.m_CurrentPath, hasUnsureEntries, is_outdated);
			if (found) {
				// We're done if listing is recent and has no outdated entries
				if (!is_outdated && !hasUnsureEntries) {
					controlSocket_.SendDirectoryListingNotification(controlSocket_.m_CurrentPath, !pNextOpData, false);

					return FZ_REPLY_OK;
				}
			}
		}

		if (!holdsLock) {
			if (!controlSocket_.TryLockCache(CFtpControlSocket::lock_list, controlSocket_.m_CurrentPath)) {
				opState = list_waitlock;
				m_time_before_locking = fz::monotonic_clock::now();
				return FZ_REPLY_WOULDBLOCK;
			}
		}

		controlSocket_.m_pTransferSocket.reset();
		controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(controlSocket_.engine_, controlSocket_, TransferMode::list);

		// Assume that a server supporting UTF-8 does not send EBCDIC listings.
		listingEncoding::type encoding = listingEncoding::unknown;
		if (CServerCapabilities::GetCapability(currentServer(), utf8_command) == yes) {
			encoding = listingEncoding::normal;
		}

		m_pDirectoryListingParser = std::make_unique<CDirectoryListingParser>(&controlSocket_, currentServer(), encoding);

		m_pDirectoryListingParser->SetTimezoneOffset(controlSocket_.GetTimezoneOffset());
		controlSocket_.m_pTransferSocket->m_pDirectoryListingParser = m_pDirectoryListingParser.get();

		controlSocket_.engine_.transfer_status_.Init(-1, 0, true);

		opState = list_waittransfer;
		if (CServerCapabilities::GetCapability(currentServer(), mlsd_command) == yes) {
			return controlSocket_.Transfer(L"MLSD", this);
		}
		else {
			if (controlSocket_.engine_.GetOptions().GetOptionVal(OPTION_VIEW_HIDDEN_FILES)) {
				capabilities cap = CServerCapabilities::GetCapability(currentServer(), list_hidden_support);
				if (cap == unknown) {
					viewHiddenCheck = true;
				}
				else if (cap == yes) {
					viewHidden = true;
				}
				else {
					LogMessage(MessageType::Debug_Info, _("View hidden option set, but unsupported by server"));
				}
			}

			if (viewHidden) {
				return controlSocket_.Transfer(L"LIST -a", this);
			}
			else {
				return controlSocket_.Transfer(L"LIST", this);
			}
		}
	}
	else if (opState == list_waittransfer) {
		if (prevResult == FZ_REPLY_OK) {
			CDirectoryListing listing = m_pDirectoryListingParser->Parse(controlSocket_.m_CurrentPath);

			if (viewHiddenCheck) {
				if (!viewHidden) {
					// Repeat with LIST -a
					viewHidden = true;
					directoryListing = listing;

					// Reset status
					transferEndReason = TransferEndReason::successful;
					tranferCommandSent = false;
					controlSocket_.m_pTransferSocket.reset();
					controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(controlSocket_.engine_, controlSocket_, TransferMode::list);
					m_pDirectoryListingParser->Reset();
					controlSocket_.m_pTransferSocket->m_pDirectoryListingParser = m_pDirectoryListingParser.get();

					return controlSocket_.Transfer(L"LIST -a", this);
				}
				else {
					if (controlSocket_.CheckInclusion(listing, directoryListing)) {
						LogMessage(MessageType::Debug_Info, L"Server seems to support LIST -a");
						CServerCapabilities::SetCapability(currentServer(), list_hidden_support, yes);
					}
					else {
						LogMessage(MessageType::Debug_Info, L"Server does not seem to support LIST -a");
						CServerCapabilities::SetCapability(currentServer(), list_hidden_support, no);
						listing = directoryListing;
					}
				}
			}

			controlSocket_.SetAlive();

			int res = controlSocket_.ListCheckTimezoneDetection(listing);
			if (res != FZ_REPLY_OK) {
				return res;
			}

			controlSocket_.engine_.GetDirectoryCache().Store(listing, currentServer());

			controlSocket_.SendDirectoryListingNotification(controlSocket_.m_CurrentPath, !pNextOpData, false);

			return FZ_REPLY_OK;
		}
		else {
			if (tranferCommandSent && controlSocket_.IsMisleadingListResponse()) {
				CDirectoryListing listing;
				listing.path = controlSocket_.m_CurrentPath;
				listing.m_firstListTime = fz::monotonic_clock::now();

				if (viewHiddenCheck) {
					if (viewHidden) {
						if (directoryListing.GetCount()) {
							// Less files with LIST -a
							// Not supported
							LogMessage(MessageType::Debug_Info, L"Server does not seem to support LIST -a");
							CServerCapabilities::SetCapability(currentServer(), list_hidden_support, no);
							listing = directoryListing;
						}
						else {
							LogMessage(MessageType::Debug_Info, L"Server seems to support LIST -a");
							CServerCapabilities::SetCapability(currentServer(), list_hidden_support, yes);
						}
					}
					else {
						// Reset status
						transferEndReason = TransferEndReason::successful;
						tranferCommandSent = false;
						controlSocket_.m_pTransferSocket.reset();
						controlSocket_.m_pTransferSocket = std::make_unique<CTransferSocket>(controlSocket_.engine_, controlSocket_, TransferMode::list);
						m_pDirectoryListingParser->Reset();
						controlSocket_.m_pTransferSocket->m_pDirectoryListingParser = m_pDirectoryListingParser.get();

						// Repeat with LIST -a
						viewHidden = true;
						directoryListing = listing;
						return controlSocket_.Transfer(L"LIST -a", this);
					}
				}

				int res = controlSocket_.ListCheckTimezoneDetection(listing);
				if (res != FZ_REPLY_OK) {
					return res;
				}

				controlSocket_.engine_.GetDirectoryCache().Store(listing, currentServer());

				controlSocket_.SendDirectoryListingNotification(controlSocket_.m_CurrentPath, !pNextOpData, false);

				return FZ_REPLY_OK;
			}
			else {
				if (viewHiddenCheck) {
					// If server does not support LIST -a, the server might reject this command
					// straight away. In this case, back to the previously retrieved listing.
					// On other failures like timeouts and such, return an error
					if (viewHidden &&
						transferEndReason == TransferEndReason::transfer_command_failure_immediate)
					{
						CServerCapabilities::SetCapability(currentServer(), list_hidden_support, no);

						int res = controlSocket_.ListCheckTimezoneDetection(directoryListing);
						if (res != FZ_REPLY_OK) {
							return res;
						}

						controlSocket_.engine_.GetDirectoryCache().Store(directoryListing, currentServer());

						controlSocket_.SendDirectoryListingNotification(controlSocket_.m_CurrentPath, !pNextOpData, false);

						return FZ_REPLY_OK;
					}
				}

				if (prevResult & FZ_REPLY_ERROR) {
					controlSocket_.SendDirectoryListingNotification(controlSocket_.m_CurrentPath, !pNextOpData, true);
				}
			}

			return FZ_REPLY_ERROR;
		}
	}
	else {
		LogMessage(MessageType::Debug_Warning, L"Wrong opState: %d", opState);
		return FZ_REPLY_INTERNALERROR;
	}
}
