#include <filezilla.h>

#if FZ_MANUALUPDATECHECK

#include "buildinfo.h"
#include "updater.h"
#include "Options.h"
#include "file_utils.h"
#include <wx/tokenzr.h>
#include <string>

#ifdef __WXMSW__
#include <wx/msw/registry.h>
#endif

#include <libfilezilla/file.hpp>
#include <libfilezilla/local_filesys.hpp>

#include <string>

#include <nettle/sha2.h>

BEGIN_EVENT_TABLE(CUpdater, wxEvtHandler)
EVT_TIMER(wxID_ANY, CUpdater::OnTimer)
END_EVENT_TABLE()

// BASE-64 encoded DER without the BEGIN/END CERTIFICATE
static char s_update_cert[] = "\
MIIFsTCCA5ugAwIBAgIESnXLbzALBgkqhkiG9w0BAQ0wSTELMAkGA1UEBhMCREUx\n\
GjAYBgNVBAoTEUZpbGVaaWxsYSBQcm9qZWN0MR4wHAYDVQQDExVmaWxlemlsbGEt\n\
cHJvamVjdC5vcmcwHhcNMDkwODAyMTcyMjU2WhcNMzEwNjI4MTcyMjU4WjBJMQsw\n\
CQYDVQQGEwJERTEaMBgGA1UEChMRRmlsZVppbGxhIFByb2plY3QxHjAcBgNVBAMT\n\
FWZpbGV6aWxsYS1wcm9qZWN0Lm9yZzCCAh8wCwYJKoZIhvcNAQEBA4ICDgAwggIJ\n\
AoICAJqWXy7YzVP5pOk8VB9bd/ROC9SVbAxJiFHh0I0/JmyW+jSfzFCYWr1DKGVv\n\
Oui+qiUsaSgjWTh/UusnVu4Q4Lb00k7INRF6MFcGFkGNmOZPk4Qt0uuWMtsxiFek\n\
9QMPWSYs+bxk+M0u0rNOdAblsIzeV16yhfUQDtrJxPWbRpuLgp9/4/oNbixet7YM\n\
pvwlns2o1KXcsNcBcXraux5QmnD4oJVYbTY2qxdMVyreA7dxd40c55F6FvA+L36L\n\
Nv54VwRFSqY12KBG4I9Up+c9OQ9HMN0zm0FhYtYeKWzdMIRk06EKAxO7MUIcip3q\n\
7v9eROPnKM8Zh4dzkWnCleirW8EKFEm+4+A8pDqirMooiQqkkMesaJDV361UCoVo\n\
fRhqfK+Prx0BaJK/5ZHN4tmgU5Tmq+z2m7aIKwOImj6VF3somVvmh0G/othnU2MH\n\
GB7qFrIUMZc5VhrAwmmSA2Z/w4+0ToiR+IrdGmDKz3cVany3EZAzWRJUARaId9FH\n\
v/ymA1xcFAKmfxsjGNlNpXd7b8UElS8+ccKL9m207k++IIjc0jUPgrM70rU3cv5M\n\
Kevp971eHLhpWa9vrjbz/urDzBg3Dm8XEN09qwmABfIEnhm6f7oz2bYXjz73ImYj\n\
rZsogz+Jsx3NWhHFUD42iA4ZnxHIEgchD/TAihpbdrEhgmdvAgMBAAGjgacwgaQw\n\
EgYDVR0TAQH/BAgwBgEB/wIBAjAmBgNVHREEHzAdgRthZG1pbkBmaWxlemlsbGEt\n\
cHJvamVjdC5vcmcwDwYDVR0PAQH/BAUDAwcGADAdBgNVHQ4EFgQUd4w2verFjXAn\n\
CrNLor39nFtemNswNgYDVR0fBC8wLTAroCmgJ4YlaHR0cHM6Ly9jcmwuZmlsZXpp\n\
bGxhLXByb2plY3Qub3JnL2NybDALBgkqhkiG9w0BAQ0DggIBAF3fmV/Bs4amV78d\n\
uhe5PkW7yTO6iCfKJVDB22kXPvL0rzZn4SkIZNoac8Xl5vOoRd6k+06i3aJ78w+W\n\
9Z0HK1jUdjW7taYo4bU58nAp3Li+JwjE/lUBNqSKSescPjdZW0KzIIZls91W30yt\n\
tGq85oWAuyVprHPlr2uWLg1q4eUdF6ZAz4cZ0+9divoMuk1HiWxi1Y/1fqPRzUFf\n\
UGK0K36iPPz2ktzT7qJYXRfC5QDoX7tCuoDcO5nccVjDypRKxy45O5Ucm/fywiQW\n\
NQfz/yQAmarQSCfDjNcHD1rdJ0lx9VWP6xi+Z8PGSlR9eDuMaqPVAE1DLHwMMTTZ\n\
93PbfP2nvgbElgEki28LUalyVuzvrKcu/rL1LnCJA4jStgE/xjDofpYwgtG4ZSnE\n\
KgNy48eStvNZbGhwn2YvrxyKmw58WSQG9ArOCHoLcWnpedSZuTrPTLfgNUx7DNbo\n\
qJU36tgxiO0XLRRSetl7jkSIO6U1okVH0/tvstrXEWp4XwdlmoZf92VVBrkg3San\n\
fA5hBaI2gpQwtpyOJzwLzsd43n4b1YcPiyzhifJGcqRCBZA1uArNsH5iG6z/qHXp\n\
KjuMxZu8aM8W2gp8Yg8QZfh5St/nut6hnXb5A8Qr+Ixp97t34t264TBRQD6MuZc3\n\
PqQuF7sJR6POArUVYkRD/2LIWsB7\n\
";

