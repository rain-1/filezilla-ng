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

enum UpdaterState
{
	idle,
	failed,
	checking,
	newversion, // There is a new version available, user needs to manually download
	newversion_downloading, // There is a new version available, file is being downloaded
	newversion_ready // There is a new version available, file has been downloaded
};

class CUpdateHandler
{
public:
	virtual void UpdaterStateChanged( UpdaterState s, build const& v ) = 0;
};

class CUpdaterOptions;
class CUpdater : wxEvtHandler
{
public:
	CUpdater( CUpdateHandler& parent );
	virtual ~CUpdater();

	bool Run();

	UpdaterState GetState() const { return state_; }
	build AvailableBuild() const { return version_information_.available_; }

	wxString DownloadedFile() const;
protected:
	int Download(wxString const& url, wxString const& local_file = _T(""));

	int SendConnectCommand(wxString const& url);
	int SendTransferCommand(wxString const& url, wxString const& local_file);

	wxString GetUrl();
	void ProcessNotification(CNotification* notification);
	void ProcessOperation(CNotification* notification);
	void ProcessData(CNotification* notification);
	void ParseData();
	UpdaterState ProcessFinishedDownload();

	bool VerifyChecksum( wxString const& file, wxULongLong size, wxString const& checksum ) const;

	wxString GetTempFile() const;
	CLocalPath GetDownloadDir() const;
	wxString GetFilename( wxString const& url) const;
	wxString GetLocalFile( build const& b, bool allow_existing ) const;

	void SetState( UpdaterState s );

	DECLARE_EVENT_TABLE()
	void OnEngineEvent(wxEvent& ev);
	void OnTimer(wxTimerEvent& ev);

	UpdaterState state_;
	wxString local_file_;
	CFileZillaEngine* engine_;
	CUpdaterOptions* update_options_;

	wxString raw_version_information_;

	version_information version_information_;

	CUpdateHandler& parent_;
};

#endif //FZ_MANUALUPDATECHECK

#endif
