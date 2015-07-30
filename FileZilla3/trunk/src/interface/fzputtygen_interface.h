#ifndef FILEZILLA_FZPUTTYGEN_INTERFACE_HEADER
#define FILEZILLA_FZPUTTYGEN_INTERFACE_HEADER

#include <wx/process.h>

class CFZPuttyGenInterface
{
public:
	CFZPuttyGenInterface(const wxString& executable, wxWindow* parent);
	virtual ~CFZPuttyGenInterface();
	bool LoadKeyFile(wxString& keyFile, bool silent, wxString& comment, wxString& data);
	bool IsKeyFileValid(wxString keyFile, bool silent);
	bool IsKeyFileEncrypted(wxString keyFile, bool silent);
	void EndProcess();
	void DeleteProcess();
	bool IsProcessCreated();
	bool IsProcessStarted();

protected:
	wxProcess* m_pProcess;
	bool m_initialized;
	wxString m_executable;
	wxWindow* m_parent;

	enum ReplyCode
	{
		success,
		error,
		failure
	};

	bool LoadProcess();
	bool Send(const wxString& cmd);
	ReplyCode GetReply(wxString& reply);
};

#endif /* FILEZILLA_FZPUTTYGEN_INTERFACE_HEADER */