void version_information::update_available()
{
	if( !nightly_.url_.empty() && COptions::Get()->GetOptionVal(OPTION_UPDATECHECK_CHECKBETA) == 2 ) {
		available_ = nightly_;
	}
	else if( !beta_.version_.empty() && COptions::Get()->GetOptionVal(OPTION_UPDATECHECK_CHECKBETA) != 0 ) {
		available_ = beta_;
	}
	else if( !stable_.version_.empty() ) {
		available_ = stable_;
	}
	else {
		available_ = build();
	}
}

static CUpdater* instance = 0;

CUpdater::CUpdater(CUpdateHandler& parent, CFileZillaEngineContext& engine_context)
	: state_(UpdaterState::idle)
	, engine_(new CFileZillaEngine(engine_context, *this))
{
	AddHandler(parent);
}

void CUpdater::Init()
{
	if (state_ == UpdaterState::checking || state_ == UpdaterState::newversion_downloading) {
		return;
	}

	raw_version_information_ = COptions::Get()->GetOption( OPTION_UPDATECHECK_NEWVERSION );

	UpdaterState s = ProcessFinishedData(FZ_AUTOUPDATECHECK);

	SetState(s);

	AutoRunIfNeeded();

	update_timer_.SetOwner(this);
	update_timer_.Start(1000 * 3600);

	if( !instance ) {
		instance = this;
	}
}

CUpdater::~CUpdater()
{
	if( instance == this ) {
		instance  =0;
	}
	delete engine_;
}

CUpdater* CUpdater::GetInstance()
{
	return instance;
}

void CUpdater::AutoRunIfNeeded()
{
#if FZ_AUTOUPDATECHECK
	if( state_ == UpdaterState::failed || state_ == UpdaterState::idle ) {
		if( !COptions::Get()->GetOptionVal(OPTION_DEFAULT_DISABLEUPDATECHECK) && COptions::Get()->GetOptionVal(OPTION_UPDATECHECK) != 0 && LongTimeSinceLastCheck() ) {
			Run();
		}
	}
#endif
}

void CUpdater::RunIfNeeded()
{
	build const b = AvailableBuild();
	if( state_ == UpdaterState::idle || state_ == UpdaterState::failed ||
		LongTimeSinceLastCheck() || (state_ == UpdaterState::newversion && !b.url_.empty()) ||
		(state_ == UpdaterState::newversion_ready && !VerifyChecksum( DownloadedFile(), b.size_, b.hash_ ) ) )
	{
		Run();
	}
}

bool CUpdater::LongTimeSinceLastCheck() const
{
	wxString const lastCheckStr = COptions::Get()->GetOption(OPTION_UPDATECHECK_LASTDATE);
	if (lastCheckStr.empty())
		return true;

	fz::datetime lastCheck(lastCheckStr.ToStdWstring(), fz::datetime::utc);
	if (lastCheck.empty())
		return true;

	auto const span = fz::datetime::now() - lastCheck;

	if (span.get_seconds() < 0)
		// Last check in future
		return true;

	int days = 1;
	if (!CBuildInfo::IsUnstable())
		days = COptions::Get()->GetOptionVal(OPTION_UPDATECHECK_INTERVAL);
	return span.get_days() >= days;
}

