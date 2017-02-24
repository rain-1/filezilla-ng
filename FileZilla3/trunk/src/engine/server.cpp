#include <filezilla.h>

#include "uri.h"

#include <libfilezilla/format.hpp>

#include <assert.h>

struct t_protocolInfo
{
	ServerProtocol const protocol;
	std::wstring const prefix;
	bool alwaysShowPrefix;
	unsigned int defaultPort;
	bool const translateable;
	char const* const name;
	bool supportsPostlogin;
};

static const t_protocolInfo protocolInfos[] = {
	{ FTP,          L"ftp",   false, 21,  true,  fztranslate_mark("FTP - File Transfer Protocol with optional encryption"), true  },
	{ SFTP,         L"sftp",  true,  22,  false, "SFTP - SSH File Transfer Protocol",                                       false },
	{ HTTP,         L"http",  true,  80,  false, "HTTP - Hypertext Transfer Protocol",                                      false },
	{ HTTPS,        L"https", true, 443,  true,  fztranslate_mark("HTTPS - HTTP over TLS"),                                 false },
	{ FTPS,         L"ftps",  true, 990,  true,  fztranslate_mark("FTPS - FTP over implicit TLS"),                          true  },
	{ FTPES,        L"ftpes", true,  21,  true,  fztranslate_mark("FTPES - FTP over explicit TLS"),                         true  },
	{ INSECURE_FTP, L"ftp",   false, 21,  true,  fztranslate_mark("FTP - Insecure File Transfer Protocol"),                 true  },
	{ S3,           L"s3",    true,  80,  false,  "S3 - Amazon Simple Storage Service",	                                    false },
	{ UNKNOWN,      L"",      false, 21,  false, "", false }
};

static std::vector<ServerProtocol> const defaultProtocols = { FTP, SFTP, FTPS, FTPES, INSECURE_FTP };

static char const* const typeNames[SERVERTYPE_MAX] = {
	fztranslate_mark("Default (Autodetect)"),
	"Unix",
	"VMS",
	"DOS with backslash separators",
	"MVS, OS/390, z/OS",
	"VxWorks",
	"z/VM",
	"HP NonStop",
	fztranslate_mark("DOS-like with virtual paths"),
	"Cygwin",
	"DOS with forward-slash separators",
};

static const t_protocolInfo& GetProtocolInfo(ServerProtocol protocol)
{
	unsigned int i = 0;
	for ( ; protocolInfos[i].protocol != UNKNOWN; ++i) {
		if (protocolInfos[i].protocol == protocol) {
			break;
		}
	}
	return protocolInfos[i];
}

bool CServer::ParseUrl(std::wstring const& host, std::wstring const& port, std::wstring const& user, std::wstring const& pass, std::wstring &error, CServerPath &path)
{
	unsigned int nPort = 0;
	if (!port.empty()) {
		nPort = fz::to_integral<unsigned int>(fz::trimmed(port));
		if (port.size() > 5 || !nPort || nPort > 65535) {
			error = _("Invalid port given. The port has to be a value from 1 to 65535.");
			error += L"\n";
			error += _("You can leave the port field empty to use the default port.");
			return false;
		}
	}
	return ParseUrl(host, nPort, user, pass, error, path);
}

