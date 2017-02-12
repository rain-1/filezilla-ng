#include <filezilla.h>

#include "../directorycache.h"
#include "../servercapabilities.h"
#include "list.h"

CFtpListOpData::CFtpListOpData(CFtpControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int flags)
    : COpData(Command::list)
    , CFtpOpData(controlSocket)
    , path_(path)
    , subDir_(subDir)
    , flags_(flags)
{
	opState = list_waitcwd;

	if (path_.GetType() == DEFAULT) {
		path_.SetType(currentServer()->GetType());
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
		bool found = controlSocket_.engine_.GetDirectoryCache().Lookup(listing, *currentServer(), path_, true, is_outdated);
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
	if (CServerCapabilities::GetCapability(*currentServer(), timezone_offset) == unknown &&
	    response.substr(0, 4) == L"213 " && response.size() > 16)
	{
		fz::datetime date(response.substr(4), fz::datetime::utc);
		if (!date.empty()) {
			assert(directoryListing[mdtm_index].has_date());
			fz::datetime listTime = directoryListing[mdtm_index].time;
			listTime -= fz::duration::from_minutes(currentServer()->GetTimezoneOffset());

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

			CServerCapabilities::SetCapability(*currentServer(), timezone_offset, yes, serveroffset);
		}
		else {
			CServerCapabilities::SetCapability(*currentServer(), mdtm_command, no);
			CServerCapabilities::SetCapability(*currentServer(), timezone_offset, no);
		}
	}
	else {
		CServerCapabilities::SetCapability(*currentServer(), timezone_offset, no);
	}

	controlSocket_.engine_.GetDirectoryCache().Store(directoryListing, *currentServer());

	controlSocket_.SendDirectoryListingNotification(controlSocket_.m_CurrentPath, !pNextOpData, false);

	return FZ_REPLY_OK;
}