wxString CUpdater::GetUrl()
{
	wxString host = CBuildInfo::GetHostname();
	if (host.empty())
		host = _T("unknown");

	wxString version = CBuildInfo::GetVersion();
	version.Replace(_T(" "), _T("%20"));

	wxString url = wxString::Format(_T("https://update.filezilla-project.org/update.php?platform=%s&version=%s"), host, version);
#if defined(__WXMSW__) || defined(__WXMAC__)
	// Makes not much sense to submit OS version on Linux, *BSD and the likes, too many flavours.
	wxString osVersion = wxString::Format(_T("&osversion=%d.%d"), wxPlatformInfo::Get().GetOSMajorVersion(), wxPlatformInfo::Get().GetOSMinorVersion());
	url += osVersion;
#endif

#ifdef __WXMSW__
	if (wxIsPlatform64Bit())
		url += _T("&osarch=64");
	else
		url += _T("&osarch=32");

	// Add information about package
	{
		wxLogNull log;

		// Installer always writes to 32bit section
		auto key = std::make_unique<wxRegKey>(_T("HKEY_CURRENT_USER\\Software\\FileZilla Client"), wxRegKey::WOW64ViewMode_32);
		if (!key->Exists()) {
			// wxRegKey is sad, it doesn't even have a copy constructor.
			key = std::make_unique<wxRegKey>(_T("HKEY_LOCAL_MACHINE\\Software\\FileZilla Client"), wxRegKey::WOW64ViewMode_32);
		}

		long updated{};
		if (key->GetValueType(_T("Updated")) == wxRegKey::Type_Dword && key->QueryValue(_T("Updated"), &updated)) {
			url += wxString::Format(_T("&updated=%d"), updated);
		}

		long package{};
		if (key->GetValueType(_T("Package")) == wxRegKey::Type_Dword && key->QueryValue(_T("Package"), &package)) {
			url += wxString::Format(_T("&package=%d"), package);
		}
	}
#endif

	wxString const cpuCaps = CBuildInfo::GetCPUCaps(',');
	if (!cpuCaps.empty()) {
		url += _T("&cpuid=") + cpuCaps;
	}

	wxString const lastVersion = COptions::Get()->GetOption(OPTION_UPDATECHECK_LASTVERSION);
	if (lastVersion != CBuildInfo::GetVersion()) {
		url += _T("&initial=1");
	}
	else {
		url += _T("&initial=0");
	}

	return url;
}

bool CUpdater::Run()
{
	if( state_ != UpdaterState::idle && state_ != UpdaterState::failed &&
		state_ != UpdaterState::newversion && state_ != UpdaterState::newversion_ready )
	{
		return false;
	}

	auto  const t = fz::datetime::now();
	COptions::Get()->SetOption(OPTION_UPDATECHECK_LASTDATE, t.format(_T("%Y-%m-%d %H:%M:%S"), fz::datetime::utc));

	local_file_.clear();
	log_ = wxString::Format(_("Started update check on %s\n"), t.format(_T("%Y-%m-%d %H:%M:%S"), fz::datetime::local));

	wxString build = CBuildInfo::GetBuildType();
	if( build.empty() ) {
		build = _("custom");
	}
	log_ += wxString::Format(_("Own build type: %s\n"), build);

	SetState(UpdaterState::checking);

	m_use_internal_rootcert = true;
	int res = Download(GetUrl(), std::wstring());

	if (res != FZ_REPLY_WOULDBLOCK) {
		SetState(UpdaterState::failed);
	}
	raw_version_information_.clear();

	return state_ == UpdaterState::checking;
}

int CUpdater::Download(wxString const& url, std::wstring const& local_file)
{
	wxASSERT(pending_commands_.empty());
	pending_commands_.clear();
	pending_commands_.emplace_back(new CDisconnectCommand);
	if (!CreateConnectCommand(url) || !CreateTransferCommand(url, local_file)) {
		return FZ_REPLY_ERROR;
	}

	return ContinueDownload();
}

int CUpdater::ContinueDownload()
{
	if (pending_commands_.empty()) {
		return FZ_REPLY_OK;
	}

	int res = engine_->Execute(*pending_commands_.front());
	if (res == FZ_REPLY_OK) {
		pending_commands_.pop_front();
		return ContinueDownload();
	}

	return res;
}

