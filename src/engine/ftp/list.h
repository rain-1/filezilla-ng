#ifndef FILEZILLA_ENGINE_FTP_LIST_HEADER
#define FILEZILLA_ENGINE_FTP_LIST_HEADER

#include "directorylistingparser.h"
#include "ftpcontrolsocket.h"

class CDirectoryListingParser;

enum listStates
{
	list_waitcwd,
	list_waitlock,
	list_waittransfer,
	list_mdtm
};

class CFtpListOpData final : public COpData, public CFtpOpData, public CFtpTransferOpData
{
public:
	CFtpListOpData(CFtpControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int flags);

	virtual int Send() override;
	virtual int ParseResponse() override;

	CServerPath path_;
	std::wstring subDir_;
	bool fallback_to_current{};

	std::unique_ptr<CDirectoryListingParser> m_pDirectoryListingParser;

	CDirectoryListing directoryListing;

	int flags_{};

	// Set to true to get a directory listing even if a cache
	// lookup can be made after finding out true remote directory
	bool refresh{};

	bool viewHiddenCheck{};
	bool viewHidden{}; // Uses LIST -a command

	// Listing index for list_mdtm
	int mdtm_index{};

	fz::monotonic_clock m_time_before_locking;
};

#endif
