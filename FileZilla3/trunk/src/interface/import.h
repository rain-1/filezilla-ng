#ifndef __IMPORT_H__
#define __IMPORT_H__

#include "dialogex.h"

#include "xmlfunctions.h"

class CQueueView;
class CImportDialog : public wxDialogEx
{
public:
	CImportDialog(wxWindow* parent, CQueueView* pQueueView);

	void Run();

protected:

	// Import function for Site Manager
	bool HasEntryWithName(pugi::xml_node element, wxString const& name);
	pugi::xml_node GetFolderWithName(pugi::xml_node element, wxString const& name);
	bool ImportSites(pugi::xml_node sites);
	bool ImportSites(pugi::xml_node sitesToImport, pugi::xml_node existingSites);
	bool ImportLegacySites(pugi::xml_node sites);
	bool ImportLegacySites(pugi::xml_node sitesToImport, pugi::xml_node existingSites);
	wxString DecodeLegacyPassword(wxString pass);

	wxWindow* const m_parent;
	CQueueView* m_pQueueView;
};

#endif //__IMPORT_H__