bool CUpdater::CreateConnectCommand(wxString const& url)
{
	CServer s;
	CServerPath path;
	std::wstring error;
	if (!s.ParseUrl(url.ToStdWstring(), 0, std::wstring(), std::wstring(), error, path) || (s.GetProtocol() != HTTP && s.GetProtocol() != HTTPS)) {
		return false;
	}

	pending_commands_.emplace_back(new CConnectCommand(s));
	return true;
}

bool CUpdater::CreateTransferCommand(wxString const& url, std::wstring const& local_file)
{
	CFileTransferCommand::t_transferSettings transferSettings;

	CServer s;
	CServerPath path;
	std::wstring error;
	if (!s.ParseUrl(url.ToStdWstring(), 0, std::wstring(), std::wstring(), error, path) || (s.GetProtocol() != HTTP && s.GetProtocol() != HTTPS)) {
		return false;
	}
	std::wstring file = path.GetLastSegment();
	path = path.GetParent();

	pending_commands_.emplace_back(new CFileTransferCommand(local_file, path, file, true, transferSettings));
	return true;
}

void CUpdater::OnEngineEvent(CFileZillaEngine* engine)
{
	CallAfter(&CUpdater::DoOnEngineEvent, engine);
}

void CUpdater::DoOnEngineEvent(CFileZillaEngine* engine)
{
	if (!engine_ || engine_ != engine)
		return;

	std::unique_ptr<CNotification> notification;
	while( (notification = engine_->GetNextNotification()) ) {
		ProcessNotification(std::move(notification));
	}
}

void CUpdater::ProcessNotification(std::unique_ptr<CNotification> && notification)
{
	if (state_ != UpdaterState::checking && state_ != UpdaterState::newversion_downloading) {
		return;
	}

	switch (notification->GetID())
	{
	case nId_asyncrequest:
		{
			auto pData = unique_static_cast<CAsyncRequestNotification>(std::move(notification));
			if (pData->GetRequestID() == reqId_fileexists) {
				static_cast<CFileExistsNotification *>(pData.get())->overwriteAction = CFileExistsNotification::resume;
			}
			else if (pData->GetRequestID() == reqId_certificate) {
				auto & certNotification = static_cast<CCertificateNotification &>(*pData.get());
				if (m_use_internal_rootcert) {
					auto certs = certNotification.GetCertificates();
					if (certs.size() > 1) {
						auto const& ca = certs.back();
						std::vector<uint8_t> ca_data = ca.GetRawData();

						std::string updater_root = fz::base64_decode(s_update_cert);
						if (ca_data.size() == updater_root.size() && !memcmp(&ca_data[0], updater_root.c_str(), ca_data.size()) ) {
							certNotification.m_trusted = true;
						}
					}
				}
				else {
					certNotification.m_trusted = true;
				}
			}
			engine_->SetAsyncRequestReply(std::move(pData));
		}
		break;
	case nId_data:
		ProcessData(static_cast<CDataNotification&>(*notification.get()));
		break;
	case nId_operation:
		ProcessOperation(static_cast<COperationNotification const&>(*notification.get()));
		break;
	case nId_logmsg:
		{
			auto const& msg = static_cast<CLogmsgNotification const&>(*notification.get());
			log_ += msg.msg + _T("\n");
		}
		break;
	default:
		break;
	}
}

UpdaterState CUpdater::ProcessFinishedData(bool can_download)
{
	UpdaterState s = UpdaterState::failed;

	ParseData();

	if (version_information_.available_.version_.empty()) {
		s = UpdaterState::idle;
	}
	else if (!version_information_.available_.url_.empty()) {

		std::wstring const temp = GetTempFile();
		wxString const local_file = GetLocalFile(version_information_.available_, true);
		if (!local_file.empty() && fz::local_filesys::get_file_type(fz::to_native(local_file)) != fz::local_filesys::unknown) {
			local_file_ = local_file;
			log_ += wxString::Format(_("Local file is %s\n"), local_file);
			s = UpdaterState::newversion_ready;
		}
		else {
			// We got a checksum over a secure channel already.
			m_use_internal_rootcert = false;

			if (temp.empty() || local_file.empty()) {
				s = UpdaterState::newversion;
			}
			else {
				s = UpdaterState::newversion_downloading;
				auto size = fz::local_filesys::get_size(fz::to_native(temp));
				if (size >= 0 && size >= version_information_.available_.size_) {
					s = ProcessFinishedDownload();
				}
				else if (!can_download || Download(version_information_.available_.url_, temp) != FZ_REPLY_WOULDBLOCK ) {
					s = UpdaterState::newversion;
				}
			}
		}
	}
	else {
		s = UpdaterState::newversion;
	}

	return s;
}

