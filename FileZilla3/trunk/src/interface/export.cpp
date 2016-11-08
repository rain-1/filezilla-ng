#include <filezilla.h>
#include "export.h"
#include "filezillaapp.h"
#include "xmlfunctions.h"
#include "ipcmutex.h"
#include "queue.h"
#include "xrc_helper.h"

CExportDialog::CExportDialog(wxWindow* parent, CQueueView* pQueueView)
	: m_parent(parent), m_pQueueView(pQueueView)
{
}

void CExportDialog::Run()
{
	if (!Load(m_parent, _T("ID_EXPORT"))) {
		return;
	}

	if (ShowModal() != wxID_OK) {
		return;
	}

	bool sitemanager = xrc_call(*this, "ID_SITEMANAGER", &wxCheckBox::GetValue);
	bool settings = xrc_call(*this, "ID_SETTINGS", &wxCheckBox::GetValue);
	bool queue = xrc_call(*this, "ID_QUEUE", &wxCheckBox::GetValue);
	bool filters = xrc_call(*this, "ID_FILTERS", &wxCheckBox::GetValue);

	if (!sitemanager && !settings && !queue && !filters) {
		wxMessageBoxEx(_("No category to export selected"), _("Error exporting settings"), wxICON_ERROR, m_parent);
		return;
	}

	wxString str;
	if (sitemanager && !queue && !settings && !filters) {
		str = _("Select file for exported sites");
	}
	else if (!sitemanager && queue && !settings && !filters) {
		str = _("Select file for exported queue");
	}
	else if (!sitemanager && !queue && settings && !filters) {
		str = _("Select file for exported settings");
	}
	else if (!sitemanager && !queue && !settings && filters) {
		str = _("Select file for exported filters");
	}
	else {
		str = _("Select file for exported data");
	}

	wxFileDialog dlg(m_parent, str, wxString(),
					_T("FileZilla.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	CXmlFile xml(dlg.GetPath().ToStdWstring());

	auto exportRoot = xml.CreateEmpty();

	if (sitemanager) {
		CInterProcessMutex mutex(MUTEX_SITEMANAGER);

		CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
		auto document = file.Load();
		if (document) {
			auto element = document.child("Servers");
			if (element) {
				exportRoot.append_copy(element);
			}
		}
	}
	if (settings) {
		CInterProcessMutex mutex(MUTEX_OPTIONS);
		CXmlFile file(wxGetApp().GetSettingsFile(_T("filezilla")));
		auto document = file.Load();
		if (document) {
			auto element = document.child("Settings");
			if (element) {
				exportRoot.append_copy(element);
			}
		}
	}

	if (queue) {
		m_pQueueView->WriteToFile(exportRoot);
	}

	if (filters) {
		CInterProcessMutex mutex(MUTEX_FILTERS);
		CXmlFile file(wxGetApp().GetSettingsFile(_T("filters")));
		auto document = file.Load();
		if (document) {
			auto element = document.child("Filters");
			if (element) {
				exportRoot.append_copy(element);
			}
			element = document.child("Sets");
			if (element) {
				exportRoot.append_copy(element);
			}
		}
	}

	xml.Save(true);
}
