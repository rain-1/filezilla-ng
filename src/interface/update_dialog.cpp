#include <filezilla.h>
#include "buildinfo.h"
#include "filezillaapp.h"
#include "update_dialog.h"
#include "themeprovider.h"

BEGIN_EVENT_TABLE(CUpdateDialog, wxDialogEx)
EVT_BUTTON(XRCID("ID_INSTALL"), CUpdateDialog::OnInstall)
EVT_TIMER(wxID_ANY, CUpdateDialog::OnTimer)
END_EVENT_TABLE()

namespace pagenames {
enum type {
	checking,
	failed,
	newversion,
	latest
};
}

CUpdateDialog::CUpdateDialog(wxWindow* parent, CUpdater& updater)
	: parent_(parent)
	, updater_(updater)
{
	timer_.SetOwner(this);
}

CUpdateDialog::~CUpdateDialog()
{
}

int CUpdateDialog::ShowModal()
{
	wxString version(PACKAGE_VERSION, wxConvLocal);
	if (version[0] < '0' || version[0] > '9')
	{
		wxMessageBox(_("Executable contains no version info, cannot check for updates."), _("Check for updates failed"), wxICON_ERROR, parent_);
		return wxID_CANCEL;
	}

	if (!Load(parent_, _T("ID_UPDATE_DIALOG")))
		return wxID_CANCEL;

	LoadPanel(_T("ID_CHECKING_PANEL"));
	LoadPanel(_T("ID_FAILURE_PANEL"));
	LoadPanel(_T("ID_NEWVERSION_PANEL"));
	LoadPanel(_T("ID_LATEST_PANEL"));
	if( panels_.size() != 4 ) {
		return wxID_CANCEL;
	}

	wxAnimation a = CThemeProvider::Get()->CreateAnimation(_T("ART_THROBBER"), wxSize(16,16));
	XRCCTRL(*this, "ID_WAIT_CHECK", wxAnimationCtrl)->SetAnimation(a);
	XRCCTRL(*this, "ID_WAIT_CHECK", wxAnimationCtrl)->Play();
	XRCCTRL(*this, "ID_WAIT_DOWNLOAD", wxAnimationCtrl)->SetAnimation(a);
	XRCCTRL(*this, "ID_WAIT_DOWNLOAD", wxAnimationCtrl)->Play();
	
	Wrap();

	UpdaterState s = updater_.GetState();
	UpdaterStateChanged( s, updater_.AvailableBuild() );

	updater_.AddHandler(*this);

	if( s == idle || s == failed ) {
		updater_.Run();
	}
	int ret = wxDialogEx::ShowModal();
	updater_.RemoveHandler(*this);
	
	return ret;
}

void CUpdateDialog::Wrap()
{
	wxPanel* parentPanel = XRCCTRL(*this, "ID_CONTENT", wxPanel);
	wxSize canvas;
	canvas.x = GetSize().x - parentPanel->GetSize().x;
	canvas.y = GetSize().y - parentPanel->GetSize().y;

	// Wrap pages nicely
	std::vector<wxWindow*> pages;
	for (unsigned int i = 0; i < panels_.size(); i++) {
		pages.push_back(panels_[i]);
	}
	wxGetApp().GetWrapEngine()->WrapRecursive(pages, 1.33, "Update", canvas);

	// Keep track of maximum page size
	wxSize size = GetSizer()->GetMinSize();
	for (std::vector<wxPanel*>::iterator iter = panels_.begin(); iter != panels_.end(); ++iter)
		size.IncTo((*iter)->GetSizer()->GetMinSize());

#ifdef __WXGTK__
	size.x += 1;
#endif
	parentPanel->SetInitialSize(size);

	// Adjust pages sizes according to maximum size
	for (std::vector<wxPanel*>::iterator iter = panels_.begin(); iter != panels_.end(); ++iter) {
		(*iter)->GetSizer()->SetMinSize(size);
		(*iter)->GetSizer()->Fit(*iter);
		(*iter)->GetSizer()->SetSizeHints(*iter);
	}

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

#ifdef __WXGTK__
	// Pre-show dialog under GTK, else panels won't get initialized properly
	Show();
#endif

	for (std::vector<wxPanel*>::iterator iter = panels_.begin(); iter != panels_.end(); ++iter) {
		(*iter)->Hide();
	}
	panels_[0]->Show();
}

void CUpdateDialog::LoadPanel(wxString const& name)
{
	wxPanel* p = new wxPanel();
	if (!wxXmlResource::Get()->LoadPanel(p, XRCCTRL(*this, "ID_CONTENT", wxPanel), name)) {
		delete p;
		return;
	}

	panels_.push_back(p);
}


void CUpdateDialog::UpdaterStateChanged( UpdaterState s, build const& v )
{
	timer_.Stop();
	for (std::vector<wxPanel*>::iterator iter = panels_.begin(); iter != panels_.end(); ++iter) {
		(*iter)->Hide();
	}
	if( s == idle ) {
		panels_[pagenames::latest]->Show();
	}
	else if( s == failed ) {
		panels_[pagenames::failed]->Show();
	}
	else if( s == checking ) {
		panels_[pagenames::checking]->Show();
	}
	else if( s == newversion || s == newversion_ready || s == newversion_downloading ) {
		XRCCTRL(*this, "ID_VERSION", wxStaticText)->SetLabel(v.version_);
		wxString news = updater_.GetChangelog();
		XRCCTRL(*this, "ID_NEWS_LABEL", wxStaticText)->Show(!news.empty());
		XRCCTRL(*this, "ID_NEWS", wxTextCtrl)->Show(!news.empty());
		if( news != XRCCTRL(*this, "ID_NEWS", wxTextCtrl)->GetValue() ) {
			XRCCTRL(*this, "ID_NEWS", wxTextCtrl)->ChangeValue(news);
		}
		bool downloading = s == newversion_downloading;
		XRCCTRL(*this, "ID_DOWNLOAD_LABEL", wxStaticText)->Show(downloading);
		XRCCTRL(*this, "ID_WAIT_DOWNLOAD", wxAnimationCtrl)->Show(downloading);
		XRCCTRL(*this, "ID_DOWNLOAD_PROGRESS", wxStaticText)->Show(downloading);
		if( downloading ) {
			timer_.Start(500);
			OnTimer(wxTimerEvent());
		}

		bool ready = s == newversion_ready;
		XRCCTRL(*this, "ID_DOWNLOADED", wxStaticText)->Show(ready);
		XRCCTRL(*this, "ID_INSTALL", wxButton)->Show(ready);

		panels_[pagenames::newversion]->Show();
		panels_[pagenames::newversion]->Layout();
	}
}

void CUpdateDialog::OnInstall(wxCommandEvent& ev)
{
	wxString f = updater_.DownloadedFile();
	if( f.empty() ) {
		return;
	}
#ifdef __WXMSW__
	wxExecute(_T("\"") + f +  _T("\" /update"));
	parent_->Close();
#endif
}

void CUpdateDialog::OnTimer(wxTimerEvent& ev)
{
	wxULongLong size = updater_.AvailableBuild().size_;
	wxULongLong downloaded = updater_.BytesDownloaded();

	unsigned int percent = 0;
	if( size > 0 ) {
		percent = ((downloaded * 100) / size).GetLo();
	}

	XRCCTRL(*this, "ID_DOWNLOAD_PROGRESS", wxStaticText)->SetLabel(wxString::Format(_T("(%u%% downloaded)"), percent));
}
