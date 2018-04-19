#include <filezilla.h>
#include "context_control.h"
#include "bookmarks_dialog.h"
#include "listingcomparison.h"
#include "Mainfrm.h"
#include "menu_bar.h"
#include "Options.h"
#include "QueueView.h"
#include "sitemanager.h"
#include "state.h"

#if USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif

IMPLEMENT_DYNAMIC_CLASS(CMenuBar, wxMenuBar)

BEGIN_EVENT_TABLE(CMenuBar, wxMenuBar)
EVT_MENU(wxID_ANY, CMenuBar::OnMenuEvent)
END_EVENT_TABLE()

CMenuBar::CMenuBar()
	: m_pMainFrame()
{
}

CMenuBar::~CMenuBar()
{
	for (auto hidden_menu : m_hidden_items) {
		for (auto hidden_item : hidden_menu.second) {
			delete hidden_item.second;
		}
	}

	m_pMainFrame->Disconnect(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(CMenuBar::OnMenuEvent), 0, this);
}

CMenuBar* CMenuBar::Load(CMainFrame* pMainFrame)
{
	CMenuBar* menubar = new CMenuBar();
	menubar->m_pMainFrame = pMainFrame;

	wxMenu* file = new wxMenu;
	menubar->Append(file, _("&File"));

	wxAcceleratorEntry accel;

	accel.FromString(L"CTRL+S");
	file->Append(XRCID("ID_MENU_FILE_SITEMANAGER"), _("&Site Manager..."), _("Opens the Site Manager"))->SetAccel(&accel);
	file->Append(XRCID("ID_MENU_FILE_COPYSITEMANAGER"), _("&Copy current connection to Site Manager..."));
	file->AppendSeparator();
	accel.FromString(L"CTRL+T");
	file->Append(XRCID("ID_MENU_FILE_NEWTAB"), _("New &tab"), _("Opens a new tab"))->SetAccel(&accel);
	accel.FromString(L"CTRL+W");
	file->Append(XRCID("ID_MENU_FILE_CLOSETAB"), _("Cl&ose tab"), _("Closes current tab"))->SetAccel(&accel);
	file->AppendSeparator();
	file->Append(XRCID("ID_EXPORT"), _("&Export..."));
	file->Append(XRCID("ID_IMPORT"), _("&Import..."));
	file->AppendSeparator();
	accel.FromString(L"CTRL+E");
	file->Append(XRCID("ID_MENU_FILE_EDITED"), _("S&how files currently being edited..."))->SetAccel(&accel);
	file->AppendSeparator();
	accel.FromString(L"CTRL+Q");
	file->Append(XRCID("wxID_EXIT"), _("E&xit"), _("Close FileZilla"))->SetAccel(&accel);


	wxMenu* edit = new wxMenu;
	menubar->Append(edit, _("&Edit"));
	edit->Append(XRCID("ID_MENU_EDIT_NETCONFWIZARD"), _("&Network configuration wizard..."));
	edit->Append(XRCID("ID_MENU_EDIT_CLEARPRIVATEDATA"), _("&Clear private data..."));
	edit->AppendSeparator();
	edit->Append(XRCID("wxID_PREFERENCES"), _("&Settings..."), _("Open the settings dialog of FileZilla"));
#ifdef FZ_MAC
	edit->Append(XRCID("ID_MENU_EDIT_SANDBOX_DIRECTORIES", _("&Directory access permissions..."), _("Open the directory access permissions dialog to configure the local directories FileZilla has access to."));
#endif

	wxMenu* view = new wxMenu;
	menubar->Append(view, _("&View"));
	accel.FromString(L"F5");
	view->Append(XRCID("ID_REFRESH"), _("&Refresh"))->SetAccel(&accel);
	view->AppendSeparator();
	accel.FromString(L"Ctrl+I");
	view->Append(XRCID("ID_MENU_VIEW_FILTERS"), _("Directory listing &filters..."))->SetAccel(&accel);

	wxMenu * comparison = new wxMenu;
	view->AppendSubMenu(comparison, _("&Directory comparison"));

	accel.FromString(L"CTRL+O");
	comparison->Append(XRCID("ID_TOOLBAR_COMPARISON"), _("&Enable"), L"", wxITEM_CHECK)->SetAccel(&accel);
	comparison->AppendSeparator();
	comparison->Append(XRCID("ID_COMPARE_SIZE"), _("Compare file&size"), L"", wxITEM_RADIO);
	comparison->Append(XRCID("ID_COMPARE_DATE"), _("Compare &modification time"), L"", wxITEM_RADIO);
	comparison->AppendSeparator();
	comparison->Append(XRCID("ID_COMPARE_HIDEIDENTICAL"), _("&Hide identical files"), L"", wxITEM_CHECK);

	accel.FromString(L"CTRL+Y");
	view->Append(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), _("S&ynchronized browsing"), L"", wxITEM_CHECK)->SetAccel(&accel);
	view->Append(XRCID("ID_MENU_VIEW_FILELISTSTATUSBAR"), _("Filelist status &bars"), L"", wxITEM_CHECK);
	view->AppendSeparator();
	view->Append(XRCID("ID_VIEW_TOOLBAR"), _("T&oolbar"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_QUICKCONNECT"), _("&Quickconnect bar"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_MESSAGELOG"), _("&Message log"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_LOCALTREE"), _("&Local directory tree"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_REMOTETREE"), _("R&emote directory tree"), L"", wxITEM_CHECK);
	view->Append(XRCID("ID_VIEW_QUEUE"), _("&Transfer queue"), L"", wxITEM_CHECK);

	wxMenu * transfer = new wxMenu;
	menubar->Append(transfer, _("&Transfer"));

	accel.FromString(L"CTRL+P");
	transfer->Append(XRCID("ID_MENU_TRANSFER_PROCESSQUEUE"), _("Process &Queue"), L"", wxITEM_CHECK)->SetAccel(&accel);
	transfer->AppendSeparator();
	transfer->Append(XRCID("ID_MENU_TRANSFER_FILEEXISTS"), _("&Default file exists action..."));

	wxMenu * type = new wxMenu;
	transfer->Append(XRCID("ID_MENU_TRANSFER_TYPE"), _("Transfer &type"), type);

	type->Append(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), _("&Auto"), L"", wxITEM_RADIO);
	type->Append(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), _("A&SCII"), L"", wxITEM_RADIO);
	type->Append(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), _("&Binary"), L"", wxITEM_RADIO);
	accel.FromString(L"CTRL+U");

	transfer->Append(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), _("&Preserve timestamps of transferred files"), L"", wxITEM_CHECK)->SetAccel(&accel);

	wxMenu * speed = new wxMenu;
	transfer->AppendSubMenu(speed, _("&Speed limits"));

	speed->Append(XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_ENABLE"), _("&Enable"), L"", wxITEM_CHECK);
	speed->Append(XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_CONFIGURE"), _("&Configure..."));

	transfer->AppendSeparator();
	accel.FromString(L"CTRL+M");
	transfer->Append(XRCID("ID_MENU_TRANSFER_MANUAL"), _("&Manual transfer..."))->SetAccel(&accel);

	wxMenu * server = new wxMenu;
	menubar->Append(server, _("&Server"));
	accel.FromString(L"CTRL+.");
	server->Append(XRCID("ID_CANCEL"), _("C&ancel current operation"))->SetAccel(&accel);
	server->AppendSeparator();
	accel.FromString(L"CTRL+R");
	server->Append(XRCID("ID_MENU_SERVER_RECONNECT"), _("&Reconnect"))->SetAccel(&accel);
	accel.FromString(L"CTRL+D");
	server->Append(XRCID("ID_MENU_SERVER_DISCONNECT"), _("&Disconnect"))->SetAccel(&accel);
	server->AppendSeparator();
	accel.FromString(L"F3");
	server->Append(XRCID("ID_MENU_SERVER_SEARCH"), _("&Search remote files..."), _("Search server for files"))->SetAccel(&accel);
	server->Append(XRCID("ID_MENU_SERVER_CMD"), _("Enter &custom command..."), _("Send custom command to the server otherwise not available"));
	server->Append(XRCID("ID_MENU_SERVER_VIEWHIDDEN"), _("Force showing &hidden files"), L"", wxITEM_CHECK);

	wxMenu * bookmarks = new wxMenu;
	menubar->Append(bookmarks, _("&Bookmarks"));

	accel.FromString(L"CTRL+B");
	bookmarks->Append(XRCID("ID_BOOKMARK_ADD"), _("&Add bookmark..."))->SetAccel(&accel);
	accel.FromString(L"CTRL+SHIFT+B");
	bookmarks->Append(XRCID("ID_BOOKMARK_MANAGE"), _("&Manage bookmarks..."))->SetAccel(&accel);

	wxMenu * help = new wxMenu;
#ifdef FZ_MAC
	menubar->Append(help, _("?"));
#else
	menubar->Append(help, _("&Help"));
#endif

#if FZ_MANUALUPDATECHECK
	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_DISABLEUPDATECHECK) == 0) {
		help->Append(XRCID("ID_CHECKFORUPDATES"), _("Check for &updates..."), _("Check for newer versions of FileZilla"));
		help->AppendSeparator();
	}
