	#include <filezilla.h>
#include "sitemanager.h"

#include "filezillaapp.h"
#include "ipcmutex.h"
#include "Options.h"
#include "xmlfunctions.h"

std::map<int, std::unique_ptr<CSiteManagerItemData_Site>> CSiteManager::m_idMap;

bool CSiteManager::Load(CSiteManagerXmlHandler& handler)
{
	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return false;
	}

	auto element = document.child("Servers");
	if (!element) {
		return false;
	}

	return Load(element, handler);
}

bool CSiteManager::Load(pugi::xml_node element, CSiteManagerXmlHandler& handler)
{
	wxASSERT(element);

	for (auto child = element.first_child(); child; child = child.next_sibling()) {
		if (!strcmp(child.name(), "Folder")) {
			wxString name = GetTextElement_Trimmed(child);
			if (name.empty())
				continue;

			const bool expand = GetTextAttribute(child, "expanded") != _T("0");
			if (!handler.AddFolder(name, expand))
				return false;
			Load(child, handler);
			if (!handler.LevelUp())
				return false;
		}
		else if (!strcmp(child.name(), "Server")) {
			std::unique_ptr<CSiteManagerItemData_Site> data = ReadServerElement(child);

			if (data) {
				handler.AddSite(std::move(data));

				// Bookmarks
				for (auto bookmark = child.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
					wxString name = GetTextElement_Trimmed(bookmark, "Name");
					if (name.empty())
						continue;

					auto bookmarkData = std::make_unique<CSiteManagerItemData>(CSiteManagerItemData::BOOKMARK);

					bookmarkData->m_localDir = GetTextElement(bookmark, "LocalDir");
					bookmarkData->m_remoteDir.SetSafePath(GetTextElement(bookmark, "RemoteDir"));
					if (bookmarkData->m_localDir.empty() && bookmarkData->m_remoteDir.empty()) {
						continue;
					}

					if (!bookmarkData->m_localDir.empty() && !bookmarkData->m_remoteDir.empty())
						bookmarkData->m_sync = GetTextElementBool(bookmark, "SyncBrowsing", false);

					bookmarkData->m_comparison = GetTextElementBool(bookmark, "DirectoryComparison", false);

					handler.AddBookmark(name, std::move(bookmarkData));
				}

				if (!handler.LevelUp())
					return false;
			}
		}
	}

	return true;
}

std::unique_ptr<CSiteManagerItemData_Site> CSiteManager::ReadServerElement(pugi::xml_node element)
{
	CServer server;
	if (!::GetServer(element, server))
		return 0;
	if (server.GetName().empty())
		return 0;

	auto data = std::make_unique<CSiteManagerItemData_Site>(server);
	data->m_comments = GetTextElement(element, "Comments");
	data->m_localDir = GetTextElement(element, "LocalDir");
	data->m_remoteDir.SetSafePath(GetTextElement(element, "RemoteDir"));

	if (!data->m_localDir.empty() && !data->m_remoteDir.empty())
		data->m_sync = GetTextElementBool(element, "SyncBrowsing", false);

	data->m_comparison = GetTextElementBool(element, "DirectoryComparison", false);

	return data;
}

class CSiteManagerXmlHandler_Menu : public CSiteManagerXmlHandler
{
public:
	CSiteManagerXmlHandler_Menu(wxMenu* pMenu, std::map<int, std::unique_ptr<CSiteManagerItemData_Site>> *idMap, bool predefined)
		: m_pMenu(pMenu), m_idMap(idMap)
	{
		m_added_site = false;

		if (predefined)
			path = _T("1");
		else
			path = _T("0");
	}

	unsigned int GetInsertIndex(wxMenu* pMenu, const wxString& name)
	{
		unsigned int i;
		for (i = 0; i < pMenu->GetMenuItemCount(); ++i)
		{
			const wxMenuItem* const pItem = pMenu->FindItemByPosition(i);
			if (!pItem)
				continue;

			// Use same sorting as site tree in site manager
#ifdef __WXMSW__
			if (pItem->GetItemLabelText().CmpNoCase(name) > 0)
				break;
#else
			if (pItem->GetItemLabelText() > name)
				break;
#endif
		}

		return i;
	}

