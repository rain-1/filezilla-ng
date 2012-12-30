#include <filezilla.h>

#if FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_updatecheck.h"
#include "../updatewizard.h"

BEGIN_EVENT_TABLE(COptionsPageUpdateCheck, COptionsPage)
EVT_BUTTON(XRCID("ID_RUNUPDATECHECK"), COptionsPageUpdateCheck::OnRunUpdateCheck)
END_EVENT_TABLE()

bool COptionsPageUpdateCheck::LoadPage()
{
	bool failure = false;
	int sel;
	if (!m_pOptions->GetOptionVal(OPTION_UPDATECHECK))
		sel = 0;
	else
	{
		int days = m_pOptions->GetOptionVal(OPTION_UPDATECHECK_INTERVAL);
		if (days <= 7)
			sel = 1;
		else if (days <= 14)
			sel = 2;
		else
			sel = 3;
	}
	SetChoice(XRCID("ID_UPDATECHECK"), sel, failure);

	SetChoice(XRCID("ID_UPDATETYPE"), (m_pOptions->GetOptionVal(OPTION_UPDATECHECK_CHECKBETA) != 0) ? 1 : 0, failure);

	return !failure;
}

bool COptionsPageUpdateCheck::SavePage()
{
	int sel = GetChoice(XRCID("ID_UPDATECHECK"));
	m_pOptions->SetOption(OPTION_UPDATECHECK, (sel > 0) ? 1 : 0);
	int days = 0;
	switch (sel)
	{
	case 1:
		days = 7;
		break;
	case 2:
		days = 14;
		break;
	case 3:
		days = 30;
		break;
	default:
		break;
	}
	m_pOptions->SetOption(OPTION_UPDATECHECK_INTERVAL, days);

	int type = GetChoice(XRCID("ID_UPDATETYPE"));
	m_pOptions->SetOption(OPTION_UPDATECHECK_CHECKBETA, (type > 0) ? 1 : 0);

	return true;
}

void COptionsPageUpdateCheck::OnRunUpdateCheck(wxCommandEvent &event)
{
	CUpdateWizard dlg(this);
	if (!dlg.Load())
		return;

	dlg.Run();
}

#endif //FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK
