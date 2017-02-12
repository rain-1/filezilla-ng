#include <filezilla.h>

#include "../directorycache.h"
#include "list.h"

CFtpListOpData::CFtpListOpData(CFtpControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int flags)
    : COpData(Command::list)
    , CFtpOpData(controlSocket)
    , path_(path)
    , subDir_(subDir)
    , flags_(flags)
{
	opState = list_cwd;

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

	if (opState == list_waitlock) {
		int res = controlSocket_.ChangeDir(path_, subDir_, (flags_ & LIST_FLAG_LINK));
		if (res & FZ_REPLY_ERROR) {
			return res;
		}
		return FZ_REPLY_CONTINUE;
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