#endif
	help->Append(XRCID("ID_MENU_HELP_WELCOME"), _("Show &welcome dialog..."));
	help->Append(XRCID("ID_MENU_HELP_GETTINGHELP"), _("&Getting help..."));
	help->Append(XRCID("ID_MENU_HELP_BUGREPORT"), _("&Report a bug..."));

#ifndef FZ_MAC
	help->Append(XRCID("wxID_ABOUT"), _("&About..."), _("Display about dialog"));
#endif

	if (COptions::Get()->GetOptionVal(OPTION_DEBUG_MENU)) {
		wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_DEBUG"));
		if (pMenu) {
			menubar->Append(pMenu, _("&Debug"));
		}
	}

	menubar->UpdateBookmarkMenu();

	menubar->Check(XRCID("ID_MENU_SERVER_VIEWHIDDEN"), COptions::Get()->GetOptionVal(OPTION_VIEW_HIDDEN_FILES) ? true : false);

	int mode = COptions::Get()->GetOptionVal(OPTION_COMPARISONMODE);
	if (mode != 1) {
		menubar->Check(XRCID("ID_COMPARE_SIZE"), true);
	}
	else {
		menubar->Check(XRCID("ID_COMPARE_DATE"), true);
	}

	menubar->Check(XRCID("ID_COMPARE_HIDEIDENTICAL"), COptions::Get()->GetOptionVal(OPTION_COMPARE_HIDEIDENTICAL) != 0);
	menubar->Check(XRCID("ID_VIEW_QUICKCONNECT"), COptions::Get()->GetOptionVal(OPTION_SHOW_QUICKCONNECT) != 0);
	menubar->Check(XRCID("ID_VIEW_TOOLBAR"), COptions::Get()->GetOptionVal(OPTION_TOOLBAR_HIDDEN) == 0);
	menubar->Check(XRCID("ID_VIEW_MESSAGELOG"), COptions::Get()->GetOptionVal(OPTION_SHOW_MESSAGELOG) != 0);
	menubar->Check(XRCID("ID_VIEW_QUEUE"), COptions::Get()->GetOptionVal(OPTION_SHOW_QUEUE) != 0);
	menubar->Check(XRCID("ID_VIEW_LOCALTREE"), COptions::Get()->GetOptionVal(OPTION_SHOW_TREE_LOCAL) != 0);
	menubar->Check(XRCID("ID_VIEW_REMOTETREE"), COptions::Get()->GetOptionVal(OPTION_SHOW_TREE_REMOTE) != 0);
	menubar->Check(XRCID("ID_MENU_VIEW_FILELISTSTATUSBAR"), COptions::Get()->GetOptionVal(OPTION_FILELIST_STATUSBAR) != 0);
	menubar->Check(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), COptions::Get()->GetOptionVal(OPTION_PRESERVE_TIMESTAMPS) != 0);

	switch (COptions::Get()->GetOptionVal(OPTION_ASCIIBINARY))
	{
	case 1:
		menubar->Check(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), true);
		break;
	case 2:
		menubar->Check(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), true);
		break;
	default:
		menubar->Check(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), true);
		break;
	}

	menubar->UpdateSpeedLimitMenuItem();

	if (COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION) == 2)
		menubar->HideItem(XRCID("ID_VIEW_MESSAGELOG"));

	menubar->UpdateMenubarState();

	pMainFrame->Connect(wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(CMenuBar::OnMenuEvent), 0, menubar);

	CContextManager::Get()->RegisterHandler(menubar, STATECHANGE_REMOTE_IDLE, true);
	CContextManager::Get()->RegisterHandler(menubar, STATECHANGE_SERVER, true);
	CContextManager::Get()->RegisterHandler(menubar, STATECHANGE_SYNC_BROWSE, true);
	CContextManager::Get()->RegisterHandler(menubar, STATECHANGE_COMPARISON, true);

	CContextManager::Get()->RegisterHandler(menubar, STATECHANGE_QUEUEPROCESSING, false);
	CContextManager::Get()->RegisterHandler(menubar, STATECHANGE_CHANGEDCONTEXT, false);

	CContextManager::Get()->RegisterHandler(menubar, STATECHANGE_GLOBALBOOKMARKS, false);

	menubar->RegisterOption(OPTION_ASCIIBINARY);
	menubar->RegisterOption(OPTION_PRESERVE_TIMESTAMPS);

	menubar->RegisterOption(OPTION_TOOLBAR_HIDDEN);
	menubar->RegisterOption(OPTION_SHOW_MESSAGELOG);
	menubar->RegisterOption(OPTION_SHOW_QUEUE);
	menubar->RegisterOption(OPTION_SHOW_TREE_LOCAL);
	menubar->RegisterOption(OPTION_SHOW_TREE_REMOTE);
	menubar->RegisterOption(OPTION_MESSAGELOG_POSITION);
	menubar->RegisterOption(OPTION_COMPARISONMODE);
	menubar->RegisterOption(OPTION_COMPARE_HIDEIDENTICAL);
	menubar->RegisterOption(OPTION_SPEEDLIMIT_ENABLE);
	menubar->RegisterOption(OPTION_SPEEDLIMIT_INBOUND);
	menubar->RegisterOption(OPTION_SPEEDLIMIT_OUTBOUND);