bool CServer::ParseUrl(std::wstring host, unsigned int port, std::wstring user, std::wstring pass, std::wstring &error, CServerPath &path)
{
	m_type = DEFAULT;

	if (host.empty()) {
		error = _("No host given, please enter a host.");
		return false;
	}

	size_t pos = host.find(L"://");
	if (pos != std::wstring::npos) {
		std::wstring protocol = fz::str_tolower_ascii(host.substr(0, pos));
		host = host.substr(pos + 3);
		if (protocol.substr(0, 3) == L"fz_") {
			protocol = protocol.substr(3);
		}
		m_protocol = GetProtocolFromPrefix(protocol);
		if (m_protocol == UNKNOWN) {
			error = _("Invalid protocol specified. Valid protocols are:\nftp:// for normal FTP with optional encryption,\nsftp:// for SSH file transfer protocol,\nftps:// for FTP over TLS (implicit) and\nftpes:// for FTP over TLS (explicit).");
			return false;
		}
	}

	pos = host.find('@');
	if (pos != std::wstring::npos) {
		// Check if it's something like
		//   user@name:password@host:port/path
		// => If there are multiple at signs, username/port ends at last at before
		// the first slash. (Since host and port never contain any at sign)

		size_t slash = host.find('/', pos + 1);

		size_t next_at = host.find('@', pos + 1);
		while (next_at != std::wstring::npos) {
			if (slash != std::wstring::npos  && next_at > slash) {
				break;
			}

			pos = next_at;
			next_at = host.find('@', next_at + 1);
		}

		user = host.substr(0, pos);
		host = host.substr(pos + 1);

		// Extract password (if any) from username
		pos = user.find(':');
		if (pos != std::wstring::npos) {
			pass = user.substr(pos + 1);
			user = user.substr(0, pos);
		}

		// Remove leading and trailing whitespace
		fz::trim(user);

		if (user.empty()) {
			error = _("Invalid username given.");
			return false;
		}
	}
	else {
		// Remove leading and trailing whitespace
		fz::trim(user);

		if (user.empty() && m_logonType != ASK && m_logonType != INTERACTIVE) {
			user = L"anonymous";
			pass = L"anonymous@example.com";
		}
	}

	pos = host.find('/');
	if (pos != std::wstring::npos) {
		path = CServerPath(host.substr(pos));
		host = host.substr(0, pos);
	}

	if (!host.empty() && host[0] == '[') {
		// Probably IPv6 address
		pos = host.find(']');
		if (pos == std::wstring::npos) {
			error = _("Host starts with '[' but no closing bracket found.");
			return false;
		}
		if (pos < host.size() - 1 ) {
			if (host[pos + 1] != ':') {
				error = _("Invalid host, after closing bracket only colon and port may follow.");
				return false;
			}
			++pos;
		}
		else
			pos = std::wstring::npos;
	}
	else {
		pos = host.find(':');
	}
	if (pos != std::wstring::npos) {
		if (!pos) {
			error = _("No host given, please enter a host.");
			return false;
		}

		port = fz::to_integral<unsigned int>(host.substr(pos + 1));
		host = host.substr(0, pos);
	}
	else {
		if (!port) {
			port = GetDefaultPort(m_protocol);
		}
	}

	if (port < 1 || port > 65535) {
		error = _("Invalid port given. The port has to be a value from 1 to 65535.");
		return false;
	}

	fz::trim(host);

	if (host.empty()) {
		error = _("No host given, please enter a host.");
		return false;
	}

	m_host = host;

	if (m_host[0] == '[') {
		m_host = m_host.substr(1, m_host.size() - 2);
	}

	m_port = port;
	m_user = user;
	m_pass = pass;
	m_account.clear();
	if (m_logonType != ASK && m_logonType != INTERACTIVE) {
		if (m_user.empty()) {
			m_logonType = ANONYMOUS;
		}
		else if (m_user == L"anonymous") {
			if (m_pass.empty() || m_pass == L"anonymous@example.com") {
				m_logonType = ANONYMOUS;
			}
			else {
				m_logonType = NORMAL;
			}
		}
		else {
			m_logonType = NORMAL;
		}
	}

	if (m_protocol == UNKNOWN) {
		m_protocol = GetProtocolFromPort(port);
	}

	return true;
}

ServerProtocol CServer::GetProtocol() const
{
	return m_protocol;
}

ServerType CServer::GetType() const
{
	return m_type;
}

std::wstring CServer::GetHost() const
{
	return m_host;
}

unsigned int CServer::GetPort() const
{
	return m_port;
}

std::wstring CServer::GetUser() const
{
	if (m_logonType == ANONYMOUS) {
		return L"anonymous";
	}

	return m_user;
}

std::wstring CServer::GetPass() const
{
	if (m_logonType == ANONYMOUS) {
		return L"anon@localhost";
	}

	return m_pass;
}

std::wstring CServer::GetAccount() const
{
	if (m_logonType != ACCOUNT) {
		return std::wstring();
	}

	return m_account;
}

