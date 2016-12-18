#include <filezilla.h>
#include "filter.h"
#include "filteredit.h"
#include "filezillaapp.h"
#include "inputdialog.h"
#include "ipcmutex.h"
#include "Mainfrm.h"
#include "Options.h"
#include "state.h"
#include "xmlfunctions.h"

#include <libfilezilla/local_filesys.hpp>

#include <array>

bool CFilterManager::m_loaded = false;
std::vector<CFilter> CFilterManager::m_globalFilters;
std::vector<CFilterSet> CFilterManager::m_globalFilterSets;
unsigned int CFilterManager::m_globalCurrentFilterSet = 0;
bool CFilterManager::m_filters_disabled = false;

BEGIN_EVENT_TABLE(CFilterDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CFilterDialog::OnOkOrApply)
EVT_BUTTON(XRCID("wxID_CANCEL"), CFilterDialog::OnCancel)
EVT_BUTTON(XRCID("wxID_APPLY"), CFilterDialog::OnOkOrApply)
EVT_BUTTON(XRCID("ID_EDIT"), CFilterDialog::OnEdit)
EVT_CHECKLISTBOX(wxID_ANY, CFilterDialog::OnFilterSelect)
EVT_BUTTON(XRCID("ID_SAVESET"), CFilterDialog::OnSaveAs)
EVT_BUTTON(XRCID("ID_RENAMESET"), CFilterDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETESET"), CFilterDialog::OnDeleteSet)
EVT_CHOICE(XRCID("ID_SETS"), CFilterDialog::OnSetSelect)

EVT_BUTTON(XRCID("ID_LOCAL_ENABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_LOCAL_DISABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_REMOTE_ENABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_REMOTE_DISABLEALL"), CFilterDialog::OnChangeAll)
END_EVENT_TABLE()

namespace {
std::array<std::wstring, 4> matchTypeXmlNames =
	{ L"All", L"Any", L"None", L"Not all" };
}

CFilterCondition::CFilterCondition()
{
	type = filter_name;
	condition = 0;
	matchCase = true;
	value = 0;
}

bool CFilter::HasConditionOfType(t_filterType type) const
{
	for (std::vector<CFilterCondition>::const_iterator iter = filters.begin(); iter != filters.end(); ++iter)
	{
		if (iter->type == type)
			return true;
	}

	return false;
}

bool CFilter::IsLocalFilter() const
{
	 return HasConditionOfType(filter_attributes) || HasConditionOfType(filter_permissions);
}

CFilterDialog::CFilterDialog()
	: m_shiftClick()
	, m_pMainFrame()
	, m_filters(m_globalFilters)
	, m_filterSets(m_globalFilterSets)
	, m_currentFilterSet(m_globalCurrentFilterSet)
{
}

bool CFilterDialog::Create(CMainFrame* parent)
{
	m_pMainFrame = parent;

	if (!Load(parent, _T("ID_FILTER")))
		return false;

	XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
	XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);
	XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
	XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);

	DisplayFilters();

	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	wxString name = _("Custom filter set");
	pChoice->Append(_T("<") + name + _T(">"));
	for (unsigned int i = 1; i < m_filterSets.size(); i++)
		pChoice->Append(m_filterSets[i].name);
	pChoice->SetSelection(m_currentFilterSet);
	SetCtrlState();

	GetSizer()->Fit(this);

	return true;
}

void CFilterDialog::OnOkOrApply(wxCommandEvent& event)
{
	m_globalFilters = m_filters;
	CompileRegexes();
	m_globalFilterSets = m_filterSets;
	m_globalCurrentFilterSet = m_currentFilterSet;

	SaveFilters();
	m_filters_disabled = false;

	CContextManager::Get()->NotifyAllHandlers(STATECHANGE_APPLYFILTER);

	if (event.GetId() == wxID_OK) {
		EndModal(wxID_OK);
	}
}

void CFilterDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

void CFilterDialog::OnEdit(wxCommandEvent&)
{
	CFilterEditDialog dlg;
	if (!dlg.Create(this, m_filters, m_filterSets))
		return;

	if (dlg.ShowModal() != wxID_OK)
		return;

	m_filters = dlg.GetFilters();
	m_filterSets = dlg.GetFilterSets();
	CompileRegexes();

	DisplayFilters();
}

