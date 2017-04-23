#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_FTP_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_FTP_HEADER

class COptionsPageConnectionFTP final : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_CONNECTION_FTP"); }
	virtual bool LoadPage();
	virtual bool SavePage();
};

#endif