#ifdef FZ_MAC
	wxMenu* editMenu = nullptr;
	wxMenuItem* dirsItem = menubar->FindItem(XRCID("ID_MENU_EDIT_SANDBOX_DIRECTORIES"), &editMenu);
	if (editMenu && dirsItem) {
#if USE_MAC_SANDBOX
		editMenu->Bind(wxEVT_COMMAND_MENU_SELECTED, [pMainFrame](wxCommandEvent&)
			{
				OSXSandboxUserdirsDialog dlg;
				dlg.Run(pMainFrame);
			}, XRCID("ID_MENU_EDIT_SANDBOX_DIRECTORIES"));
#else		
		editMenu->Delete(dirsItem);
#endif
	}
#endif

	return menubar;
}

void CMenuBar::UpdateBookmarkMenu()
{
	wxMenu* pMenu;
	if (!FindItem(XRCID("ID_BOOKMARK_ADD"), &pMenu)) {
		return;
	}

	// Delete old bookmarks
	for (std::map<int, wxString>::const_iterator iter = m_bookmark_menu_id_map_global.begin(); iter != m_bookmark_menu_id_map_global.end(); ++iter) {
		pMenu->Delete(iter->first);
	}
	m_bookmark_menu_id_map_global.clear();

	for (std::map<int, wxString>::const_iterator iter = m_bookmark_menu_id_map_site.begin(); iter != m_bookmark_menu_id_map_site.end(); ++iter) {
		pMenu->Delete(iter->first);
	}
	m_bookmark_menu_id_map_site.clear();

	// Delete the separators
	while (pMenu->GetMenuItemCount() > 2) {
		wxMenuItem* pSeparator = pMenu->FindItemByPosition(2);
		if (pSeparator) {
			pMenu->Delete(pSeparator);
		}
	}

	auto ids = m_bookmark_menu_ids.begin();

	// Insert global bookmarks
	std::vector<wxString> global_bookmarks;
	if (CBookmarksDialog::GetGlobalBookmarks(global_bookmarks) && !global_bookmarks.empty()) {
		pMenu->AppendSeparator();

		for (auto const& bookmark : global_bookmarks) {
			int id;
			if (ids == m_bookmark_menu_ids.end()) {
				id = wxNewId();
				m_bookmark_menu_ids.push_back(id);
			}
			else {
				id = *ids;
				++ids;
			}
			wxString name(bookmark);
			name.Replace(_T("&"), _T("&&"));
			pMenu->Append(id, name);

			m_bookmark_menu_id_map_global[id] = bookmark;
		}
	}

	// Insert site-specific bookmarks
	CContextControl* pContextControl = m_pMainFrame ? m_pMainFrame->GetContextControl() : 0;
	CContextControl::_context_controls* controls = pContextControl ? pContextControl->GetCurrentControls() : 0;
	CState* pState = controls ? controls->pState : 0;
	if (!pState) {
		return;
	}

	Site site = pState->GetSite();
	if (!site.server_) {
		site = pState->GetLastSite();
	}
	if (site.m_bookmarks.empty()) {
		return;
	}

	pMenu->AppendSeparator();

	for (auto const& bookmark : site.m_bookmarks) {
		int id;
		if (ids == m_bookmark_menu_ids.end()) {
			id = wxNewId();
			m_bookmark_menu_ids.push_back(id);
		}
		else {
			id = *ids;
			++ids;
		}
		wxString name(bookmark.m_name);
		name.Replace(_T("&"), _T("&&"));
		pMenu->Append(id, name);

		m_bookmark_menu_id_map_site[id] = bookmark.m_name;
	}
}

