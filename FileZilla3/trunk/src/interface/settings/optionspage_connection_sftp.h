#ifndef __OPTIONSPAGE_CONNECTION_SFTP_H__
#define __OPTIONSPAGE_CONNECTION_SFTP_H__

#include <wx/process.h>
#include "../fzputtygen_interface.h"

class COptionsPageConnectionSFTP : public COptionsPage
{
public:
	COptionsPageConnectionSFTP();
	virtual ~COptionsPageConnectionSFTP();
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_CONNECTION_SFTP"); }
	virtual bool LoadPage();
	virtual bool SavePage();

protected:
	CFZPuttyGenInterface* m_pFzpg;

	bool AddKey(std::wstring keyFile, bool silent);
	bool KeyFileExists(std::wstring const& keyFile);

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnAdd(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);
	void OnSelChanged(wxListEvent& event);
};

#endif //__OPTIONSPAGE_CONNECTION_SFTP_H__
