#ifndef __DEFAULTFILEEXISTSDLG_H__
#define __DEFAULTFILEEXISTSDLG_H__

#include "dialogex.h"

class CDefaultFileExistsDlg final : protected wxDialogEx
{
public:
	CDefaultFileExistsDlg();

	bool Load(wxWindow *parent, bool fromQueue);

	static CFileExistsNotification::OverwriteAction GetDefault(bool download);
	static void SetDefault(bool download, CFileExistsNotification::OverwriteAction action);

	bool Run(CFileExistsNotification::OverwriteAction *downloadAction = 0, CFileExistsNotification::OverwriteAction *uploadAction = 0);

protected:
	void SelectDefaults(CFileExistsNotification::OverwriteAction* downloadAction, CFileExistsNotification::OverwriteAction* uploadAction);

	static CFileExistsNotification::OverwriteAction m_defaults[2];
};

#endif //__DEFAULTFILEEXISTSDLG_H__