void CUpdater::ProcessOperation(COperationNotification const& operation)
{
	if (state_ != UpdaterState::checking && state_ != UpdaterState::newversion_downloading) {
		return;
	}

	if (pending_commands_.empty()) {
		SetState(UpdaterState::failed);
		return;
	}


	UpdaterState s = UpdaterState::failed;

	int res = operation.nReplyCode;
	if (res == FZ_REPLY_OK || (operation.commandId == Command::disconnect && res & FZ_REPLY_DISCONNECTED)) {
		pending_commands_.pop_front();
		res = ContinueDownload();
		if (res == FZ_REPLY_WOULDBLOCK) {
			return;
		}
	}

	if (res != FZ_REPLY_OK) {
		if (state_ != UpdaterState::checking) {
			s = UpdaterState::newversion;
		}
	}
	else if (state_ == UpdaterState::checking) {
		COptions::Get()->SetOption(OPTION_UPDATECHECK_LASTVERSION, CBuildInfo::GetVersion());
		s = ProcessFinishedData(true);
	}
	else {
		s = ProcessFinishedDownload();
	}
	SetState(s);
}

UpdaterState CUpdater::ProcessFinishedDownload()
{
	UpdaterState s = UpdaterState::newversion;

	std::wstring const temp = GetTempFile();
	if (temp.empty()) {
		s = UpdaterState::newversion;
	}
	else if (!VerifyChecksum(temp, version_information_.available_.size_, version_information_.available_.hash_)) {
		wxLogNull log;
		wxRemoveFile(temp);
		s = UpdaterState::newversion;
	}
	else {
		s = UpdaterState::newversion_ready;

		wxString local_file = GetLocalFile(version_information_.available_, false);

		wxLogNull log;
		if (local_file.empty() || !wxRenameFile( temp, local_file, false ) ) {
			s = UpdaterState::newversion;
			wxRemoveFile( temp );
			log_ += wxString::Format(_("Could not create local file %s\n"), local_file);
		}
		else {
			local_file_ = local_file;
			log_ += wxString::Format(_("Local file is %s\n"), local_file);
		}
	}
	return s;
}

wxString CUpdater::GetLocalFile(build const& b, bool allow_existing)
{
	wxString const fn = GetFilename( b.url_ );
	wxString const dl = GetDownloadDir().GetPath();

	int i = 1;
	wxString f = dl + fn;

	while (fz::local_filesys::get_file_type(fz::to_native(f)) != fz::local_filesys::unknown && (!allow_existing || !VerifyChecksum(f, b.size_, b.hash_))) {
		if (++i > 99) {
			return wxString();
		}
		wxString ext;
		int pos;
		if (!fn.Right(8).CmpNoCase(_T(".tar.bz2"))) {
			pos = fn.size() - 8;
		}
		else {
			pos = fn.Find('.', true);
		}

		if (pos == -1) {
			f = dl + fn + wxString::Format(_T(" (%d)"), i);
		}
		else {
			f = dl + fn.Left(pos) + wxString::Format(_T(" (%d)"), i) + fn.Mid(pos);
		}
	}

	return f;
}

void CUpdater::ProcessData(CDataNotification& dataNotification)
{
	if (state_ != UpdaterState::checking) {
		return;
	}

	int len;
	char* data = dataNotification.Detach(len);

	if (COptions::Get()->GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 4) {
		log_ += wxString::Format(_T("ProcessData %d\n"), len);
	}

	if (raw_version_information_.size() + len > 0x40000) {
		log_ += _("Received version information is too large");
		engine_->Cancel();
		SetState(UpdaterState::failed);
	}
	else {
		for (int i = 0; i < len; ++i) {
			if (data[i] < 10 || (unsigned char)data[i] > 127) {
				log_ += _("Received invalid character in version information");
				SetState(UpdaterState::failed);
				engine_->Cancel();
				break;
			}
		}
	}

	if (state_ == UpdaterState::checking) {
		raw_version_information_ += wxString(data, wxConvUTF8, len);
	}
	delete [] data;
}