	virtual bool AddFolder(const wxString& name, bool)
	{
		m_parents.push_back(m_pMenu);
		m_childNames.push_back(name);
		m_paths.push_back(path);
		path += _T("/") + CSiteManager::EscapeSegment(name);

		m_pMenu = new wxMenu;

		return true;
	}

	virtual bool AddSite(std::unique_ptr<CSiteManagerItemData_Site> data)
	{
		wxString newName(data->m_server.GetName());
		int i = GetInsertIndex(m_pMenu, newName);
		newName.Replace(_T("&"), _T("&&"));
		wxMenuItem* pItem = m_pMenu->Insert(i, wxID_ANY, newName);

		data->m_path = path + _T("/") + CSiteManager::EscapeSegment(data->m_server.GetName());

		(*m_idMap)[pItem->GetId()] = std::move(data);

		m_added_site = true;

		return true;
	}

	virtual bool AddBookmark(const wxString&, std::unique_ptr<CSiteManagerItemData>)
	{
		return true;
	}

	// Go up a level
	virtual bool LevelUp()
	{
		if (m_added_site)
		{
			m_added_site = false;
			return true;
		}

		if (m_parents.empty() || m_childNames.empty())
			return false;

		wxMenu* pChild = m_pMenu;
		m_pMenu = m_parents.back();
		if (pChild->GetMenuItemCount())
		{
			wxString name = m_childNames.back();
			int i = GetInsertIndex(m_pMenu, name);
			name.Replace(_T("&"), _T("&&"));

			wxMenuItem* pItem = new wxMenuItem(m_pMenu, wxID_ANY, name, _T(""), wxITEM_NORMAL, pChild);
			m_pMenu->Insert(i, pItem);
		}
		else
			delete pChild;
		m_childNames.pop_back();
		m_parents.pop_back();

		path = m_paths.back();
		m_paths.pop_back();

		return true;
	}

protected:
	wxMenu* m_pMenu;

	std::map<int, std::unique_ptr<CSiteManagerItemData_Site>> *m_idMap;

	std::list<wxMenu*> m_parents;
	std::list<wxString> m_childNames;

	bool m_added_site;

	wxString path;
	std::list<wxString> m_paths;
};


class CSiteManagerXmlHandler_Stats : public CSiteManagerXmlHandler
{
public:
	virtual bool AddFolder(const wxString&, bool)
	{
		++directories_;
		return true;
	}

	virtual bool AddSite(std::unique_ptr<CSiteManagerItemData_Site>)
	{
		++sites_;
		return true;
	}

	virtual bool AddBookmark(const wxString&, std::unique_ptr<CSiteManagerItemData>)
	{
		++bookmarks_;
		return true;
	}

	unsigned int sites_{};
	unsigned int directories_{};
	unsigned int bookmarks_{};
};

std::unique_ptr<wxMenu> CSiteManager::GetSitesMenu()
{
	ClearIdMap();

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	auto predefinedSites = GetSitesMenu_Predefined(m_idMap);

	auto pMenu = std::make_unique<wxMenu>();
	CSiteManagerXmlHandler_Menu handler(pMenu.get(), &m_idMap, false);

	bool res = Load(handler);
	if (!res || !pMenu->GetMenuItemCount()) {
		pMenu.reset();
	}

	if (pMenu) {
		if (!predefinedSites)
			return pMenu;

		auto pRootMenu = std::make_unique<wxMenu>();
		pRootMenu->AppendSubMenu(predefinedSites.release(), _("Predefined Sites"));
		pRootMenu->AppendSubMenu(pMenu.release(), _("My Sites"));

		return pRootMenu;
	}

	if (predefinedSites)
		return predefinedSites;

	pMenu = std::make_unique<wxMenu>();
	wxMenuItem* pItem = pMenu->Append(wxID_ANY, _("No sites available"));
	pItem->Enable(false);
	return pMenu;
}

