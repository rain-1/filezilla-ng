#ifndef FILEZILLA_INTERFACE_SFTP_CRYPT_INFO_DLG_HEADER
#define FILEZILLA_INTERFACE_SFTP_CRYPT_INFO_DLG_HEADER

class wxDialogEx;
class CSftpEncryptioInfoDialog
{
public:
	void ShowDialog(CSftpEncryptionNotification* pNotification);

protected:
	void SetLabel(wxDialogEx& dlg, int id, const wxString& text);
};

#endif