void CFilterDialog::DisplayFilters()
{
	wxCheckListBox* pLocalFilters = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemoteFilters = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	pLocalFilters->Clear();
	pRemoteFilters->Clear();

	for (unsigned int i = 0; i < m_filters.size(); ++i) {
		const CFilter& filter = m_filters[i];

		const bool localOnly = filter.IsLocalFilter();

		pLocalFilters->Append(filter.name);
		pRemoteFilters->Append(filter.name);

		pLocalFilters->Check(i, m_filterSets[m_currentFilterSet].local[i]);
		pRemoteFilters->Check(i, localOnly ? false : m_filterSets[m_currentFilterSet].remote[i]);
	}
}

void CFilterDialog::OnMouseEvent(wxMouseEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnKeyEvent(wxKeyEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnFilterSelect(wxCommandEvent& event)
{
	wxCheckListBox* pLocal = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemote = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	int item = event.GetSelection();

	const CFilter& filter = m_filters[item];
	const bool localOnly = filter.IsLocalFilter();
	if (localOnly && event.GetEventObject() != pLocal) {
		pRemote->Check(item, false);
		wxMessageBoxEx(_("Selected filter only works for local files."), _("Cannot select filter"), wxICON_INFORMATION);
		return;
	}


	if (m_shiftClick) {
		if (event.GetEventObject() == pLocal) {
			if (!localOnly)
				pRemote->Check(item, pLocal->IsChecked(event.GetSelection()));
		}
		else
			pLocal->Check(item, pRemote->IsChecked(event.GetSelection()));
	}

	if (m_currentFilterSet) {
		m_filterSets[0] = m_filterSets[m_currentFilterSet];
		m_currentFilterSet = 0;
		wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
		pChoice->SetSelection(0);
	}

	bool localChecked = pLocal->IsChecked(event.GetSelection());
	bool remoteChecked = pRemote->IsChecked(event.GetSelection());
	m_filterSets[0].local[item] = localChecked;
	m_filterSets[0].remote[item] = remoteChecked;
}

void CFilterDialog::OnSaveAs(wxCommandEvent&)
{
	CInputDialog dlg;
	dlg.Create(this, _("Enter name for filterset"), _("Please enter a unique name for this filter set"));
	if (dlg.ShowModal() != wxID_OK)
		return;

	wxString name = dlg.GetValue();
	if (name.empty()) {
		wxMessageBoxEx(_("No name for the filterset given."), _("Cannot save filterset"), wxICON_INFORMATION);
		return;
	}
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);

	CFilterSet set;
	int old_pos = pChoice->GetSelection();
	if (old_pos > 0)
		set = m_filterSets[old_pos];
	else
		set = m_filterSets[0];

	int pos = pChoice->FindString(name);
	if (pos != wxNOT_FOUND) {
		if (wxMessageBoxEx(_("Given filterset name already exists, overwrite filter set?"), _("Filter set already exists"), wxICON_QUESTION | wxYES_NO) != wxYES)
			return;
	}

	if (pos == wxNOT_FOUND) {
		pos = m_filterSets.size();
		m_filterSets.push_back(set);
		pChoice->Append(name);
	}
	else
		m_filterSets[pos] = set;

	m_filterSets[pos].name = name;

	pChoice->SetSelection(pos);
	m_currentFilterSet = pos;

	SetCtrlState();

	GetSizer()->Fit(this);
}

void CFilterDialog::OnRename(wxCommandEvent&)
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	int old_pos = pChoice->GetSelection();
	if (old_pos == -1)
		return;

	if (!old_pos) {
		wxMessageBoxEx(_("This filter set cannot be renamed."));
		return;
	}

	CInputDialog dlg;

	wxString msg = wxString::Format(_("Please enter a new name for the filter set \"%s\""), pChoice->GetStringSelection());

	dlg.Create(this, _("Enter new name for filterset"), msg);
	if (dlg.ShowModal() != wxID_OK)
		return;

	wxString name = dlg.GetValue();

	if (name == pChoice->GetStringSelection()) {
		// Nothing changed
		return;
	}

	if (name.empty()) {
		wxMessageBoxEx(_("No name for the filterset given."), _("Cannot save filterset"), wxICON_INFORMATION);
		return;
	}

	int pos = pChoice->FindString(name);
	if (pos != wxNOT_FOUND) {
		if (wxMessageBoxEx(_("Given filterset name already exists, overwrite filter set?"), _("Filter set already exists"), wxICON_QUESTION | wxYES_NO) != wxYES)
			return;
	}

	// Remove old entry
	pChoice->Delete(old_pos);
	CFilterSet set = m_filterSets[old_pos];
	m_filterSets.erase(m_filterSets.begin() + old_pos);

	pos = pChoice->FindString(name);
	if (pos == wxNOT_FOUND) {
		pos = m_filterSets.size();
		m_filterSets.push_back(set);
		pChoice->Append(name);
	}
	else
		m_filterSets[pos] = set;

	m_filterSets[pos].name = name;

	pChoice->SetSelection(pos);
	m_currentFilterSet = pos;

	GetSizer()->Fit(this);
}

void CFilterDialog::OnDeleteSet(wxCommandEvent&)
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	int pos = pChoice->GetSelection();
	if (pos == -1)
		return;

	if (!pos) {
		wxMessageBoxEx(_("This filter set cannot be removed."));
		return;
	}

	m_filterSets[0] = m_filterSets[pos];

	pChoice->Delete(pos);
	m_filterSets.erase(m_filterSets.begin() + pos);
	wxASSERT(!m_filterSets.empty());

	pChoice->SetSelection(0);
	m_currentFilterSet = 0;

	SetCtrlState();
}

void CFilterDialog::OnSetSelect(wxCommandEvent& event)
{
	m_currentFilterSet = event.GetSelection();
	DisplayFilters();
	SetCtrlState();
}

void CFilterDialog::OnChangeAll(wxCommandEvent& event)
{
	bool check = true;
	if (event.GetId() == XRCID("ID_LOCAL_DISABLEALL") || event.GetId() == XRCID("ID_REMOTE_DISABLEALL"))
		check = false;

	bool local;
	std::vector<bool>* pValues;
	wxCheckListBox* pListBox;
	if (event.GetId() == XRCID("ID_LOCAL_ENABLEALL") || event.GetId() == XRCID("ID_LOCAL_DISABLEALL")) {
		pListBox = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
		pValues = &m_filterSets[0].local;
		local = true;
	}
	else {
		pListBox = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);
		pValues = &m_filterSets[0].remote;
		local = false;
	}

	if (m_currentFilterSet) {
		m_filterSets[0] = m_filterSets[m_currentFilterSet];
		m_currentFilterSet = 0;
		wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
		pChoice->SetSelection(0);
	}

	for (size_t i = 0; i < pListBox->GetCount(); ++i) {
		if (!local && (m_filters[i].IsLocalFilter())) {
			pListBox->Check(i, false);
			(*pValues)[i] = false;
		}
		else {
			pListBox->Check(i, check);
			(*pValues)[i] = check;
		}
	}
}

void CFilterDialog::SetCtrlState()
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);

	int sel = pChoice->GetSelection();
	XRCCTRL(*this, "ID_RENAMESET", wxButton)->Enable(sel > 0);
	XRCCTRL(*this, "ID_DELETESET", wxButton)->Enable(sel > 0);
}

CFilterManager::CFilterManager()
{
	LoadFilters();
}

bool CFilterManager::HasActiveFilters(bool ignore_disabled)
{
	if (!m_loaded) {
		LoadFilters();
	}

	if (m_globalFilterSets.empty()) {
		return false;
	}

	wxASSERT(m_globalCurrentFilterSet < m_globalFilterSets.size());

	if (m_filters_disabled && !ignore_disabled) {
		return false;
	}

	const CFilterSet& set = m_globalFilterSets[m_globalCurrentFilterSet];
	for (unsigned int i = 0; i < m_globalFilters.size(); ++i) {
		if (set.local[i]) {
			return true;
		}

		if (set.remote[i]) {
			return true;
		}
	}

	return false;
}

bool CFilterManager::HasSameLocalAndRemoteFilters() const
{
	CFilterSet const& set = m_globalFilterSets[m_globalCurrentFilterSet];
	return set.local == set.remote;

	return true;
}

