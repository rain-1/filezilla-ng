#ifndef __LOGINMANAGER_H__
#define __LOGINMANAGER_H__

// The purpose of this class is to manage some aspects of the login
// behaviour. These are:
// - Password dialog for servers with ASK or INTERACTIVE logontype
// - Storage of passwords for ASK servers for duration of current session

class CLoginManager
{
public:
	static CLoginManager& Get() { return m_theLoginManager; }

	bool GetPassword(CServer& server, bool silent, std::wstring const& name = std::wstring(), std::wstring const& challenge = std::wstring(), bool canRemember = true);

	void CachedPasswordFailed(const CServer& server, std::wstring const& challenge = std::wstring());

	void RememberPassword(CServer & server, std::wstring const& challenge = std::wstring());

protected:
	bool DisplayDialog(CServer& server, std::wstring const& name, std::wstring const& challenge, bool canRemember);

	static CLoginManager m_theLoginManager;

	// Session password cache for Ask-type servers
	struct t_passwordcache
	{
		std::wstring host;
		unsigned int port;
		std::wstring user;
		std::wstring password;
		std::wstring challenge;
	};

	std::list<t_passwordcache>::iterator FindItem(CServer const& server, std::wstring const& challenge);

	std::list<t_passwordcache> m_passwordCache;
};

#endif //__LOGINMANAGER_H__