void CUpdater::ParseData()
{
	int64_t const ownVersionNumber = CBuildInfo::ConvertToVersionNumber(CBuildInfo::GetVersion().c_str());
	version_information_ = version_information();

	wxString raw_version_information = raw_version_information_;

	log_ += wxString::Format(_("Parsing %d bytes of version information.\n"), static_cast<int>(raw_version_information.size()));

	while( !raw_version_information.empty() ) {
		wxString line;
		int pos = raw_version_information.Find('\n');
		if (pos != -1) {
			line = raw_version_information.Left(pos);
			raw_version_information = raw_version_information.Mid(pos + 1);
		}
		else {
			line = raw_version_information;
			raw_version_information.clear();
		}

		wxStringTokenizer tokens(line, _T(" \t\n"),  wxTOKEN_STRTOK);
		if (!tokens.CountTokens()) {
			// After empty line, changelog follows
			version_information_.changelog_ = raw_version_information;
			version_information_.changelog_.Trim(true);
			version_information_.changelog_.Trim(false);

			if (COptions::Get()->GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 4) {
				log_ += wxString::Format(_T("Changelog: %s\n"), version_information_.changelog_);
			}
			break;
		}

		wxString const type = tokens.GetNextToken();
		if (type == _T("resources")) {
			if (tokens.HasMoreTokens()) {
				if (UpdatableBuild()) {
					version_information_.resources_ = tokens.GetNextToken();
				}
			}
			continue;
		}

		if (tokens.CountTokens() != 1 && tokens.CountTokens() != 5) {
			if (COptions::Get()->GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 4) {
				log_ += wxString::Format(_T("Skipping line with %d tokens\n"), static_cast<int>(tokens.CountTokens() + 1));
			}
			continue;
		}

		wxString versionOrDate = tokens.GetNextToken();

		if (type == _T("nightly")) {
			fz::datetime nightlyDate(versionOrDate.ToStdWstring(), fz::datetime::utc);
			if (nightlyDate.empty()) {
				if (COptions::Get()->GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 4) {
					log_ += _T("Could not parse nightly date\n");
				}
				continue;
			}

			fz::datetime buildDate = CBuildInfo::GetBuildDate();
			if (buildDate.empty() || nightlyDate.empty() || nightlyDate <= buildDate) {
				if( COptions::Get()->GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 4 ) {
					log_ += _T("Nightly isn't newer\n");
				}
				continue;
			}
		}
		else {
			int64_t v = CBuildInfo::ConvertToVersionNumber(versionOrDate.c_str());
			if (v <= ownVersionNumber)
				continue;
		}

		build* b = 0;
		if( type == _T("nightly") && UpdatableBuild() ) {
			b = &version_information_.nightly_;
		}
		else if( type == _T("release") ) {
			b = &version_information_.stable_;
		}
		else if( type == _T("beta") ) {
			b = &version_information_.beta_;
		}

		if( b ) {
			b->version_ = versionOrDate;

			if( UpdatableBuild() && tokens.CountTokens() == 4 ) {
				wxString const url = tokens.GetNextToken();
				wxString const sizestr = tokens.GetNextToken();
				wxString const hash_algo = tokens.GetNextToken();
				wxString const hash = tokens.GetNextToken();

				if( GetFilename(url).empty() ) {
					if( COptions::Get()->GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 4 ) {
						log_ += wxString::Format(_T("Could not extract filename from URL: %s\n"), url);
					}
					continue;
				}

				if( hash_algo.CmpNoCase(_T("sha512")) ) {
					continue;
				}

				unsigned long long l = 0;
				if( !sizestr.ToULongLong(&l) ) {
					if( COptions::Get()->GetOptionVal(OPTION_LOGGING_DEBUGLEVEL) == 4 ) {
						log_ += wxString::Format(_T("Could not parse size: %s"), sizestr);
					}
					continue;
				}

				b->url_ = url;
				b->size_ = l;
				b->hash_ = hash;

				// @translator: Two examples: Found new nightly 2014-04-03\n, Found new release 3.9.0.1\n
				log_ += wxString::Format(_("Found new %s %s\n"), type, b->version_);
			}
		}
	}

	version_information_.update_available();

	COptions::Get()->SetOption(OPTION_UPDATECHECK_NEWVERSION, raw_version_information_.ToStdWstring());
}

void CUpdater::OnTimer(wxTimerEvent&)
{
	AutoRunIfNeeded();
}