bool CFilterManager::FilenameFiltered(std::wstring const& name, const wxString& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const
{
	if (m_filters_disabled) {
		return false;
	}

	wxASSERT(m_globalCurrentFilterSet < m_globalFilterSets.size());

	CFilterSet const& set = m_globalFilterSets[m_globalCurrentFilterSet];
	auto const& active = local ? set.local : set.remote;

	// Check active filters
	for (unsigned int i = 0; i < m_globalFilters.size(); ++i) {
		if (active[i]) {
			if (FilenameFilteredByFilter(m_globalFilters[i], name, path, dir, size, attributes, date)) {
				return true;
			}
		}
	}

	return false;
}

bool CFilterManager::FilenameFiltered(std::vector<CFilter> const& filters, std::wstring const& name, const wxString& path, bool dir, int64_t size, int attributes, fz::datetime const& date) const
{
	for (auto const& filter : filters) {
		if (FilenameFilteredByFilter(filter, name, path, dir, size, attributes, date)) {
			return true;
		}
	}

	return false;
}

static bool StringMatch(const wxString& subject, const wxString& filter, int condition, bool matchCase, std::shared_ptr<const std::wregex> const& pRegEx)
{
	bool match = false;

	switch (condition)
	{
	case 0:
		if (matchCase) {
			if (subject.Contains(filter))
				match = true;
		}
		else {
			if (subject.Lower().Contains(filter.Lower()))
				match = true;
		}
		break;
	case 1:
		if (matchCase) {
			if (subject == filter)
				match = true;
		}
		else {
			if (!subject.CmpNoCase(filter))
				match = true;
		}
		break;
	case 2:
		{
			const wxString& left = subject.Left(filter.Len());
			if (matchCase) {
				if (left == filter)
					match = true;
			}
			else {
				if (!left.CmpNoCase(filter))
					match = true;
			}
		}
		break;
	case 3:
		{
			const wxString& right = subject.Right(filter.Len());
			if (matchCase) {
				if (right == filter)
					match = true;
			}
			else {
				if (!right.CmpNoCase(filter))
					match = true;
			}
		}
		break;
	case 4:
		wxASSERT(pRegEx);
		if (pRegEx && std::regex_search(subject.ToStdWstring(), *pRegEx)) {
			match = true;
		}
		break;
	case 5:
		if (matchCase) {
			if (!subject.Contains(filter))
				match = true;
		}
		else {
			if (!subject.Lower().Contains(filter.Lower()))
				match = true;
		}
		break;
	}

	return match;
}

bool CFilterManager::FilenameFilteredByFilter(CFilter const& filter, std::wstring const& name, const wxString& path, bool dir, int64_t size, int attributes, fz::datetime const& date)
{
	if (dir && !filter.filterDirs) {
		return false;
	}
	else if (!dir && !filter.filterFiles) {
		return false;
	}

	for (auto const& condition : filter.filters) {
		bool match = false;

		switch (condition.type)
		{
		case filter_name:
			match = StringMatch(name, condition.strValue, condition.condition, filter.matchCase, condition.pRegEx);
			break;
		case filter_path:
			match = StringMatch(path, condition.strValue, condition.condition, filter.matchCase, condition.pRegEx);
			break;
		case filter_size:
			if (size == -1) {
				continue;
			}
			switch (condition.condition)
			{
			case 0:
				if (size > condition.value)
					match = true;
				break;
			case 1:
				if (size == condition.value)
					match = true;
				break;
			case 2:
				if (size != condition.value)
					match = true;
				break;
			case 3:
				if (size < condition.value)
					match = true;
				break;
			}
			break;
		case filter_attributes:
#ifndef __WXMSW__
			continue;
#else
			if (!attributes)
				continue;

			{
				int flag = 0;
				switch (condition.condition)
				{
				case 0:
					flag = FILE_ATTRIBUTE_ARCHIVE;
					break;
				case 1:
					flag = FILE_ATTRIBUTE_COMPRESSED;
					break;
				case 2:
					flag = FILE_ATTRIBUTE_ENCRYPTED;
					break;
				case 3:
					flag = FILE_ATTRIBUTE_HIDDEN;
					break;
				case 4:
					flag = FILE_ATTRIBUTE_READONLY;
					break;
				case 5:
					flag = FILE_ATTRIBUTE_SYSTEM;
					break;
				}

				int set = (flag & attributes) ? 1 : 0;
				if (set == condition.value)
					match = true;
			}
#endif //__WXMSW__
			break;
		case filter_permissions:
#ifdef __WXMSW__
			continue;
#else
			if (attributes == -1)
				continue;

			{
				int flag = 0;
				switch (condition.condition)
				{
				case 0:
					flag = S_IRUSR;
					break;
				case 1:
					flag = S_IWUSR;
					break;
				case 2:
					flag = S_IXUSR;
					break;
				case 3:
					flag = S_IRGRP;
					break;
				case 4:
					flag = S_IWGRP;
					break;
				case 5:
					flag = S_IXGRP;
					break;
				case 6:
					flag = S_IROTH;
					break;
				case 7:
					flag = S_IWOTH;
					break;
				case 8:
					flag = S_IXOTH;
					break;
				}

				int set = (flag & attributes) ? 1 : 0;
				if (set == condition.value)
					match = true;
			}
#endif //__WXMSW__
			break;
		case filter_date:
			if (!date.empty()) {
				int cmp = date.compare( condition.date );
				switch (condition.condition)
				{
				case 0: // Before
					match = cmp < 0;
					break;
				case 1: // Equals
					match = cmp == 0;
					break;
				case 2: // Not equals
					match = cmp != 0;
					break;
				case 3: // After
					match = cmp > 0;
					break;
				}
			}
			break;
		default:
			wxFAIL_MSG(_T("Unhandled filter type"));
			break;
		}
		if (match) {
			if (filter.matchType == CFilter::any) {
				return true;
			}
			else if (filter.matchType == CFilter::none) {
				return false;
			}
		}
		else {
			if (filter.matchType == CFilter::all) {
				return false;
			}
			else if (filter.matchType == CFilter::not_all) {
				return true;
			}
		}
	}

	if (filter.matchType == CFilter::not_all) {
		return false;
	}

	if (filter.matchType != CFilter::any || filter.filters.empty()) {
		return true;
	}

	return false;
}

bool CFilterManager::CompileRegexes(std::vector<CFilter>& filters)
{
	bool ret = true;
	for (auto & filter : filters) {
		ret &= CompileRegexes(filter);
	}
	return ret;
}

bool CFilterManager::CompileRegexes(CFilter& filter)
{
	bool ret = true;
	for (auto & condition : filter.filters) {
		if ((condition.type == filter_name || condition.type == filter_path) && condition.condition == 4) {
			try {
				condition.pRegEx = std::make_shared<std::wregex>(condition.strValue.ToStdWstring());
			}
			catch (std::regex_error const&) {
				condition.pRegEx.reset();
				ret = false;
			}
		}
		else {
			condition.pRegEx.reset();
		}
	}

	return ret;
}

bool CFilterManager::CompileRegexes()
{
	for (auto & filter : m_globalFilters) {
		CompileRegexes(filter);
	}
	return true;
}

bool CFilterManager::LoadFilter(pugi::xml_node& element, CFilter& filter)
{
	filter.name = GetTextElement(element, "Name");
	filter.filterFiles = GetTextElement(element, "ApplyToFiles") == _T("1");
	filter.filterDirs = GetTextElement(element, "ApplyToDirs") == _T("1");

	wxString const matchType = GetTextElement(element, "MatchType");
	filter.matchType = CFilter::all;
	for (size_t i = 0; i < matchTypeXmlNames.size(); ++i) {
		if (matchType == matchTypeXmlNames[i]) {
			filter.matchType = static_cast<CFilter::t_matchType>(i);
		}
	}
	filter.matchCase = GetTextElement(element, "MatchCase") == _T("1");

	auto xConditions = element.child("Conditions");
	if (!xConditions)
		return false;

	for (auto xCondition = xConditions.child("Condition"); xCondition; xCondition = xCondition.next_sibling("Condition")) {
		CFilterCondition condition;
		int const type = GetTextElementInt(xCondition, "Type", 0);
		switch (type) {
		case 0:
			condition.type = filter_name;
			break;
		case 1:
			condition.type = filter_size;
			break;
		case 2:
			condition.type = filter_attributes;
			break;
		case 3:
			condition.type = filter_permissions;
			break;
		case 4:
			condition.type = filter_path;
			break;
		case 5:
			condition.type = filter_date;
			break;
		default:
			continue;
		}
		condition.condition = GetTextElementInt(xCondition, "Condition", 0);
		if (condition.type == filter_size) {
			if (condition.value == 3)
				condition.value = 2;
			else if (condition.value >= 2)
				++condition.value;
		}
		condition.strValue = GetTextElement(xCondition, "Value");
		condition.matchCase = filter.matchCase;
		if (condition.strValue.empty())
			continue;

		if (condition.type == filter_size) {
			unsigned long long tmp;
			condition.strValue.ToULongLong(&tmp);
			condition.value = tmp;
		}
		else if (condition.type == filter_attributes || condition.type == filter_permissions) {
			if (condition.strValue == _T("0"))
				condition.value = 0;
			else
				condition.value = 1;
		}
		else if (condition.type == filter_date) {
			condition.date = fz::datetime(condition.strValue.ToStdWstring(), fz::datetime::local);
			if (condition.date.empty()) {
				continue;
			}
		}

		filter.filters.push_back(condition);
	}

	return true;
}

void CFilterManager::LoadFilters()
{
	if (m_loaded) {
		return;
	}

	m_loaded = true;

	CReentrantInterProcessMutexLocker mutex(MUTEX_FILTERS);

	std::wstring file(wxGetApp().GetSettingsFile(_T("filters")));
	if (fz::local_filesys::get_size(fz::to_native(file)) < 1) {
		file = wxGetApp().GetResourceDir().GetPath() + _T("defaultfilters.xml");
	}

	CXmlFile xml(file);
	auto element = xml.Load();
	if (!element) {
		wxString msg = xml.GetError() + _T("\n\n") + _("Any changes made to the filters will not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	LoadFilters(element);
}

void CFilterManager::Import(pugi::xml_node& element)
{
	m_globalFilters.clear();
	m_globalFilterSets.clear();
	m_globalCurrentFilterSet = 0;
	m_filters_disabled = false;

	CReentrantInterProcessMutexLocker mutex(MUTEX_FILTERS);

	LoadFilters(element);
	SaveFilters();

	CContextManager::Get()->NotifyAllHandlers(STATECHANGE_APPLYFILTER);
}

void CFilterManager::LoadFilters(pugi::xml_node& element)
{
	auto xFilters = element.child("Filters");
	if (!xFilters) {
		return;
	}

	auto xFilter = xFilters.child("Filter");
	while (xFilter) {
		CFilter filter;

		bool loaded = LoadFilter(xFilter, filter);

		if (loaded && !filter.name.empty() && !filter.filters.empty()) {
			m_globalFilters.push_back(filter);
		}

		xFilter = xFilter.next_sibling("Filter");
	}

	CompileRegexes();
	auto xSets = element.child("Sets");
	if (xSets) {
		for (auto xSet = xSets.child("Set"); xSet; xSet = xSet.next_sibling("Set")) {
			CFilterSet set;
			auto xItem = xSet.child("Item");
			while (xItem) {
				wxString local = GetTextElement(xItem, "Local");
				wxString remote = GetTextElement(xItem, "Remote");
				set.local.push_back(local == _T("1") ? true : false);
				set.remote.push_back(remote == _T("1") ? true : false);

				xItem = xItem.next_sibling("Item");
			}

			if (!m_globalFilterSets.empty()) {
				set.name = GetTextElement(xSet, "Name");
				if (set.name.empty()) {
					continue;
				}
			}

			if (set.local.size() == m_globalFilters.size()) {
				m_globalFilterSets.push_back(set);
			}
		}

		int value = GetAttributeInt(xSets, "Current");
		if (value >= 0 && static_cast<size_t>(value) < m_globalFilterSets.size()) {
			m_globalCurrentFilterSet = value;
		}
	}
	if (m_globalFilterSets.empty()) {
		CFilterSet set;
		set.local.resize(m_globalFilters.size(), false);
		set.remote.resize(m_globalFilters.size(), false);

		m_globalFilterSets.push_back(set);
	}
}

void CFilterManager::SaveFilters()
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_FILTERS);

	CXmlFile xml(wxGetApp().GetSettingsFile(_T("filters")));
	auto element = xml.Load();
	if (!element) {
		wxString msg = xml.GetError() + _T("\n\n") + _("Any changes made to the filters could not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	auto xFilters = element.child("Filters");
	while (xFilters) {
		element.remove_child(xFilters);
		xFilters = element.child("Filters");
	}

	xFilters = element.append_child("Filters");

	for (auto const& filter : m_globalFilters) {
		auto xFilter = xFilters.append_child("Filter");
		SaveFilter(xFilter, filter);
	}

	auto xSets = element.child("Sets");
	while (xSets) {
		element.remove_child(xSets);
		xSets = element.child("Sets");
	}

	xSets = element.append_child("Sets");
	SetAttributeInt(xSets, "Current", m_globalCurrentFilterSet);

	for (auto const& set : m_globalFilterSets) {
		auto xSet = xSets.append_child("Set");

		if (!set.name.empty()) {
			AddTextElement(xSet, "Name", set.name.ToStdWstring());
		}

		for (unsigned int i = 0; i < set.local.size(); ++i) {
			auto xItem = xSet.append_child("Item");
			AddTextElement(xItem, "Local", set.local[i] ? _T("1") : _T("0"));
			AddTextElement(xItem, "Remote", set.remote[i] ? _T("1") : _T("0"));
		}
	}

	xml.Save(true);
}

void CFilterManager::SaveFilter(pugi::xml_node& element, const CFilter& filter)
{
	AddTextElement(element, "Name", filter.name.ToStdWstring());
	AddTextElement(element, "ApplyToFiles", filter.filterFiles ? _T("1") : _T("0"));
	AddTextElement(element, "ApplyToDirs", filter.filterDirs ? _T("1") : _T("0"));
	AddTextElement(element, "MatchType", matchTypeXmlNames[filter.matchType]);
	AddTextElement(element, "MatchCase", filter.matchCase ? _T("1") : _T("0"));

	auto xConditions = element.append_child("Conditions");
	for (std::vector<CFilterCondition>::const_iterator conditionIter = filter.filters.begin(); conditionIter != filter.filters.end(); ++conditionIter) {
		const CFilterCondition& condition = *conditionIter;

		int type;
		switch (condition.type)
		{
		case filter_name:
			type = 0;
			break;
		case filter_size:
			type = 1;
			break;
		case filter_attributes:
			type = 2;
			break;
		case filter_permissions:
			type = 3;
			break;
		case filter_path:
			type = 4;
			break;
		case filter_date:
			type = 5;
			break;
		default:
			wxFAIL_MSG(_T("Unhandled filter type"));
			continue;
		}

		auto xCondition = xConditions.append_child("Condition");
		AddTextElement(xCondition, "Type", type);

		if (condition.type == filter_size) {
			// Backwards compatibility sucks
			int v = condition.condition;
			if (v == 2) {
				v = 3;
			}
			else if (v > 2) {
				--v;
			}
			AddTextElement(xCondition, "Condition", v);
		}
		else {
			AddTextElement(xCondition, "Condition", condition.condition);
		}
		AddTextElement(xCondition, "Value", condition.strValue.ToStdWstring());
	}
}

void CFilterManager::ToggleFilters()
{
	if (m_filters_disabled) {
		m_filters_disabled = false;
		return;
	}

	if (HasActiveFilters(true)) {
		m_filters_disabled = true;
	}
}

ActiveFilters CFilterManager::GetActiveFilters()
{
	ActiveFilters filters;

	if (m_filters_disabled) {
		return filters;
	}

	const CFilterSet& set = m_globalFilterSets[m_globalCurrentFilterSet];

	// Check active filters
	for (unsigned int i = 0; i < m_globalFilters.size(); ++i) {
		if (set.local[i]) {
			filters.first.push_back(m_globalFilters[i]);
		}
		if (set.remote[i]) {
			filters.second.push_back(m_globalFilters[i]);
		}
	}

	return filters;
}