void CMenuBar::OnMenuEvent(wxCommandEvent& event)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	std::map<int, wxString>::const_iterator iter = m_bookmark_menu_id_map_site.find(event.GetId());
	if (iter != m_bookmark_menu_id_map_site.end()) {
		// We hit a site-specific bookmark
		CContextControl* pContextControl = m_pMainFrame ? m_pMainFrame->GetContextControl() : 0;
		CContextControl::_context_controls* controls = pContextControl ? pContextControl->GetCurrentControls() : 0;
		CState* pState = controls ? controls->pState : 0;
		if (!pState)
			return;

		Site site = pState->GetSite();
		if (!site.server_) {
			site = pState->GetLastSite();
		}


		for (auto const& bookmark : site.m_bookmarks) {
			if (bookmark.m_name == iter->second) {

				pState->SetSyncBrowse(false);
				if (!bookmark.m_remoteDir.empty() && pState->IsRemoteIdle(true)) {
					ServerWithCredentials const& server = pState->GetServer();
					if (!server || server != site.server_) {
						m_pMainFrame->ConnectToSite(site, bookmark);
						break;
					}
					else {
						pState->ChangeRemoteDir(bookmark.m_remoteDir, std::wstring(), 0, false, bookmark.m_comparison);
					}
				}
				if (!bookmark.m_localDir.empty()) {
					bool set = pState->SetLocalDir(bookmark.m_localDir.ToStdWstring());

					if (set && bookmark.m_sync) {
						wxASSERT(!bookmark.m_remoteDir.empty());
						pState->SetSyncBrowse(true, bookmark.m_remoteDir);
					}
				}

				if (bookmark.m_comparison && pState->GetComparisonManager()) {
					pState->GetComparisonManager()->CompareListings();
				}

				break;
			}
		}

		return;
	}

	std::map<int, wxString>::const_iterator iter2 = m_bookmark_menu_id_map_global.find(event.GetId());
	if (iter2 != m_bookmark_menu_id_map_global.end()) {
		// We hit a global bookmark
		wxString local_dir;
		CServerPath remote_dir;
		bool sync;
		bool comparison;
		if (!CBookmarksDialog::GetBookmark(iter2->second, local_dir, remote_dir, sync, comparison)) {
			return;
		}

		pState->SetSyncBrowse(false);
		if (!remote_dir.empty() && pState->IsRemoteIdle(true)) {
			ServerWithCredentials const& server = pState->GetServer();
			if (server) {
				CServerPath current_remote_path = pState->GetRemotePath();
				if (!current_remote_path.empty() && current_remote_path.GetType() != remote_dir.GetType()) {
					wxMessageBoxEx(_("Selected global bookmark and current server use a different server type.\nUse site-specific bookmarks for this server."), _("Bookmark"), wxICON_EXCLAMATION, this);
					return;
				}
				pState->ChangeRemoteDir(remote_dir, _T(""), 0, false, comparison);
			}
		}
		if (!local_dir.empty()) {
			bool set = pState->SetLocalDir(local_dir.ToStdWstring());

			if (set && sync) {
				wxASSERT(!remote_dir.empty());
				pState->SetSyncBrowse(true, remote_dir);
			}
		}

		if (comparison && pState->GetComparisonManager()) {
			pState->GetComparisonManager()->CompareListings();
		}

		return;
	}

	event.Skip();
}

