#ifndef FILEZILLA_ENGINE_SERVER_HEADER
#define FILEZILLA_ENGINE_SERVER_HEADER

enum ServerProtocol
{
	// Never change any existing values or user's saved sites will become
	// corrupted
	UNKNOWN = -1,
	FTP, // FTP, attempts AUTH TLS
	SFTP,
	HTTP,
	FTPS, // Implicit SSL
	FTPES, // Explicit SSL
	HTTPS,
	INSECURE_FTP, // Insecure, as the name suggests

	S3, // Amazon S3 or compatible

	MAX_VALUE = S3
};

enum ServerType
{
	DEFAULT,
	UNIX,
	VMS,
	DOS, // Backslashes as preferred separator
	MVS,
	VXWORKS,
	ZVM,
	HPNONSTOP,
	DOS_VIRTUAL,
	CYGWIN,
	DOS_FWD_SLASHES, // Forwardslashes as preferred separator

	SERVERTYPE_MAX
};

enum PasvMode
{
	MODE_DEFAULT,
	MODE_ACTIVE,
	MODE_PASSIVE
};

enum class ServerFormat
{
	host_only,
	with_optional_port,
	with_user_and_optional_port,
	url,
	url_with_password
};

enum CharsetEncoding
{
	ENCODING_AUTO,
	ENCODING_UTF8,
	ENCODING_CUSTOM
};

class Credentials;
class CServerPath;
class CServer final
{
public:

	// No error checking is done in the constructors
	CServer() = default;
	CServer(ServerProtocol protocol, ServerType type, std::wstring const& host, unsigned int);

	void clear();

	void SetType(ServerType type);

	ServerProtocol GetProtocol() const;
	ServerType GetType() const;
	std::wstring GetHost() const;
	unsigned int GetPort() const;
	std::wstring GetUser() const;
	int GetTimezoneOffset() const;
	PasvMode GetPasvMode() const;
	int MaximumMultipleConnections() const;
	bool GetBypassProxy() const;

	void SetProtocol(ServerProtocol serverProtocol);
	bool SetHost(std::wstring const& host, unsigned int port);

	void SetUser(std::wstring const& user);

	CServer& operator=(const CServer &op);
	bool operator==(const CServer &op) const;
	bool operator<(const CServer &op) const;
	bool operator!=(const CServer &op) const;

	bool SetTimezoneOffset(int minutes);
	void SetPasvMode(PasvMode pasvMode);
	void MaximumMultipleConnections(int maximum);

	std::wstring Format(ServerFormat formatType) const;
	std::wstring Format(ServerFormat formatType, Credentials const& credentials) const;

	bool SetEncodingType(CharsetEncoding type, std::wstring const& encoding = std::wstring());
	bool SetCustomEncoding(std::wstring const& encoding);
	CharsetEncoding GetEncodingType() const;
	std::wstring GetCustomEncoding() const;

	static unsigned int GetDefaultPort(ServerProtocol protocol);
	static ServerProtocol GetProtocolFromPort(unsigned int port, bool defaultOnly = false);

	static std::wstring GetProtocolName(ServerProtocol protocol);
	static ServerProtocol GetProtocolFromName(std::wstring const& name);

	static ServerProtocol GetProtocolFromPrefix(std::wstring const& prefix);
	static std::wstring GetPrefixFromProtocol(ServerProtocol const protocol);
	static std::vector<ServerProtocol> const& GetDefaultProtocols();

	// Some protocol distinguish between ASCII and binary files for line-ending
	// conversion.
	static bool ProtocolHasDataTypeConcept(ServerProtocol const protocol);

	// These commands will be executed after a successful login.
	std::vector<std::wstring> const& GetPostLoginCommands() const { return m_postLoginCommands; }
	bool SetPostLoginCommands(std::vector<std::wstring> const& postLoginCommands);
	static bool SupportsPostLoginCommands(ServerProtocol const protocol);

	void SetBypassProxy(bool val);

	// Abstract server name.
	// Not compared in ==, < and related operators
	void SetName(std::wstring const& name) { m_name = name; }
	std::wstring GetName() const { return m_name; }

	static std::wstring GetNameFromServerType(ServerType type);
	static ServerType GetServerTypeFromName(std::wstring const& name);

	bool empty() const { return m_host.empty(); }
	explicit operator bool() const { return !empty(); }

protected:
	ServerProtocol m_protocol{UNKNOWN};
	ServerType m_type{DEFAULT};
	std::wstring m_host;
	unsigned int m_port{21};
	std::wstring m_user;
	int m_timezoneOffset{};
	PasvMode m_pasvMode{MODE_DEFAULT};
	int m_maximumMultipleConnections{};
	CharsetEncoding m_encodingType{ENCODING_AUTO};
	std::wstring m_customEncoding;
	std::wstring m_name;

	std::vector<std::wstring> m_postLoginCommands;
	bool m_bypassProxy{};
};


enum class LogonType
{
	anonymous,
	normal,
	ask, // ask should not be sent to the engine, it's intended to be used by the interface
	interactive,
	account,
	key,

	count
};
std::wstring GetNameFromLogonType(LogonType type);
LogonType GetLogonTypeFromName(std::wstring const& name);


class Credentials
{
public:
	bool operator==(Credentials const& rhs) const {
		return
			logonType_ == rhs.logonType_ &&
			password_ == rhs.password_ &&
			account_ == rhs.account_ &&
			keyFile_ == rhs.keyFile_;
	}

	LogonType logonType_{LogonType::anonymous};
	std::wstring password_;
	std::wstring account_;
	std::wstring keyFile_;
};

class ServerWithCredentials final
{
public:
	ServerWithCredentials() = default;

	explicit ServerWithCredentials(CServer const& s, Credentials const& c)
		: server(s)
		, credentials(c)
	{}

	// Return true if URL could be parsed correctly, false otherwise.
	// If parsing fails, pError is filled with the reason and the CServer instance may be left an undefined state.
	bool ParseUrl(std::wstring host, unsigned int port, std::wstring user, std::wstring pass, std::wstring &error, CServerPath &path);
	bool ParseUrl(std::wstring const& host, std::wstring const& port, std::wstring const& user, std::wstring const& pass, std::wstring &error, CServerPath &path);

	std::wstring Format(ServerFormat formatType) const {
		return server.Format(formatType, credentials);
	}

	explicit operator bool() const {
		return static_cast<bool>(server);
	}

	bool operator==(ServerWithCredentials const& rhs) const {
		return server == rhs.server && credentials == rhs.credentials;
	}
	bool operator!=(ServerWithCredentials const& rhs) const {
		return !(*this == rhs);
	}

	CServer server;
	Credentials credentials;
};

#endif
