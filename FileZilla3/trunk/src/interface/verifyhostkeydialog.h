#ifndef FILEZILLAINTERFACE_VERIFYHOSTKEYDIALOG_HEADER
#define FILEZILLAINTERFACE_VERIFYHOSTKEYDIALOG_HEADER

/* Full handling is done inside fzsftp, this class is just to display the
 * dialog and for temporary session trust, lost on restart of FileZilla.
 */
class CVerifyHostkeyDialog final
{
public:
	static bool IsTrusted(CHostKeyNotification const& pNotification);
	static void ShowVerificationDialog(wxWindow* parent, CHostKeyNotification& pNotification);

protected:
	struct t_keyData
	{
		std::wstring host;
		std::wstring fingerprint;
	};
	static std::vector<t_keyData> m_sessionTrustedKeys;
};

#endif