void CMenuBar::OnStateChange(CState* pState, t_statechange_notifications notification, const wxString&, const void*)
{
	switch (notification)
	{
	case STATECHANGE_CHANGEDCONTEXT:
		UpdateMenubarState();
		UpdateBookmarkMenu();
		break;
	case STATECHANGE_GLOBALBOOKMARKS:
		UpdateBookmarkMenu();
		break;
	case STATECHANGE_SERVER:
	case STATECHANGE_REMOTE_IDLE:
		UpdateMenubarState();
		UpdateBookmarkMenu();
		break;
	case STATECHANGE_QUEUEPROCESSING:
		{
			const bool check = m_pMainFrame->GetQueue() && m_pMainFrame->GetQueue()->IsActive() != 0;
			Check(XRCID("ID_MENU_TRANSFER_PROCESSQUEUE"), check);
		}
		break;
	case STATECHANGE_SYNC_BROWSE:
		{
			bool is_sync_browse = pState && pState->GetSyncBrowse();
			Check(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), is_sync_browse);
		}
		break;
	case STATECHANGE_COMPARISON:
		{
			bool is_comparing = pState && pState->GetComparisonManager()->IsComparing();
			Check(XRCID("ID_TOOLBAR_COMPARISON"), is_comparing);
		}
		break;
	default:
		break;
	}
}

