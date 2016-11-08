#include <filezilla.h>
#include "filezillaapp.h"
#include "import.h"
#include "xmlfunctions.h"
#include "ipcmutex.h"
#include "Options.h"
#include "queue.h"
#include "xrc_helper.h"

CImportDialog::CImportDialog(wxWindow* parent, CQueueView* pQueueView)
	: m_parent(parent), m_pQueueView(pQueueView)
{
}

void CImportDialog::Run()
{
	wxFileDialog dlg(m_parent, _("Select file to import settings from"), wxString(),
					_T("FileZilla.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	dlg.CenterOnParent();

	if (dlg.ShowModal() != wxID_OK)
		return;

	wxFileName fn(dlg.GetPath());
	wxString const path = fn.GetPath();
	wxString const settingsDir(COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR));
	if (path == settingsDir) {
		wxMessageBoxEx(_("You cannot import settings from FileZilla's own settings directory."), _("Error importing"), wxICON_ERROR, m_parent);
		return;
	}

	CXmlFile fz3(dlg.GetPath().ToStdWstring());
	auto fz3Root = fz3.Load();
	if (fz3Root) {
		bool settings = fz3Root.child("Settings") != 0;
		bool queue = fz3Root.child("Queue") != 0;
		bool sites = fz3Root.child("Servers") != 0;
		bool filters = fz3Root.child("Filters") != 0;

		if (settings || queue || sites || filters) {
			if (!Load(m_parent, _T("ID_IMPORT"))) {
				wxBell();
				return;
			}
			if (!queue) {
				xrc_call(*this, "ID_QUEUE", &wxCheckBox::Hide);
			}
			if (!sites) {
				xrc_call(*this, "ID_SITEMANAGER", &wxCheckBox::Hide);
			}
			if (!settings) {
				xrc_call(*this, "ID_SETTINGS", &wxCheckBox::Hide);
			}
			if (!filters) {
				xrc_call(*this, "ID_FILTERS", &wxCheckBox::Hide);
			}
			GetSizer()->Fit(this);

			if (ShowModal() != wxID_OK) {
				return;
			}

			if (fz3.IsFromFutureVersion()) {
				wxString msg = wxString::Format(_("The file '%s' has been created by a more recent version of FileZilla.\nLoading files created by newer versions can result in loss of data.\nDo you want to continue?"), fz3.GetFileName());
				if (wxMessageBoxEx(msg, _("Detected newer version of FileZilla"), wxICON_QUESTION | wxYES_NO) != wxYES) {
					return;
				}
			}

			if (queue && xrc_call(*this, "ID_QUEUE", &wxCheckBox::IsChecked)) {
				m_pQueueView->ImportQueue(fz3Root.child("Queue"), true);
			}

			if (sites && xrc_call(*this, "ID_SITEMANAGER", &wxCheckBox::IsChecked)) {
				ImportSites(fz3Root.child("Servers"));
			}

			if (settings && xrc_call(*this, "ID_SETTINGS", &wxCheckBox::IsChecked)) {
				COptions::Get()->Import(fz3Root.child("Settings"));
				wxMessageBoxEx(_("The settings have been imported. You have to restart FileZilla for all settings to have effect."), _("Import successful"), wxOK, this);
			}

			if (filters && xrc_call(*this, "ID_FILTERS", &wxCheckBox::IsChecked)) {
				CFilterManager::Import(fz3Root);
			}

			wxMessageBoxEx(_("The selected categories have been imported."), _("Import successful"), wxOK, this);
			return;
		}
	}

	CXmlFile fz2(dlg.GetPath().ToStdWstring(), "FileZilla");
	auto fz2Root = fz2.Load();
	if (fz2Root) {
		auto sites_fz2 = fz2Root.child("Sites");
		if (sites_fz2) {
			int res = wxMessageBoxEx(_("The file you have selected contains site manager data from a previous version of FileZilla.\nDue to differences in the storage format, only host, port, username and password will be imported.\nContinue with the import?"),
				_("Import data from older version"), wxICON_QUESTION | wxYES_NO);

			if (res == wxYES)
				ImportLegacySites(sites_fz2);
			return;
		}
	}

	wxMessageBoxEx(_("File does not contain any importable data."), _("Error importing"), wxICON_ERROR, m_parent);
}

bool CImportDialog::ImportLegacySites(pugi::xml_node sites)
{
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto element = file.Load();
	if (!element) {
		wxString msg = wxString::Format(_("Could not load \"%s\", please make sure the file is valid and can be accessed.\nAny changes made in the Site Manager will not be saved."), file.GetFileName());
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	auto currentSites = element.child("Servers");
	if (!currentSites)
		currentSites = element.append_child("Servers");

	if (!ImportLegacySites(sites, currentSites))
		return false;

	return file.Save(true);
}

std::wstring CImportDialog::DecodeLegacyPassword(std::wstring const& pass)
{
	if (pass.size() % 3) {
		return std::wstring();
	}

	std::wstring output;
	const char* key = "FILEZILLA1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	int pos = (pass.size() / 3) % strlen(key);
	for (size_t i = 0; i < pass.size(); i += 3) {
		if (pass[i] < '0' || pass[i] > '9' ||
			pass[i + 1] < '0' || pass[i + 1] > '9' ||
			pass[i + 2] < '0' || pass[i + 2] > '9')
			return std::wstring();
		int number = (pass[i] - '0') * 100 +
						(pass[i + 1] - '0') * 10 +
						(pass[i + 2] - '0');
		wchar_t c = number ^ key[(i / 3 + pos) % strlen(key)];
		output += c;
	}

	return output;
}

bool CImportDialog::ImportLegacySites(pugi::xml_node sitesToImport, pugi::xml_node existingSites)
{
	for (auto importFolder = sitesToImport.child("Folder"); importFolder; importFolder = importFolder.next_sibling("Folder")) {
		std::wstring name = GetTextAttribute(importFolder, "Name");
		if (name.empty()) {
			continue;
		}

		std::wstring newName = name;
		int i = 2;
		pugi::xml_node folder;
		while (!(folder = GetFolderWithName(existingSites, newName))) {
			newName = fz::sprintf(L"%s %d", name, i++);
		}

		ImportLegacySites(importFolder, folder);
	}

	for (auto importSite = sitesToImport.child("Site"); importSite; importSite = importSite.next_sibling("Site")) {
		std::wstring name = GetTextAttribute(importSite, "Name");
		if (name.empty()) {
			continue;
		}

		std::wstring host = GetTextAttribute(importSite, "Host");
		if (host.empty()) {
			continue;
		}

		int port = GetAttributeInt(importSite, "Port");
		if (port < 1 || port > 65535) {
			continue;
		}

		int serverType = GetAttributeInt(importSite, "ServerType");
		if (serverType < 0 || serverType > 4) {
			continue;
		}

		int protocol;
		switch (serverType)
		{
		default:
		case 0:
			protocol = 0;
			break;
		case 1:
			protocol = 3;
			break;
		case 2:
		case 4:
			protocol = 4;
			break;
		case 3:
			protocol = 1;
			break;
		}

		bool dontSavePass = GetAttributeInt(importSite, "DontSavePass") == 1;

		int logontype = GetAttributeInt(importSite, "Logontype");
		if (logontype < 0 || logontype > 2) {
			continue;
		}
		if (logontype == 2) {
			logontype = 4;
		}
		if (logontype == 1 && dontSavePass) {
			logontype = 2;
		}

		std::wstring user = GetTextAttribute(importSite, "User");
		std::wstring pass = DecodeLegacyPassword(GetTextAttribute(importSite, "Pass"));
		std::wstring account = GetTextAttribute(importSite, "Account");
		if (logontype && user.empty()) {
			continue;
		}

		// Find free name
		std::wstring newName = name;
		int i = 2;
		while (HasEntryWithName(existingSites, newName)) {
			newName = fz::sprintf(L"%s %d", name, i++);
		}

		auto server = existingSites.append_child("Server");
		AddTextElement(server, newName);

		AddTextElement(server, "Host", host);
		AddTextElement(server, "Port", port);
		AddTextElement(server, "Protocol", protocol);
		AddTextElement(server, "Logontype", logontype);
		AddTextElement(server, "User", user);
		AddTextElement(server, "Pass", pass);
		AddTextElement(server, "Account", account);
	}

	return true;
}

bool CImportDialog::HasEntryWithName(pugi::xml_node element, std::wstring const& name)
{
	pugi::xml_node child;
	for (child = element.child("Server"); child; child = child.next_sibling("Server")) {
		std::wstring childName = GetTextElement_Trimmed(child);
		if (!fz::stricmp(name, childName)) {
			return true;
		}
	}
	for (child = element.child("Folder"); child; child = child.next_sibling("Folder")) {
		std::wstring childName = GetTextElement_Trimmed(child);
		if (!fz::stricmp(name, childName)) {
			return true;
		}
	}

	return false;
}

pugi::xml_node CImportDialog::GetFolderWithName(pugi::xml_node element, std::wstring const& name)
{
	pugi::xml_node child;
	for (child = element.child("Server"); child; child = child.next_sibling("Server")) {
		std::wstring childName = GetTextElement_Trimmed(child);
		if (!fz::stricmp(name, childName)) {
			// We do not allow servers and directories to share the same name
			return pugi::xml_node();
		}
	}

	for (child = element.child("Folder"); child; child = child.next_sibling("Folder")) {
		std::wstring childName = GetTextElement_Trimmed(child);
		if (!fz::stricmp(name, childName)) {
			return child;
		}
	}

	child = element.append_child("Folder");
	AddTextElement(child, name);

	return child;
}

bool CImportDialog::ImportSites(pugi::xml_node sites)
{
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	auto element = file.Load();
	if (!element) {
		wxString msg = wxString::Format(_("Could not load \"%s\", please make sure the file is valid and can be accessed.\nAny changes made in the Site Manager will not be saved."), file.GetFileName());
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	auto currentSites = element.child("Servers");
	if (!currentSites) {
		currentSites = element.append_child("Servers");
	}

	if (!ImportSites(sites, currentSites)) {
		return false;
	}

	return file.Save(true);
}

bool CImportDialog::ImportSites(pugi::xml_node sitesToImport, pugi::xml_node existingSites)
{
	for (auto importFolder = sitesToImport.child("Folder"); importFolder; importFolder = importFolder.next_sibling("Folder")) {
		std::wstring name = GetTextElement_Trimmed(importFolder, "Name");
		if (name.empty()) {
			name = GetTextElement_Trimmed(importFolder);
		}
		if (name.empty()) {
			continue;
		}

		std::wstring newName = name;
		int i = 2;
		pugi::xml_node folder;
		while (!(folder = GetFolderWithName(existingSites, newName))) {
			newName = fz::sprintf(L"%s %d", name, i++);
		}

		ImportSites(importFolder, folder);
	}

	for (auto importSite = sitesToImport.child("Server"); importSite; importSite = importSite.next_sibling("Server")) {
		std::wstring name = GetTextElement_Trimmed(importSite, "Name");
		if (name.empty()) {
			name = GetTextElement_Trimmed(importSite);
		}
		if (name.empty()) {
			continue;
		}

		// Find free name
		std::wstring newName = name;
		int i = 2;
		while (HasEntryWithName(existingSites, newName)) {
			newName = fz::sprintf(L"%s %d", name, i++);
		}

		auto server = existingSites.append_copy(importSite);
		AddTextElement(server, "Name", newName, true);
		AddTextElement(server, newName);
	}

	return true;
}
