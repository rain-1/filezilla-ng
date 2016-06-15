#ifndef __SITEMANAGER_H__
#define __SITEMANAGER_H__

#include <wx/treectrl.h>

#include "xmlfunctions.h"

class Bookmark final
{
public:
	bool operator==(Bookmark const& b) const;
	bool operator!=(Bookmark const& b) const { return !(*this == b); }

	wxString m_localDir;
	CServerPath m_remoteDir;

	bool m_sync{};
	bool m_comparison{};

	wxString m_name;
};

class Site final
{
public:
	bool operator==(Site const& s) const;
	bool operator!=(Site const& s) const { return !(*this == s); }

	CServer m_server;
	wxString m_comments;

	Bookmark m_default_bookmark;

	std::vector<Bookmark> m_bookmarks;

	wxString m_path;

	wxColour m_colour;
};

class CSiteManagerXmlHandler
{
public:
	virtual ~CSiteManagerXmlHandler() {};

	// Adds a folder and descents
	virtual bool AddFolder(wxString const& name, bool expanded) = 0;
	virtual bool AddSite(std::unique_ptr<Site> data) = 0;

	// Go up a level
	virtual bool LevelUp() { return true; } // *Ding*
};

class CSiteManagerXmlHandler;
class CSiteManagerDialog;
class CSiteManager
{
	friend class CSiteManagerDialog;
public:
	// This function also clears the Id map
	static std::unique_ptr<Site> GetSiteById(int id);
	static std::unique_ptr<Site> GetSiteByPath(wxString sitePath);

	static wxString AddServer(CServer server);
	static bool AddBookmark(wxString sitePath, const wxString& name, const wxString &local_dir, const CServerPath &remote_dir, bool sync, bool comparison);
	static bool ClearBookmarks(wxString sitePath);

	static std::unique_ptr<wxMenu> GetSitesMenu();
	static void ClearIdMap();

	static bool UnescapeSitePath(wxString path, std::list<wxString>& result);
	static wxString EscapeSegment(wxString segment);

	static bool HasSites();

	static bool ReadBookmarkElement(Bookmark & bookmark, pugi::xml_node element);

protected:
	static bool Load(CSiteManagerXmlHandler& pHandler);
	static bool Load(pugi::xml_node element, CSiteManagerXmlHandler& pHandler);
	static std::unique_ptr<Site> ReadServerElement(pugi::xml_node element);

	static pugi::xml_node GetElementByPath(pugi::xml_node node, std::list<wxString> const& segments);
	static wxString BuildPath(wxChar root, std::list<wxString> const& segments);

	static std::map<int, std::unique_ptr<Site>> m_idMap;

	// The map maps event id's to sites
	static std::unique_ptr<wxMenu> GetSitesMenu_Predefined(std::map<int, std::unique_ptr<Site>> &idMap);

	static bool LoadPredefined(CSiteManagerXmlHandler& handler);
};

#endif //__SITEMANAGER_H__