std::wstring CServer::GetKeyFile() const
{
	if (m_logonType != KEY) {
		return std::wstring();
	}

	return m_keyFile;
}

CServer& CServer::operator=(const CServer &op)
{
	m_protocol = op.m_protocol;
	m_type = op.m_type;
	m_host = op.m_host;
	m_port = op.m_port;
	m_logonType = op.m_logonType;
	m_user = op.m_user;
	m_pass = op.m_pass;
	m_account = op.m_account;
	m_keyFile = op.m_keyFile;
	m_timezoneOffset = op.m_timezoneOffset;
	m_pasvMode = op.m_pasvMode;
	m_maximumMultipleConnections = op.m_maximumMultipleConnections;
	m_encodingType = op.m_encodingType;
	m_customEncoding = op.m_customEncoding;
	m_postLoginCommands = op.m_postLoginCommands;
	m_bypassProxy = op.m_bypassProxy;
	m_name = op.m_name;

	return *this;
}

bool CServer::operator==(const CServer &op) const
{
	if (m_protocol != op.m_protocol) {
		return false;
	}
	else if (m_type != op.m_type) {
		return false;
	}
	else if (m_host != op.m_host) {
		return false;
	}
	else if (m_port != op.m_port) {
		return false;
	}
	else if (m_logonType != op.m_logonType) {
		return false;
	}
	else if (m_logonType != ANONYMOUS) {
		if (m_user != op.m_user) {
			return false;
		}

		if (m_logonType == NORMAL) {
			if (m_pass != op.m_pass) {
				return false;
			}
		}
		else if (m_logonType == ACCOUNT) {
			if (m_pass != op.m_pass) {
				return false;
			}
			if (m_account != op.m_account) {
				return false;
			}
		}
		else if (m_logonType == KEY) {
			if (m_keyFile != op.m_keyFile) {
				return false;
			}
		}
	}
	if (m_timezoneOffset != op.m_timezoneOffset) {
		return false;
	}
	else if (m_pasvMode != op.m_pasvMode) {
		return false;
	}
	else if (m_encodingType != op.m_encodingType) {
		return false;
	}
	else if (m_encodingType == ENCODING_CUSTOM) {
		if (m_customEncoding != op.m_customEncoding) {
			return false;
		}
	}
	if (m_postLoginCommands != op.m_postLoginCommands) {
		return false;
	}
	if (m_bypassProxy != op.m_bypassProxy) {
		return false;
	}

	// Do not compare number of allowed multiple connections

	return true;
}

bool CServer::operator<(const CServer &op) const
{
	if (m_protocol < op.m_protocol) {
		return true;
	}
	else if (m_protocol > op.m_protocol) {
		return false;
	}

	if (m_type < op.m_type) {
		return true;
	}
	else if (m_type > op.m_type) {
		return false;
	}

	int cmp = m_host.compare(op.m_host);
	if (cmp < 0) {
		return true;
	}
	else if (cmp > 0) {
		return false;
	}

	if (m_port < op.m_port) {
		return true;
	}
	else if (m_port > op.m_port) {
		return false;
	}

	if (m_logonType < op.m_logonType) {
		return true;
	}
	else if (m_logonType > op.m_logonType) {
		return false;
	}

	if (m_logonType != ANONYMOUS) {
		cmp = m_user.compare(op.m_user);
		if (cmp < 0) {
			return true;
		}
		else if (cmp > 0) {
			return false;
		}

		if (m_logonType == NORMAL) {
			cmp = m_pass.compare(op.m_pass);
			if (cmp < 0) {
				return true;
			}
			else if (cmp > 0) {
				return false;
			}
		}
		else if (m_logonType == ACCOUNT) {
			cmp = m_pass.compare(op.m_pass);
			if (cmp < 0) {
				return true;
			}
			else if (cmp > 0) {
				return false;
			}

			cmp = m_account.compare(op.m_account);
			if (cmp < 0) {
				return true;
			}
			else if (cmp > 0) {
				return false;
			}
		}
	}
	if (m_timezoneOffset < op.m_timezoneOffset) {
		return true;
	}
	else if (m_timezoneOffset > op.m_timezoneOffset) {
		return false;
	}

	if (m_pasvMode < op.m_pasvMode) {
		return true;
	}
	else if (m_pasvMode > op.m_pasvMode) {
		return false;
	}

	if (m_encodingType < op.m_encodingType) {
		return true;
	}
	else if (m_encodingType > op.m_encodingType) {
		return false;
	}

	if (m_encodingType == ENCODING_CUSTOM) {
		if (m_customEncoding < op.m_customEncoding) {
			return true;
		}
		else if (m_customEncoding > op.m_customEncoding) {
			return false;
		}
	}
	if (m_bypassProxy < op.m_bypassProxy) {
		return true;
	}
	else if (m_bypassProxy > op.m_bypassProxy) {
		return false;
	}

	// Do not compare number of allowed multiple connections

	return false;
}