void CSiteManager::ClearIdMap()
{
	m_idMap.clear();
}

bool CSiteManager::LoadPredefined(CSiteManagerXmlHandler& handler)
{
	CLocalPath const defaultsDir = wxGetApp().GetDefaultsDir();
	if (defaultsDir.empty())
		return false;

	wxString const name(defaultsDir.GetPath() + _T("fzdefaults.xml"));
	CXmlFile file(name);

	auto document = file.Load();
	if (!document)
		return false;

	auto element = document.child("Servers");
	if (!element)
		return false;

	if (!Load(element, handler)) {
		return false;
	}

	return true;
}

std::unique_ptr<wxMenu> CSiteManager::GetSitesMenu_Predefined(std::map<int, std::unique_ptr<CSiteManagerItemData_Site>> &idMap)
{
	auto pMenu = std::make_unique<wxMenu>();
	CSiteManagerXmlHandler_Menu handler(pMenu.get(), &idMap, true);

	if (!LoadPredefined(handler)) {
		return 0;
	}

	if (!pMenu->GetMenuItemCount()) {
		return 0;
	}

	return pMenu;
}

std::unique_ptr<CSiteManagerItemData_Site> CSiteManager::GetSiteById(int id)
{
	auto iter = m_idMap.find(id);

	std::unique_ptr<CSiteManagerItemData_Site> pData;
	if (iter != m_idMap.end()) {
		pData = std::move(iter->second);
	}
	ClearIdMap();

	return pData;
}

bool CSiteManager::UnescapeSitePath(wxString path, std::list<wxString>& result)
{
	result.clear();

	wxString name;
	wxChar const* p = path.c_str();

	// Undo escapement
	bool lastBackslash = false;
	while (*p)
	{
		const wxChar& c = *p;
		if (c == '\\')
		{
			if (lastBackslash)
			{
				name += _T("\\");
				lastBackslash = false;
			}
			else
				lastBackslash = true;
		}
		else if (c == '/')
		{
			if (lastBackslash)
			{
				name += _T("/");
				lastBackslash = 0;
			}
			else
			{
				if (!name.empty())
					result.push_back(name);
				name.clear();
			}
		}
		else
			name += *p;
		++p;
	}
	if (lastBackslash)
		return false;
	if (!name.empty())
		result.push_back(name);

	return !result.empty();
}

wxString CSiteManager::EscapeSegment(wxString segment)
{
	segment.Replace(_T("\\"), _T("\\\\"));
	segment.Replace(_T("/"), _T("\\/"));
	return segment;
}

wxString CSiteManager::BuildPath(wxChar root, std::list<wxString> const& segments)
{
	wxString ret = root;
	for (std::list<wxString>::const_iterator it = segments.begin(); it != segments.end(); ++it)
		ret += _T("/") + EscapeSegment(*it);

	return ret;
}

std::unique_ptr<CSiteManagerItemData_Site> CSiteManager::GetSiteByPath(wxString sitePath)
{
	wxChar c = sitePath.empty() ? 0 : sitePath[0];
	if (c != '0' && c != '1') {
		wxMessageBoxEx(_("Site path has to begin with 0 or 1."), _("Invalid site path"));
		return 0;
	}

	sitePath = sitePath.Mid(1);

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file;
	if (c == '0') {
		file.SetFileName(wxGetApp().GetSettingsFile(_T("sitemanager")));
	}
	else {
		CLocalPath const defaultsDir = wxGetApp().GetDefaultsDir();
		if (defaultsDir.empty()) {
			wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
			return 0;
		}
		file.SetFileName(defaultsDir.GetPath() + _T("fzdefaults.xml"));
	}

	auto document = file.Load();
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return 0;
	}

	auto element = document.child("Servers");
	if (!element) {
		wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
		return 0;
	}

	std::list<wxString> segments;
	if (!UnescapeSitePath(sitePath, segments) || segments.empty()) {
		wxMessageBoxEx(_("Site path is malformed."), _("Invalid site path"));
		return 0;
	}

	auto child = GetElementByPath(element, segments);
	if (!child) {
		wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
		return 0;
	}

	pugi::xml_node bookmark;
	if (!strcmp(child.name(), "Bookmark")) {
		bookmark = child;
		child = child.parent();
		segments.pop_back();
	}

	std::unique_ptr<CSiteManagerItemData_Site> data = ReadServerElement(child);

	if (!data) {
		wxMessageBoxEx(_("Could not read server item."), _("Invalid site path"));
		return 0;
	}

	if (bookmark) {
		data->m_localDir = GetTextElement(bookmark, "LocalDir");
		data->m_remoteDir.SetSafePath(GetTextElement(bookmark, "RemoteDir"));
		if (!data->m_localDir.empty() && !data->m_remoteDir.empty()) {
			data->m_sync = GetTextElementBool(bookmark, "SyncBrowsing", false);
		}
		else
			data->m_sync = false;
		data->m_comparison = GetTextElementBool(bookmark, "DirectoryComparison", false);
	}

	data->m_path = BuildPath( c, segments );

	return data;
}

