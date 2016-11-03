#ifndef FILEZILLA_INTERFACE_CONDITIONS_DIALOG_HEADER
#define FILEZILLA_INTERFACE_CONDITIONS_DIALOG_HEADER

#include "dialogex.h"
#include "filter.h"
#include <set>

class CFilterControls final
{
public:
	CFilterControls();

	std::unique_ptr<wxBoxSizer> sizer;
	std::unique_ptr<wxChoice> pType;
	std::unique_ptr<wxChoice> pCondition;
	std::unique_ptr<wxTextCtrl> pValue;
	std::unique_ptr<wxChoice> pSet;
	std::unique_ptr<wxStaticText> pLabel;
	std::unique_ptr<wxButton> pRemove;
};

class wxCustomHeightListCtrl;
class CFilterConditionsDialog : public wxDialogEx
{
public:
	CFilterConditionsDialog();

	// has_foreign_type for attributes on MSW, permissions on *nix
	// has_foreign_type for attributes on *nix, permissions on MSW
	bool CreateListControl(int conditions);

	void EditFilter(CFilter const& filter);
	CFilter GetFilter();
	void ClearFilter();
	bool ValidateFilter(wxString& error, bool allow_empty = false);

private:
	void CalcMinListWidth();

	t_filterType GetTypeFromTypeSelection(int selection);
	void SetSelectionFromType(wxChoice* pChoice, t_filterType);

	void MakeControls(CFilterCondition const& condition, size_t i);
	void UpdateControls(CFilterCondition const& condition, size_t i);

	void DestroyControls();

	void SetFilterCtrlState(bool disable);

	bool m_has_foreign_type;

	wxCustomHeightListCtrl* m_pListCtrl;
	wxSize m_lastListSize;
	int m_choiceBoxHeight;

	std::vector<CFilterControls> m_filterControls;

	CFilter m_currentFilter;

	wxArrayString filterTypes;
	std::vector<t_filterType> filter_type_map;

	wxSize m_button_size;
	wxSize m_size_label_size;

	void OnMore();
	void OnRemove(size_t item);

	DECLARE_EVENT_TABLE()
	void OnButton(wxCommandEvent& event);
	void OnFilterTypeChange(wxCommandEvent& event);
	void OnConditionSelectionChange(wxCommandEvent& event);
};

#endif
