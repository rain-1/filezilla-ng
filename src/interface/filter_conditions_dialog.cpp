#include <filezilla.h>
#include "filter_conditions_dialog.h"
#include "customheightlistctrl.h"
#include "textctrlex.h"

static wxArrayString stringConditionTypes;
static wxArrayString sizeConditionTypes;
static wxArrayString attributeConditionTypes;
static wxArrayString permissionConditionTypes;
static wxArrayString attributeSetTypes;
static wxArrayString dateConditionTypes;

static std::unique_ptr<wxChoice> CreateChoice(wxWindow* parent, const wxArrayString& items, wxSize const& size = wxDefaultSize)
{
#ifdef __WXGTK__
	// Really obscure bug in wxGTK: If creating in a single step,
	// first item in the choice sometimes looks disabled
	// even though it can still be selected and returns to looking
	// normal after hovering mouse over it.
	// This works around it nicely.
	auto ret = std::make_unique<wxChoice>();
	ret->Create(parent, wxID_ANY, wxDefaultPosition, size);
	ret->Append(items);
	ret->InvalidateBestSize();
	ret->SetInitialSize();
	return ret;
#else
	return std::make_unique<wxChoice>(parent, wxID_ANY, wxDefaultPosition, size, items);
#endif
}

CFilterControls::CFilterControls()
{
	sizer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
}

BEGIN_EVENT_TABLE(CFilterConditionsDialog, wxDialogEx)
EVT_BUTTON(wxID_ANY, CFilterConditionsDialog::OnButton)
EVT_CHOICE(wxID_ANY, CFilterConditionsDialog::OnFilterTypeChange)
EVT_LISTBOX(wxID_ANY, CFilterConditionsDialog::OnConditionSelectionChange)
END_EVENT_TABLE()

CFilterConditionsDialog::CFilterConditionsDialog()
{
	m_choiceBoxHeight = 0;
	m_pListCtrl = 0;
	m_has_foreign_type = false;
	m_button_size = wxSize(-1, -1);
}

bool CFilterConditionsDialog::CreateListControl(int conditions)
{
	m_pListCtrl = XRCCTRL(*this, "ID_CONDITIONS", wxCustomHeightListCtrl);
	if (!m_pListCtrl) {
		return false;
	}
	m_pListCtrl->AllowSelection(false);

	CalcMinListWidth();

	if (stringConditionTypes.empty()) {
		stringConditionTypes.Add(_("contains"));
		stringConditionTypes.Add(_("is equal to"));
		stringConditionTypes.Add(_("begins with"));
		stringConditionTypes.Add(_("ends with"));
		stringConditionTypes.Add(_("matches regex"));
		stringConditionTypes.Add(_("does not contain"));

		sizeConditionTypes.Add(_("greater than"));
		sizeConditionTypes.Add(_("equals"));
		sizeConditionTypes.Add(_("does not equal"));
		sizeConditionTypes.Add(_("less than"));

		attributeSetTypes.Add(_("is set"));
		attributeSetTypes.Add(_("is unset"));

		attributeConditionTypes.Add(_("Archive"));
		attributeConditionTypes.Add(_("Compressed"));
		attributeConditionTypes.Add(_("Encrypted"));
		attributeConditionTypes.Add(_("Hidden"));
		attributeConditionTypes.Add(_("Read-only"));
		attributeConditionTypes.Add(_("System"));

		permissionConditionTypes.Add(_("owner readable"));
		permissionConditionTypes.Add(_("owner writeable"));
		permissionConditionTypes.Add(_("owner executable"));
		permissionConditionTypes.Add(_("group readable"));
		permissionConditionTypes.Add(_("group writeable"));
		permissionConditionTypes.Add(_("group executable"));
		permissionConditionTypes.Add(_("world readable"));
		permissionConditionTypes.Add(_("world writeable"));
		permissionConditionTypes.Add(_("world executable"));

		dateConditionTypes.Add(_("before"));
		dateConditionTypes.Add(_("equals"));
		dateConditionTypes.Add(_("does not equal"));
		dateConditionTypes.Add(_("after"));
	}

	if (conditions & filter_name) {
		filterTypes.Add(_("Filename"));
		filter_type_map.push_back(filter_name);
	}
	if (conditions & filter_size) {
		filterTypes.Add(_("Filesize"));
		filter_type_map.push_back(filter_size);
	}
	if (conditions & filter_attributes) {
		filterTypes.Add(_("Attribute"));
		filter_type_map.push_back(filter_attributes);
	}
	if (conditions & filter_permissions) {
		filterTypes.Add(_("Permission"));
		filter_type_map.push_back(filter_permissions);
	}
	if (conditions & filter_path) {
		filterTypes.Add(_("Path"));
		filter_type_map.push_back(filter_path);
	}
	if (conditions & filter_date) {
		filterTypes.Add(_("Date"));
		filter_type_map.push_back(filter_date);
	}

	SetFilterCtrlState(true);

	return true;
}