bool CSiteManager::GetBookmarks(wxString sitePath, std::list<wxString> &bookmarks)
{
	if (sitePath.empty())
		return false;
	wxChar c = sitePath[0];
	if (c != '0' && c != '1')
		return false;

	sitePath = sitePath.Mid(1);

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file;
	if (c == '0') {
		file.SetFileName(wxGetApp().GetSettingsFile(_T("sitemanager")));
	}
	else {
		CLocalPath const defaultsDir = wxGetApp().GetDefaultsDir();
		if (defaultsDir.empty()) {
			wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
			return 0;
		}
		file.SetFileName(defaultsDir.GetPath() + _T("fzdefaults.xml"));
	}

	auto document = file.Load();
	if (!document) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return false;
	}

	auto element = document.child("Servers");
	if (!element)
		return false;

	std::list<wxString> segments;
	if (!UnescapeSitePath(sitePath, segments)) {
		wxMessageBoxEx(_("Site path is malformed."), _("Invalid site path"));
		return 0;
	}

	auto child = GetElementByPath(element, segments);

	if (child && !strcmp(child.name(), "Bookmark"))
		child = child.parent();

	if (!child || strcmp(child.name(), "Server"))
		return 0;

	// Bookmarks
	for (auto bookmark = child.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		wxString name = GetTextElement_Trimmed(bookmark, "Name");
		if (name.empty())
			continue;

		wxString localPath = GetTextElement(bookmark, "LocalDir");
		CServerPath remotePath;
		remotePath.SetSafePath(GetTextElement(bookmark, "RemoteDir"));
		if (localPath.empty() && remotePath.empty())
			continue;

		bookmarks.push_back(name);
	}

	return true;
}

wxString CSiteManager::AddServer(CServer server)
{
	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The server could not be added.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return wxString();
	}

	auto element = document.child("Servers");
	if (!element)
		return wxString();

	std::list<wxString> names;
	for (auto child = element.child("Server"); child; child = child.next_sibling("Server")) {
		wxString name = GetTextElement(child, "Name");
		if (name.empty())
			continue;

		names.push_back(name);
	}

	wxString name = _("New site");
	int i = 1;

	for (;;) {
		std::list<wxString>::const_iterator iter;
		for (iter = names.begin(); iter != names.end(); ++iter) {
			if (*iter == name)
				break;
		}
		if (iter == names.end())
			break;

		name = _("New site") + wxString::Format(_T(" %d"), ++i);
	}

	server.SetName(name);

	auto xServer = element.append_child("Server");
	SetServer(xServer, server);
	AddTextElement(xServer, name);

	if (!file.Save(false)) {
		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2)
			return wxString();

		wxString msg = wxString::Format(_("Could not write \"%s\", any changes to the Site Manager could not be saved: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
		return wxString();
	}

	return _T("0/") + EscapeSegment(name);
}

