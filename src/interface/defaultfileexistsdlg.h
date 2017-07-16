#ifndef FILEZILLA_INTERFACE_DEFAULTFILEEXISTSDLG_HEADER
#define FILEZILLA_INTERFACE_DEFAULTFILEEXISTSDLG_HEADER

#include "dialogex.h"

class CDefaultFileExistsDlg final : protected wxDialogEx
{
public:
	bool Load(wxWindow *parent, bool fromQueue);

	static CFileExistsNotification::OverwriteAction GetDefault(bool download);
	static void SetDefault(bool download, CFileExistsNotification::OverwriteAction action);

	bool Run(CFileExistsNotification::OverwriteAction *downloadAction = 0, CFileExistsNotification::OverwriteAction *uploadAction = 0);

protected:
	void SelectDefaults(CFileExistsNotification::OverwriteAction* downloadAction, CFileExistsNotification::OverwriteAction* uploadAction);

	static CFileExistsNotification::OverwriteAction m_defaults[2];
};

#endif
