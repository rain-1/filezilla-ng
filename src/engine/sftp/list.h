#ifndef FILEZILLA_ENGINE_SFTP_LIST_HEADER
#define FILEZILLA_ENGINE_SFTP_LIST_HEADER

#include "directorylistingparser.h"
#include "sftpcontrolsocket.h"

class CSftpListOpData final : public COpData, public CSftpOpData
{
public:
	CSftpListOpData(CSftpControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int flags, bool topLevel)
		: COpData(Command::list)
		, CSftpOpData(controlSocket)
		, path_(path)
		, subDir_(subDir)
		, flags_(flags)
		, topLevel_(topLevel)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	int ParseEntry(std::wstring && entry, std::wstring const& stime, std::wstring && name);

private:
	std::unique_ptr<CDirectoryListingParser> listing_parser_;

	CServerPath path_;
	std::wstring subDir_; 
	
	int flags_{};

	// Set to true to get a directory listing even if a cache
	// lookup can be made after finding out true remote directory
	bool refresh_{};
	bool fallback_to_current_{};

	CDirectoryListing directoryListing_;

	fz::monotonic_clock time_before_locking_;

	bool topLevel_{};
};

#endif