void CFilterConditionsDialog::CalcMinListWidth()
{
	wxChoice *pType = new wxChoice(m_pListCtrl, wxID_ANY, wxDefaultPosition, wxDefaultSize, filterTypes);
	int requiredWidth = pType->GetBestSize().GetWidth();
	pType->Destroy();

	wxChoice *pStringCondition = new wxChoice(m_pListCtrl, wxID_ANY, wxDefaultPosition, wxDefaultSize, stringConditionTypes);
	wxChoice *pSizeCondition = new wxChoice(m_pListCtrl, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizeConditionTypes);
	wxStaticText *pSizeLabel = new wxStaticText(m_pListCtrl, wxID_ANY, _("bytes"));
	wxChoice *pDateCondition = new wxChoice(m_pListCtrl, wxID_ANY, wxDefaultPosition, wxDefaultSize, dateConditionTypes);

	int w = wxMax(pStringCondition->GetBestSize().GetWidth(), pSizeCondition->GetBestSize().GetWidth() + 5 + pSizeLabel->GetBestSize().GetWidth());
	w = wxMax(w, pDateCondition->GetBestSize().GetWidth());
	requiredWidth += w;

	m_size_label_size = pSizeLabel->GetBestSize();

	pStringCondition->Destroy();
	pSizeCondition->Destroy();
	pSizeLabel->Destroy();
	pDateCondition->Destroy();

	requiredWidth += m_pListCtrl->GetWindowBorderSize().x;
	requiredWidth += 40;
	requiredWidth += 120;
	wxSize minSize = m_pListCtrl->GetMinSize();
	minSize.IncTo(wxSize(requiredWidth, -1));
	m_pListCtrl->SetMinSize(minSize);

	m_lastListSize = m_pListCtrl->GetClientSize();
}

t_filterType CFilterConditionsDialog::GetTypeFromTypeSelection(int selection)
{
	if (selection < 0 || selection >(int)filter_type_map.size()) {
		selection = 0;
	}

	return filter_type_map[selection];
}

void CFilterConditionsDialog::SetSelectionFromType(wxChoice* pChoice, t_filterType type)
{
	for (unsigned int i = 0; i < filter_type_map.size(); ++i) {
		if (filter_type_map[i] == type) {
			pChoice->SetSelection(i);
			return;
		}
	}

	pChoice->SetSelection(0);
}

void CFilterConditionsDialog::OnMore()
{
	CFilterCondition cond;
	m_currentFilter.filters.push_back(cond);

	size_t newRowIndex = m_filterControls.size() - 1;

	m_filterControls.insert(m_filterControls.begin() + newRowIndex, CFilterControls());
	MakeControls(cond, newRowIndex);

	CFilterControls& controls = m_filterControls[newRowIndex];
	if (m_filterControls.back().pRemove) {
		m_filterControls.back().pRemove->MoveAfterInTabOrder(controls.pRemove.get());
	}

	m_pListCtrl->InsertRow(m_filterControls[newRowIndex].sizer.get(), newRowIndex);
}