bool CUpdater::VerifyChecksum(wxString const& file, int64_t size, wxString const& checksum)
{
	if (file.empty() || checksum.empty()) {
		return false;
	}

	auto filesize = fz::local_filesys::get_size(fz::to_native(file));
	if (filesize < 0 || filesize != size) {
		return false;
	}

	sha512_ctx state;
	sha512_init(&state);

	{
		fz::file f(fz::to_native(file), fz::file::reading);
		if (!f.opened()) {
			return false;
		}
		unsigned char buffer[65536];
		int64_t read;
		while ((read = f.read(buffer, sizeof(buffer))) > 0) {
			sha512_update(&state, static_cast<size_t>(read), buffer);
		}
		if (read < 0) {
			return false;
		}
	}

	unsigned char raw_digest[64];
	sha512_digest(&state, 64, raw_digest);

	wxString digest;
	for (unsigned int i = 0; i < sizeof(raw_digest); ++i) {
		unsigned char l = raw_digest[i] >> 4;
		unsigned char r = raw_digest[i] & 0x0F;

		if (l > 9)
			digest += 'a' + l - 10;
		else
			digest += '0' + l;

		if (r > 9)
			digest += 'a' + r - 10;
		else
			digest += '0' + r;
	}

	if (checksum.CmpNoCase(digest)) {
		log_ += wxString::Format(_("Checksum mismatch on file %s\n"), file);
		return false;
	}

	log_ += wxString::Format(_("Checksum match on file %s\n"), file);
	return true;
}

std::wstring CUpdater::GetTempFile() const
{
	wxASSERT( !version_information_.available_.hash_.empty() );
	wxString ret = wxFileName::GetTempDir();
	if (!ret.empty()) {
		if (ret.Last() != wxFileName::GetPathSeparator()) {
			ret += wxFileName::GetPathSeparator();
		}

		ret += _T("fzupdate_") + version_information_.available_.hash_.Left(16) + _T(".tmp");
	}

	return ret.ToStdWstring();
}

wxString CUpdater::GetFilename( wxString const& url) const
{
	wxString ret;
	int pos = url.Find('/', true);
	if (pos != -1) {
		ret = url.Mid(pos + 1);
	}
	size_t p = ret.find_first_of(_T("?#"));
	if( p != std::string::npos ) {
		ret = ret.substr(0, p);
	}
#ifdef __WXMSW__
	ret.Replace(_T(":"), _T("_"));
#endif

	return ret;
}

void CUpdater::SetState( UpdaterState s )
{
	if( s != state_ ) {
		state_ = s;

		if (s != UpdaterState::checking && s != UpdaterState::newversion_downloading) {
			pending_commands_.clear();
		}
		build b = version_information_.available_;
		for (auto const& handler : handlers_ ) {
			if( handler ) {
				handler->UpdaterStateChanged( s, b );
			}
		}
	}
}

wxString CUpdater::DownloadedFile() const
{
	wxString ret;
	if( state_ == UpdaterState::newversion_ready ) {
		ret = local_file_;
	}
	return ret;
}

void CUpdater::AddHandler( CUpdateHandler& handler )
{
	for( auto const& h : handlers_ ) {
		if (h == &handler) {
			return;
		}
	}
	for( auto& h : handlers_ ) {
		if( !h ) {
			h = &handler;
			return;
		}
	}
	handlers_.push_back(&handler);
}

void CUpdater::RemoveHandler( CUpdateHandler& handler )
{
	for (auto& h : handlers_) {
		if (h == &handler) {
			// Set to 0 instead of removing from list to avoid issues with reentrancy.
			h = 0;
			return;
		}
	}
}

int64_t CUpdater::BytesDownloaded() const
{
	int64_t ret{-1};
	if (state_ == UpdaterState::newversion_ready) {
		if (!local_file_.empty()) {
			ret = fz::local_filesys::get_size(fz::to_native(local_file_));
		}
	}
	else if (state_ == UpdaterState::newversion_downloading) {
		std::wstring const temp = GetTempFile();
		if (!temp.empty()) {
			ret = fz::local_filesys::get_size(fz::to_native(temp));
		}
	}
	return ret;
}

bool CUpdater::UpdatableBuild() const
{
	return CBuildInfo::GetBuildType() == _T("nightly") || CBuildInfo::GetBuildType() == _T("official");
}

#endif
