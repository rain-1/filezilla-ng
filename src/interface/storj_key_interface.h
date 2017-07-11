#ifndef FILEZILLA_STORJ_KEY_INTERFACE_HEADER
#define FILEZILLA_STORJ_KEY_INTERFACE_HEADER

namespace fz {
class process;
}

class CStorjKeyInterface final
{
public:
	CStorjKeyInterface(wxWindow* parent);
	virtual ~CStorjKeyInterface();

	std::wstring GenerateKey();
	bool ValidateKey(std::wstring const& key, bool silent);

	bool ProcessFailed() const;
protected:

	std::unique_ptr<fz::process> m_process;
	bool m_initialized{};
	wxWindow* m_parent;

	enum ReplyCode {
		success = 1,
		error,
	};

	bool LoadProcess(bool silent);
	bool Send(std::wstring const& cmd);
	ReplyCode GetReply(std::wstring& reply);
};

#endif