void CMenuBar::OnOptionsChanged(changed_options_t const& options)
{
	if (options.test(OPTION_ASCIIBINARY)) {
		switch (COptions::Get()->GetOptionVal(OPTION_ASCIIBINARY))
		{
		default:
			Check(XRCID("ID_MENU_TRANSFER_TYPE_AUTO"), true);
			break;
		case 1:
			Check(XRCID("ID_MENU_TRANSFER_TYPE_ASCII"), true);
			break;
		case 2:
			Check(XRCID("ID_MENU_TRANSFER_TYPE_BINARY"), true);
			break;
		}
	}
	if (options.test(OPTION_PRESERVE_TIMESTAMPS)) {
		Check(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), COptions::Get()->GetOptionVal(OPTION_PRESERVE_TIMESTAMPS) != 0);
	}
	if (options.test(OPTION_SHOW_TREE_LOCAL)) {
		Check(XRCID("ID_VIEW_LOCALTREE"), COptions::Get()->GetOptionVal(OPTION_SHOW_TREE_LOCAL) != 0);
	}
	if (options.test(OPTION_SHOW_TREE_REMOTE)) {
		Check(XRCID("ID_VIEW_REMOTETREE"), COptions::Get()->GetOptionVal(OPTION_SHOW_TREE_REMOTE) != 0);
	}
	if (options.test(OPTION_SHOW_QUICKCONNECT)) {
		Check(XRCID("ID_VIEW_QUICKCONNECT"), COptions::Get()->GetOptionVal(OPTION_SHOW_QUICKCONNECT) != 0);
	}
	if (options.test(OPTION_TOOLBAR_HIDDEN)) {
		Check(XRCID("ID_VIEW_TOOLBAR"), COptions::Get()->GetOptionVal(OPTION_TOOLBAR_HIDDEN) == 0);
	}
	if (options.test(OPTION_SHOW_MESSAGELOG)) {
		Check(XRCID("ID_VIEW_MESSAGELOG"), COptions::Get()->GetOptionVal(OPTION_SHOW_MESSAGELOG) != 0);
	}
	if (options.test(OPTION_SHOW_QUEUE)) {
		Check(XRCID("ID_VIEW_QUEUE"), COptions::Get()->GetOptionVal(OPTION_SHOW_QUEUE) != 0);
	}
	if (options.test(OPTION_COMPARE_HIDEIDENTICAL)) {
		Check(XRCID("ID_COMPARE_HIDEIDENTICAL"), COptions::Get()->GetOptionVal(OPTION_COMPARE_HIDEIDENTICAL) != 0);
	}
	if (options.test(OPTION_COMPARISONMODE)) {
		if (COptions::Get()->GetOptionVal(OPTION_COMPARISONMODE) != 1)
			Check(XRCID("ID_COMPARE_SIZE"), true);
		else
			Check(XRCID("ID_COMPARE_DATE"), true);
	}
	if (options.test(OPTION_MESSAGELOG_POSITION)) {
		if (COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION) == 2)
			HideItem(XRCID("ID_VIEW_MESSAGELOG"));
		else {
			ShowItem(XRCID("ID_VIEW_MESSAGELOG"));
			Check(XRCID("ID_VIEW_MESSAGELOG"), COptions::Get()->GetOptionVal(OPTION_SHOW_MESSAGELOG) != 0);
		}
	}
	if (options.test(OPTION_SPEEDLIMIT_ENABLE) || options.test(OPTION_SPEEDLIMIT_INBOUND) || options.test(OPTION_SPEEDLIMIT_OUTBOUND)) {
		UpdateSpeedLimitMenuItem();
	}
}

