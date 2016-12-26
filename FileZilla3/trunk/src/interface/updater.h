#ifndef FILEZILLA_UPDATER_HEADER
#define FILEZILLA_UPDATER_HEADER

#if FZ_MANUALUPDATECHECK

#include "notification.h"

#include <wx/timer.h>

struct build
{
	wxString url_;
	wxString version_;
	wxString hash_;
	int64_t size_{-1};
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

	wxString changelog_;

	wxString resources_;
};

enum class UpdaterState
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

class CFileZillaEngineContext;
class CUpdater final : public wxEvtHandler, private EngineNotificationHandler
{
public:
	CUpdater(CUpdateHandler& parent, CFileZillaEngineContext& engine_context);
	virtual ~CUpdater();

	// 2-Stage initialization
	void Init();

	void AddHandler( CUpdateHandler& handler );
	void RemoveHandler( CUpdateHandler& handler );

	UpdaterState GetState() const { return state_; }
	build AvailableBuild() const { return version_information_.available_; }
	wxString GetChangelog() const { return version_information_.changelog_; }
	wxString GetResources() const { return version_information_.resources_; }

	wxString DownloadedFile() const;

	int64_t BytesDownloaded() const; // Returns -1 on error

	wxString GetLog() const { return log_; }

	bool LongTimeSinceLastCheck() const;

	static CUpdater* GetInstance();

	bool UpdatableBuild() const;

	void RunIfNeeded();

protected:
	int Download(wxString const& url, std::wstring const& local_file = std::wstring());
	int ContinueDownload();

	void AutoRunIfNeeded();
	bool Run();

	bool CreateConnectCommand(wxString const& url);
	bool CreateTransferCommand(wxString const& url, std::wstring const& local_file);

	wxString GetUrl();
	void ProcessNotification(std::unique_ptr<CNotification> && notification);
	void ProcessOperation(COperationNotification const& operation);
	void ProcessData(CDataNotification& dataNotification);
	void ParseData();
	UpdaterState ProcessFinishedDownload();
	UpdaterState ProcessFinishedData(bool can_download);

	bool VerifyChecksum(wxString const& file, int64_t size, wxString const& checksum);

	std::wstring GetTempFile() const;
	wxString GetFilename(wxString const& url) const;
	wxString GetLocalFile(build const& b, bool allow_existing);

	void SetState( UpdaterState s );

	virtual void OnEngineEvent(CFileZillaEngine* engine);
	void DoOnEngineEvent(CFileZillaEngine* engine);

	DECLARE_EVENT_TABLE()
	void OnTimer(wxTimerEvent& ev);

	UpdaterState state_;
	wxString local_file_;
	CFileZillaEngine* engine_;
	bool m_use_internal_rootcert{};

	wxString raw_version_information_;

	version_information version_information_;

	std::list<CUpdateHandler*> handlers_;

	wxString log_;

	wxTimer update_timer_;

	std::deque<std::unique_ptr<CCommand>> pending_commands_;
};

#endif //FZ_MANUALUPDATECHECK

#endif
