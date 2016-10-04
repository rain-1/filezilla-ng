#ifndef FILEZILLA_FZPUTTYGEN_INTERFACE_HEADER
#define FILEZILLA_FZPUTTYGEN_INTERFACE_HEADER

namespace fz {
class process;
}

class CFZPuttyGenInterface final
{
public:
	CFZPuttyGenInterface(wxWindow* parent);
	virtual ~CFZPuttyGenInterface();
	bool LoadKeyFile(std::wstring& keyFile, bool silent, std::wstring& comment, std::wstring& data);

	bool ProcessFailed() const;
protected:
	// return -1 on error
	int NeedsConversion(std::wstring const& keyFile, bool silent);

	// return -1 on error
	int IsKeyFileEncrypted();

	std::unique_ptr<fz::process> m_process;
	bool m_initialized{};
	wxWindow* m_parent;

	enum ReplyCode {
		success,
		error,
		failure
	};

	bool LoadProcess(bool silent);
	bool Send(std::wstring const& cmd);
	ReplyCode GetReply(std::wstring& reply);
};

#endif /* FILEZILLA_FZPUTTYGEN_INTERFACE_HEADER */