void CFilterConditionsDialog::OnRemove(size_t item)
{
	if (item + 1 >= m_filterControls.size()) {
		return;
	}

	m_pListCtrl->DeleteRow(item);
	m_filterControls.erase(m_filterControls.begin() + item);
	m_currentFilter.filters.erase(m_currentFilter.filters.begin() + item);

	if (m_currentFilter.filters.empty()) {
		OnMore();
	}
}

void CFilterConditionsDialog::OnFilterTypeChange(wxCommandEvent& event)
{
	size_t item;
	for (item = 0; item < m_filterControls.size(); ++item) {
		if (m_filterControls[item].pType && m_filterControls[item].pType->GetId() == event.GetId()) {
			break;
		}
	}
	if (item == m_filterControls.size()) {
		return;
	}

	CFilterCondition& filter = m_currentFilter.filters[item];

	t_filterType type = GetTypeFromTypeSelection(event.GetSelection());
	if (type == filter.type) {
		return;
	}
	filter.type = type;

	if (filter.type == filter_size && filter.condition > 3) {
		filter.condition = 0;
	}
	else if (filter.type == filter_date && filter.condition > 3) {
		filter.condition = 0;
	}

	UpdateControls(filter, item);
}

void CFilterConditionsDialog::MakeControls(CFilterCondition const& condition, size_t i)
{
	CFilterControls& controls = m_filterControls[i];

	if (!controls.pType) {
		controls.pType = CreateChoice(m_pListCtrl, wxArrayString());
		controls.pType->Set(filterTypes);
		controls.sizer->Add(controls.pType.get(), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	}

	if (!m_choiceBoxHeight) {
		wxSize size = controls.pType->GetSize();
		m_choiceBoxHeight = size.GetHeight();
		m_pListCtrl->SetLineHeight(m_choiceBoxHeight + 6);
	}

	if (!controls.pCondition) {
		controls.pCondition = CreateChoice(m_pListCtrl, wxArrayString());
		controls.sizer->Add(controls.pCondition.get(), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	}

	if (!controls.pValue) {
		controls.pValue = std::make_unique<wxTextCtrlEx>();
		controls.pValue->Create(m_pListCtrl, wxID_ANY, _T(""));
		controls.pValue->Hide();
		controls.sizer->Add(controls.pValue.get(), 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	}

	if (!controls.pSet) {
		controls.pSet = CreateChoice(m_pListCtrl, wxArrayString());
		controls.pSet->Set(attributeSetTypes);
		controls.pSet->Hide();
		controls.sizer->Add(controls.pSet.get(), 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	}

	if (!controls.pLabel) {
		controls.pLabel = std::make_unique<wxStaticText>();
		controls.pLabel->Hide();
		controls.pLabel->Create(m_pListCtrl, wxID_ANY, _("bytes"), wxDefaultPosition, m_size_label_size);
		controls.sizer->Add(controls.pLabel.get(), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
	}

	if (!controls.pRemove) {
		controls.pRemove = std::make_unique<wxButton>(m_pListCtrl, wxID_ANY, _T("-"), wxDefaultPosition, m_button_size, wxBU_EXACTFIT);
		if (m_button_size.x <= 0) {
			m_button_size.x = wxMax(m_choiceBoxHeight, controls.pRemove->GetSize().x);
			m_button_size.y = m_choiceBoxHeight;
			controls.pRemove->SetSize(m_button_size);
		}
		controls.sizer->Add(controls.pRemove.get(), 0, wxALIGN_CENTER_VERTICAL | wxFIXED_MINSIZE | wxLEFT | wxRIGHT, 5);
	}

	UpdateControls(condition, i);
}

void CFilterConditionsDialog::UpdateControls(CFilterCondition const& condition, size_t i)
{
	CFilterControls& controls = m_filterControls[i];

	SetSelectionFromType(controls.pType.get(), condition.type);

	switch (condition.type)
	{
	case filter_name:
	case filter_path:
		controls.pCondition->Set(stringConditionTypes);
		break;
	case filter_size:
		controls.pCondition->Set(sizeConditionTypes);
		break;
	case filter_attributes:
		controls.pCondition->Set(attributeConditionTypes);
		break;
	case filter_permissions:
		controls.pCondition->Set(permissionConditionTypes);
		break;
	case filter_date:
		controls.pCondition->Set(dateConditionTypes);
		break;
	default:
		wxFAIL_MSG(_T("Unhandled condition"));
		return;
	}
	controls.pCondition->Select(condition.condition);

	controls.pValue->SetValue(condition.strValue);
	controls.pSet->Select(condition.strValue != _T("0") ? 0 : 1);

	controls.pValue->Show(condition.type == filter_name || condition.type == filter_size || condition.type == filter_path || condition.type == filter_date);
	controls.pSet->Show(!controls.pValue->IsShown());
	controls.pLabel->Show(condition.type == filter_size);

	controls.sizer->Layout();
}

void CFilterConditionsDialog::DestroyControls()
{
	m_pListCtrl->ClearRows();
	m_filterControls.clear();
}

void CFilterConditionsDialog::EditFilter(CFilter const& filter)
{
	DestroyControls();

	// Create new controls
	m_currentFilter = filter;

	if (m_currentFilter.filters.empty()) {
		m_currentFilter.filters.push_back(CFilterCondition());
	}
	m_filterControls.resize(m_currentFilter.filters.size() + 1);

	for (unsigned int i = 0; i < m_currentFilter.filters.size(); ++i) {
		const CFilterCondition& cond = m_currentFilter.filters[i];

		MakeControls(cond, i);
		m_pListCtrl->InsertRow(m_filterControls[i].sizer.get(), i);
	}

	CFilterControls & controls = m_filterControls.back();
	controls.pRemove = std::make_unique<wxButton>(m_pListCtrl, wxID_ANY, _T("+"), wxDefaultPosition, m_button_size);
	controls.sizer->AddStretchSpacer();
	controls.sizer->Add(controls.pRemove.get(), 0, wxALIGN_CENTER_VERTICAL|wxFIXED_MINSIZE|wxRIGHT, 5);

	m_pListCtrl->InsertRow(controls.sizer.get(), m_filterControls.size() - 1);

	XRCCTRL(*this, "ID_MATCHTYPE", wxChoice)->SetSelection(filter.matchType);

	SetFilterCtrlState(false);
}

CFilter CFilterConditionsDialog::GetFilter()
{
	wxASSERT(m_filterControls.size() >= m_currentFilter.filters.size());

	CFilter filter;
	for (size_t i = 0; i < m_currentFilter.filters.size(); ++i) {
		CFilterControls const& controls = m_filterControls[i];
		if (!controls.pType || !controls.pCondition) {
			continue;
		}
		CFilterCondition condition = m_currentFilter.filters[i];

		condition.type = GetTypeFromTypeSelection(controls.pType->GetSelection());
		condition.condition = controls.pCondition->GetSelection();

		switch (condition.type)
		{
		case filter_name:
		case filter_path:
			if (!controls.pValue || controls.pValue->GetValue().empty()) {
				continue;
			}
			condition.strValue = controls.pValue->GetValue();
			break;
		case filter_size:
			{
				if (!controls.pValue || controls.pValue->GetValue().empty()) {
					continue;
				}
				condition.strValue = controls.pValue->GetValue();
				unsigned long long tmp;
				condition.strValue.ToULongLong(&tmp);
				condition.value = tmp;
			}
			break;
		case filter_attributes:
		case filter_permissions:
			if (!controls.pSet) {
				continue;
			}
			else if (controls.pSet->GetSelection()) {
				condition.strValue = _T("0");
				condition.value = 0;
			}
			else {
				condition.strValue = _T("1");
				condition.value = 1;
			}
			break;
		case filter_date:
			if (!controls.pValue || controls.pValue->GetValue().empty()) {
				continue;
			}
			else {
				condition.strValue = controls.pValue->GetValue();
				condition.date = fz::datetime(condition.strValue.ToStdWstring(), fz::datetime::local);
				if (condition.date.empty()) {
					continue;
				}
			}
			break;
		default:
			wxFAIL_MSG(_T("Unhandled condition"));
			break;
		}

		condition.matchCase = filter.matchCase;

		filter.filters.push_back(condition);
	}

	switch (XRCCTRL(*this, "ID_MATCHTYPE", wxChoice)->GetSelection())
	{
	case 1:
		filter.matchType = CFilter::any;
		break;
	case 2:
		filter.matchType = CFilter::none;
		break;
	case 3:
		filter.matchType = CFilter::not_all;
		break;
	default:
		filter.matchType = CFilter::all;
		break;
	}

	return filter;
}

void CFilterConditionsDialog::ClearFilter()
{
	DestroyControls();
	SetFilterCtrlState(true);
}

void CFilterConditionsDialog::SetFilterCtrlState(bool disable)
{
	m_pListCtrl->Enable(!disable);

	XRCCTRL(*this, "ID_MATCHTYPE", wxChoice)->Enable(!disable);
}

bool CFilterConditionsDialog::ValidateFilter(wxString& error, bool allow_empty)
{
	size_t const size = m_currentFilter.filters.size();
	if (!size) {
		if (allow_empty) {
			return true;
		}

		error = _("Each filter needs at least one condition.");
		return false;
	}

	wxASSERT(m_filterControls.size() >= m_currentFilter.filters.size());

	for (unsigned int i = 0; i < size; ++i) {
		const CFilterControls& controls = m_filterControls[i];
		t_filterType type = GetTypeFromTypeSelection(controls.pType->GetSelection());
		int condition = controls.pCondition ? controls.pCondition->GetSelection() : 0;
		if (!controls.pValue) {
			continue;
		}

		if (type == filter_name || type == filter_path) {
			if (!controls.pValue || controls.pValue->GetValue().empty()) {
				if (allow_empty) {
					continue;
				}

				m_pListCtrl->SelectLine(i);
				controls.pValue->SetFocus();
				error = _("At least one filter condition is incomplete");
				return false;
			}

			if (condition == 4) {
				try {
					std::wregex(controls.pValue->GetValue().ToStdWstring());
				}
				catch (std::regex_error const&) {
					m_pListCtrl->SelectLine(i);
					controls.pValue->SetFocus();
					error = _("Invalid regular expression");
					return false;
				}
			}
		}
		else if (type == filter_size) {
			const wxString v = controls.pValue->GetValue();
			if (v.empty() && allow_empty) {
				continue;
			}

			long long number;
			if (!v.ToLongLong(&number) || number < 0) {
				m_pListCtrl->SelectLine(i);
				controls.pValue->SetFocus();
				error = _("Invalid size in condition");
				return false;
			}
		}
		else if (type == filter_date) {
			wxString const d = controls.pValue->GetValue();
			if (d.empty() && allow_empty) {
				continue;
			}

			fz::datetime date(d.ToStdWstring(), fz::datetime::local);
			if (date.empty()) {
				m_pListCtrl->SelectLine(i);
				controls.pValue->SetFocus();
				error = _("Please enter a date of the form YYYY-MM-DD such as for example 2010-07-18.");
				return false;
			}
		}
	}

	return true;
}

void CFilterConditionsDialog::OnConditionSelectionChange(wxCommandEvent& event)
{
	if (event.GetId() != m_pListCtrl->GetId()) {
		return;
	}
}

void CFilterConditionsDialog::OnButton(wxCommandEvent& event)
{
	for (size_t i = 0; i < m_filterControls.size(); ++i) {
		if (m_filterControls[i].pRemove->GetId() == event.GetId()) {
			if (i + 1 == m_filterControls.size()) {
				OnMore();
			}
			else {
				OnRemove(i);
			}
			return;
		}
	}
	event.Skip();
}