void CMenuBar::UpdateSpeedLimitMenuItem()
{
	bool enable = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_ENABLE) != 0;

	int downloadLimit = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_INBOUND);
	int uploadLimit = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_OUTBOUND);

	if (!downloadLimit && !uploadLimit)
		enable = false;

	Check(XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_ENABLE"), enable);
}

void CMenuBar::UpdateMenubarState()
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	ServerWithCredentials const& server = pState->GetServer();
	const bool idle = pState->IsRemoteIdle();

	Enable(XRCID("ID_MENU_SERVER_DISCONNECT"), server && idle);
	Enable(XRCID("ID_CANCEL"), server && !idle);
	Enable(XRCID("ID_MENU_SERVER_CMD"), server && idle);
	Enable(XRCID("ID_MENU_FILE_COPYSITEMANAGER"), server.operator bool());
	Enable(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), server.operator bool());

	Check(XRCID("ID_TOOLBAR_COMPARISON"), pState->GetComparisonManager()->IsComparing());
	Check(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), pState->GetSyncBrowse());

	bool canReconnect;
	if (server || !idle)
		canReconnect = false;
	else {
		canReconnect = static_cast<bool>(pState->GetLastSite().server_);
	}
	Enable(XRCID("ID_MENU_SERVER_RECONNECT"), canReconnect);

	Enable(XRCID("ID_MENU_TRANSFER_TYPE"), !server || CServer::ProtocolHasFeature(server.server.GetProtocol(), ProtocolFeature::DataTypeConcept));
	Enable(XRCID("ID_MENU_TRANSFER_PRESERVETIMES"), !server || CServer::ProtocolHasFeature(server.server.GetProtocol(), ProtocolFeature::PreserveTimestamp));
	Enable(XRCID("ID_MENU_SERVER_CMD"), !server || CServer::ProtocolHasFeature(server.server.GetProtocol(), ProtocolFeature::EnterCommand));
}

bool CMenuBar::ShowItem(int id)
{
	for (auto menu_iter = m_hidden_items.begin(); menu_iter != m_hidden_items.end(); ++menu_iter) {
		int offset = 0;

		for (auto iter = menu_iter->second.begin(); iter != menu_iter->second.end(); ++iter) {
			if (iter->second->GetId() != id) {
				offset++;
				continue;
			}

			menu_iter->first->Insert(iter->first - offset, iter->second);
			menu_iter->second.erase(iter);
			if (menu_iter->second.empty())
				m_hidden_items.erase(menu_iter);

			return true;
		}
	}
	return false;
}

bool CMenuBar::HideItem(int id)
{
	wxMenu* pMenu = 0;
	wxMenuItem* pItem = FindItem(id, &pMenu);
	if (!pItem || !pMenu)
		return false;

	size_t pos;
	pItem = pMenu->FindChildItem(id, &pos);
	if (!pItem)
		return false;

	pMenu->Remove(pItem);

	auto menu_iter = m_hidden_items.insert(std::make_pair(pMenu, std::map<int, wxMenuItem*>())).first;

	for (auto iter = menu_iter->second.begin(); iter != menu_iter->second.end(); ++iter) {
		if (iter->first > (int)pos)
			break;

		pos++;
	}

	menu_iter->second[(int)pos] = pItem;

	return true;
}
