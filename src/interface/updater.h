#ifndef FILEZILLA_UPDATER_HEADER
#define FILEZILLA_UPDATER_HEADER

#if FZ_MANUALUPDATECHECK

struct build
{
	wxString url_;
	wxString version_;
	wxString hash_;
	wxULongLong size_;
};

struct version_information
{
	bool empty() const {
		return available_.version_.empty();
	}

	void update_available();

	build stable_;
	build beta_;
	build nightly_;

	build available_;

	wxString changelog;
};

class CUpdaterOptions;
class CUpdater : wxEvtHandler
{
public:
	enum state
	{
		idle,
		failed,
		checking,
		newversion, // There is a new version available, user needs to manually download
		newversion_downloading, // There is a new version available, file is being downloaded
		newversion_ready // There is a new version available, file has been downloaded
	};

	CUpdater();
	virtual ~CUpdater();

	bool Run();

protected:
	int Download(wxString const& url, wxString const& local_file = _T(""));

	int SendConnectCommand(wxString const& url);
	int SendTransferCommand(wxString const& url, wxString const& local_file);

	wxString GetUrl();
	void ProcessNotification(CNotification* notification);
	void ProcessOperation(CNotification* notification);
	void ProcessData(CNotification* notification);
	void ParseData();
	void ProcessFinishedDownload();

	bool VerifyChecksum( wxString const& file, wxULongLong size, wxString const& checksum ) const;

	wxString GetTempFile() const;
	CLocalPath GetDownloadDir() const;
	wxString GetFilename( wxString const& url) const;
	wxString GetLocalFile( build const& b, bool allow_existing ) const;

	DECLARE_EVENT_TABLE()
	void OnEngineEvent(wxEvent& ev);
	void OnTimer(wxTimerEvent& ev);

	state state_;
	wxString local_file_;
	CFileZillaEngine* engine_;
	CUpdaterOptions* update_options_;

	wxString raw_version_information_;

	version_information version_information_;
};

#endif //FZ_MANUALUPDATECHECK

#endif