bool CServer::operator!=(const CServer &op) const
{
	return !(*this == op);
}

bool CServer::EqualsNoPass(const CServer &op) const
{
	if (m_protocol != op.m_protocol) {
		return false;
	}
	else if (m_type != op.m_type) {
		return false;
	}
	else if (m_host != op.m_host) {
		return false;
	}
	else if (m_port != op.m_port) {
		return false;
	}
	else if ((m_logonType == ANONYMOUS) != (op.m_logonType == ANONYMOUS)) {
		return false;
	}
	else if ((m_logonType == ACCOUNT) != (op.m_logonType == ACCOUNT)) {
		return false;
	}
	else if (m_logonType != ANONYMOUS) {
		if (m_user != op.m_user) {
			return false;
		}
		if (m_logonType == ACCOUNT) {
			if (m_account != op.m_account) {
				return false;
			}
		}
	}
	if (m_timezoneOffset != op.m_timezoneOffset) {
		return false;
	}
	else if (m_pasvMode != op.m_pasvMode) {
		return false;
	}
	else if (m_encodingType != op.m_encodingType) {
		return false;
	}
	else if (m_encodingType == ENCODING_CUSTOM) {
		if (m_customEncoding != op.m_customEncoding) {
			return false;
		}
	}
	if (m_postLoginCommands != op.m_postLoginCommands) {
		return false;
	}
	if (m_bypassProxy != op.m_bypassProxy) {
		return false;
	}

	// Do not compare number of allowed multiple connections

	return true;
}

CServer::CServer(ServerProtocol protocol, ServerType type, std::wstring const& host, unsigned int port, std::wstring const& user, std::wstring const& pass, std::wstring const& account)
{
	m_protocol = protocol;
	m_type = type;
	m_host = host;
	m_port = port;
	m_logonType = NORMAL;
	m_user = user;
	m_pass = pass;
	m_account = account;
}

CServer::CServer(ServerProtocol protocol, ServerType type, std::wstring const& host, unsigned int port)
{
	m_protocol = protocol;
	m_type = type;
	m_host = host;
	m_port = port;
}

void CServer::SetType(ServerType type)
{
	m_type = type;
}

LogonType CServer::GetLogonType() const
{
	return m_logonType;
}

void CServer::SetLogonType(LogonType logonType)
{
	assert(logonType != LOGONTYPE_MAX);
	m_logonType = logonType;
}

void CServer::SetProtocol(ServerProtocol serverProtocol)
{
	assert(serverProtocol != UNKNOWN);

	if (!GetProtocolInfo(serverProtocol).supportsPostlogin) {
		m_postLoginCommands.clear();
	}

	m_protocol = serverProtocol;
}

bool CServer::SetHost(std::wstring const& host, unsigned int port)
{
	if (host.empty()) {
		return false;
	}

	if (port < 1 || port > 65535) {
		return false;
	}

	m_host = host;
	m_port = port;

	if (m_protocol == UNKNOWN) {
		m_protocol = GetProtocolFromPort(m_port);
	}

	return true;
}

bool CServer::SetUser(std::wstring const& user, std::wstring const& pass)
{
	if (m_logonType == ANONYMOUS) {
		return true;
	}

	if (user.empty()) {
		if (m_logonType != ASK && m_logonType != INTERACTIVE) {
			return false;
		}
		m_pass.clear();
	}
	else {
		m_pass = pass;
	}

	m_user = user;

	return true;
}

