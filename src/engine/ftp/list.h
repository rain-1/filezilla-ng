#ifndef FILEZILLA_ENGINE_FTP_LIST_HEADER
#define FILEZILLA_ENGINE_FTP_LIST_HEADER

#include "directorylistingparser.h"
#include "ftpcontrolsocket.h"

class CDirectoryListingParser;

enum listStates
{
	list_init,
	list_waitcwd,
	list_waitlock,
	list_waittransfer,
	list_mdtm
};

class CFtpListOpData final : public COpData, public CFtpOpData, public CFtpTransferOpData
{
public:
	CFtpListOpData(CFtpControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int flags, bool topLevel);

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

private:
	int CheckTimezoneDetection(CDirectoryListing& listing);

	CServerPath path_;
	std::wstring subDir_;
	bool fallback_to_current_{};

	std::unique_ptr<CDirectoryListingParser> listing_parser_;

	CDirectoryListing directoryListing_;

	int flags_{};

	// Set to true to get a directory listing even if a cache
	// lookup can be made after finding out true remote directory
	bool refresh_{};

	bool viewHiddenCheck_{};
	bool viewHidden_{}; // Uses LIST -a command

	// Listing index for list_mdtm
	int mdtm_index_{};

	fz::monotonic_clock time_before_locking_;

	bool topLevel_{};
};

#endif