pugi::xml_node CSiteManager::GetElementByPath(pugi::xml_node node, std::list<wxString> const& segments)
{
	for (std::list<wxString>::const_iterator it = segments.begin(); it != segments.end(); ++it) {
		const wxString & segment = *it;

		pugi::xml_node child;
		for (child = node.first_child(); child; child = child.next_sibling()) {
			if (strcmp(child.name(), "Server") && strcmp(child.name(), "Folder") && strcmp(child.name(), "Bookmark"))
				continue;

			wxString name = GetTextElement_Trimmed(child, "Name");
			if (name.empty())
				name = GetTextElement_Trimmed(child);
			if (name.empty())
				continue;

			if (name == segment)
				break;
		}
		if (!child)
			return pugi::xml_node();

		node = child;
		continue;
	}

	return node;
}

bool CSiteManager::AddBookmark(wxString sitePath, const wxString& name, const wxString &local_dir, const CServerPath &remote_dir, bool sync, bool comparison)
{
	if (local_dir.empty() && remote_dir.empty())
		return false;

	wxChar c = sitePath.empty() ? 0 : sitePath[0];
	if (c != '0')
		return false;

	sitePath = sitePath.Mid(1);

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The bookmark could not be added.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	auto element = document.child("Servers");
	if (!element)
		return false;

	std::list<wxString> segments;
	if (!UnescapeSitePath(sitePath, segments)) {
		wxMessageBoxEx(_("Site path is malformed."), _("Invalid site path"));
		return 0;
	}

	auto child = GetElementByPath(element, segments);
	if (!child || strcmp(child.name(), "Server")) {
		wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
		return 0;
	}

	// Bookmarks
	pugi::xml_node insertBefore, bookmark;
	for (bookmark = child.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		wxString old_name = GetTextElement_Trimmed(bookmark, "Name");
		if (old_name.empty())
			continue;

		if (name == old_name) {
			wxMessageBoxEx(_("Name of bookmark already exists."), _("New bookmark"), wxICON_EXCLAMATION);
			return false;
		}
		if (name < old_name && !insertBefore)
			insertBefore = bookmark;
	}

	if (insertBefore)
		bookmark = child.insert_child_before("Bookmark", insertBefore);
	else
		bookmark = child.append_child("Bookmark");
	AddTextElement(bookmark, "Name", name);
	if (!local_dir.empty())
		AddTextElement(bookmark, "LocalDir", local_dir);
	if (!remote_dir.empty())
		AddTextElement(bookmark, "RemoteDir", remote_dir.GetSafePath());
	if (sync)
		AddTextElementRaw(bookmark, "SyncBrowsing", "1");
	if (comparison)
		AddTextElementRaw(bookmark, "DirectoryComparison", "1");

	if (!file.Save(false)) {
		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2)
			return true;

		wxString msg = wxString::Format(_("Could not write \"%s\", the selected sites could not be exported: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}

	return true;
}

bool CSiteManager::ClearBookmarks(wxString sitePath)
{
	wxChar const c = sitePath.empty() ? 0 : sitePath[0];
	if (c != '0')
		return false;

	sitePath = sitePath.Mid(1);

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto document = file.Load();
	if (!document) {
		wxString msg = file.GetError() + _T("\n") + _("The bookmarks could not be cleared.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	auto element = document.child("Servers");
	if (!element)
		return false;

	std::list<wxString> segments;
	if (!UnescapeSitePath(sitePath, segments)) {
		wxMessageBoxEx(_("Site path is malformed."), _("Invalid site path"));
		return 0;
	}

	auto child = GetElementByPath(element, segments);
	if (!child || strcmp(child.name(), "Server")) {
		wxMessageBoxEx(_("Site does not exist."), _("Invalid site path"));
		return 0;
	}

	auto bookmark = child.child("Bookmark");
	while (bookmark) {
		child.remove_child(bookmark);
		bookmark = child.child("Bookmark");
	}

	if (!file.Save(false)) {
		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2)
			return true;

		wxString msg = wxString::Format(_("Could not write \"%s\", the selected sites could not be exported: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}

	return true;
}

bool CSiteManager::HasSites()
{
	CSiteManagerXmlHandler_Stats handler;
	Load(handler);

	return handler.sites_ > 0;
}