bool CServer::SetAccount(std::wstring const& account)
{
	if (m_logonType != ACCOUNT) {
		return false;
	}

	m_account = account;

	return true;
}

bool CServer::SetKeyFile(std::wstring const& keyFile)
{
	if (m_logonType != KEY) {
		return false;
	}

	m_keyFile = keyFile;

	return true;
}

bool CServer::SetTimezoneOffset(int minutes)
{
	if (minutes > (60 * 24) || minutes < (-60 * 24)) {
		return false;
	}

	m_timezoneOffset = minutes;

	return true;
}

int CServer::GetTimezoneOffset() const
{
	return m_timezoneOffset;
}

PasvMode CServer::GetPasvMode() const
{
	return m_pasvMode;
}

void CServer::SetPasvMode(PasvMode pasvMode)
{
	m_pasvMode = pasvMode;
}

void CServer::MaximumMultipleConnections(int maximumMultipleConnections)
{
	m_maximumMultipleConnections = maximumMultipleConnections;
}

int CServer::MaximumMultipleConnections() const
{
	return m_maximumMultipleConnections;
}

std::wstring CServer::Format(ServerFormat formatType) const
{
	std::wstring server = m_host;

	t_protocolInfo const& info = GetProtocolInfo(m_protocol);

	if (server.find(':') != std::wstring::npos) {
		server = L"[" + server + L"]";
	}

	if (formatType == ServerFormat::host_only) {
		return server;
	}

	if (m_port != GetDefaultPort(m_protocol)) {
		server += fz::sprintf(L":%d", m_port);
	}

	if (formatType == ServerFormat::with_optional_port) {
		return server;
	}

	if (m_logonType != ANONYMOUS) {
		auto user = GetUser();
		// For now, only escape if formatting for URL.
		// Open question: Do we need some form of escapement for presentation within the GUI,
		// that deals e.g. with whitespace but does not touch Unicode characters?
		if (formatType == ServerFormat::url || formatType == ServerFormat::url_with_password) {
			user = fz::percent_encode_w(user);
		}
		if (!user.empty()) {
			if (formatType == ServerFormat::url_with_password) {
				auto pass = GetPass();
				if (!pass.empty()) {
					if (formatType == ServerFormat::url || formatType == ServerFormat::url_with_password) {
						pass = fz::percent_encode_w(pass);
					}
					server = user + L":" + pass + L"@" + server;
				}
			}
			else {
				server = fz::percent_encode_w(user) + L"@" + server;
			}
		}
	}

	if (formatType == ServerFormat::with_user_and_optional_port) {
		if (!info.alwaysShowPrefix && m_port == info.defaultPort) {
			return server;
		}
	}

	if (!info.prefix.empty()) {
		server = info.prefix + L"://" + server;
	}

	return server;
}

void CServer::clear()
{
	*this = CServer();
}

bool CServer::SetEncodingType(CharsetEncoding type, std::wstring const& encoding)
{
	if (type == ENCODING_CUSTOM && encoding.empty()) {
		return false;
	}

	m_encodingType = type;
	m_customEncoding = encoding;

	return true;
}

bool CServer::SetCustomEncoding(std::wstring const& encoding)
{
	if (encoding.empty()) {
		return false;
	}

	m_encodingType = ENCODING_CUSTOM;
	m_customEncoding = encoding;

	return true;
}

CharsetEncoding CServer::GetEncodingType() const
{
	return m_encodingType;
}

std::wstring CServer::GetCustomEncoding() const
{
	return m_customEncoding;
}

unsigned int CServer::GetDefaultPort(ServerProtocol protocol)
{
	const t_protocolInfo& info = GetProtocolInfo(protocol);

	return info.defaultPort;
}

ServerProtocol CServer::GetProtocolFromPort(unsigned int port, bool defaultOnly)
{
	for (unsigned int i = 0; protocolInfos[i].protocol != UNKNOWN; ++i) {
		if (protocolInfos[i].defaultPort == port) {
			return protocolInfos[i].protocol;
		}
	}

	if (defaultOnly) {
		return UNKNOWN;
	}

	// Else default to FTP
	return FTP;
}

std::wstring CServer::GetProtocolName(ServerProtocol protocol)
{
	t_protocolInfo const* protocolInfo = protocolInfos;
	while (protocolInfo->protocol != UNKNOWN) {
		if (protocolInfo->protocol != protocol) {
			++protocolInfo;
			continue;
		}

		if (protocolInfo->translateable) {
			return fz::translate(protocolInfo->name);
		}
		else {
			return fz::to_wstring(protocolInfo->name);
		}
	}

	return std::wstring();
}

ServerProtocol CServer::GetProtocolFromName(std::wstring const& name)
{
	const t_protocolInfo *protocolInfo = protocolInfos;
	while (protocolInfo->protocol != UNKNOWN) {
		if (protocolInfo->translateable) {
			if (fz::translate(protocolInfo->name) == name) {
				return protocolInfo->protocol;
			}
		}
		else {
			if (fz::to_wstring(protocolInfo->name) == name) {
				return protocolInfo->protocol;
			}
		}
		++protocolInfo;
	}

	return UNKNOWN;
}

bool CServer::SetPostLoginCommands(const std::vector<std::wstring>& postLoginCommands)
{
	if (!SupportsPostLoginCommands(m_protocol)) {
		m_postLoginCommands.clear();
		return false;
	}

	m_postLoginCommands = postLoginCommands;
	return true;
}

bool CServer::SupportsPostLoginCommands(ServerProtocol const protocol)
{
	return protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP;
}

ServerProtocol CServer::GetProtocolFromPrefix(std::wstring const& prefix)
{
	std::wstring lower = fz::str_tolower_ascii(prefix);
	for (unsigned int i = 0; protocolInfos[i].protocol != UNKNOWN; ++i) {
		if (protocolInfos[i].prefix == lower) {
			return protocolInfos[i].protocol;
		}
	}

	return UNKNOWN;
}

std::wstring CServer::GetPrefixFromProtocol(ServerProtocol const protocol)
{
	const t_protocolInfo& info = GetProtocolInfo(protocol);

	return info.prefix;
}

std::vector<ServerProtocol> const& CServer::GetDefaultProtocols()
{
	return defaultProtocols;
}

void CServer::SetBypassProxy(bool val)
{
	m_bypassProxy = val;
}

bool CServer::GetBypassProxy() const
{
	return m_bypassProxy;
}

bool CServer::ProtocolHasDataTypeConcept(const ServerProtocol protocol)
{
	if (protocol == FTP || protocol == FTPS || protocol == FTPES || protocol == INSECURE_FTP) {
		return true;
	}

	return false;
}

std::wstring CServer::GetNameFromServerType(ServerType type)
{
	assert(type != SERVERTYPE_MAX);
	return fz::translate(typeNames[type]);
}

ServerType CServer::GetServerTypeFromName(std::wstring const& name)
{
	for (int i = 0; i < SERVERTYPE_MAX; ++i) {
		ServerType type = static_cast<ServerType>(i);
		if (name == CServer::GetNameFromServerType(type)) {
			return type;
		}
	}

	return DEFAULT;
}

LogonType CServer::GetLogonTypeFromName(std::wstring const& name)
{
	if (name == _("Normal")) {
		return NORMAL;
	}
	else if (name == _("Ask for password")) {
		return ASK;
	}
	else if (name == _("Key file")) {
		return KEY;
	}
	else if (name == _("Interactive")) {
		return INTERACTIVE;
	}
	else if (name == _("Account")) {
		return ACCOUNT;
	}
	else {
		return ANONYMOUS;
	}
}

std::wstring CServer::GetNameFromLogonType(LogonType type)
{
	assert(type != LOGONTYPE_MAX);

	switch (type)
	{
	case NORMAL:
		return _("Normal");
	case ASK:
		return _("Ask for password");
	case KEY:
		return _("Key file");
	case INTERACTIVE:
		return _("Interactive");
	case ACCOUNT:
		return _("Account");
	default:
		return _("Anonymous");
	}
}
