#ifndef FILEZILLA_ENGINE_SERVERCAPABILITIES_HEADER
#define FILEZILLA_ENGINE_SERVERCAPABILITIES_HEADER

enum capabilities
{
	unknown,
	yes,
	no
};

enum capabilityNames
{
	resume2GBbug,
	resume4GBbug,

	// FTP-protocol specific
	syst_command, // reply of SYST command as option
	feat_command,
	clnt_command, // set to 'yes' if CLNT should be sent
	utf8_command, // set to 'yes' if OPTS UTF8 ON should be sent
	mlsd_command,
	opst_mlst_command, // Arguments for OPTS MLST command
	mfmt_command,
	mdtm_command,
	size_command,
	mode_z_support,
	tvfs_support, // Trivial virtual file store (RFC 3659)
	list_hidden_support, // LIST -a command
	rest_stream, // supports REST+STOR in addition to APPE
	epsv_command,

	// FTPS and HTTPS
	tls_resume, // Does the server support resuming of TLS sessions?

	// Server timezone offset. If using FTP, LIST details are unspecified and
	// can return different times than the UTC based times using the MLST or
	// MDTM commands.
	// Note that the user can invoke an additional timezone offset on top of
	// this for server not supporting auto-detection or to compensate
	// unsynchronized clocks.
	timezone_offset,

	auth_tls_command,
	auth_ssl_command
};

class CCapabilities final
{
public:
	capabilities GetCapability(capabilityNames name, std::wstring* pOption = 0) const;
	capabilities GetCapability(capabilityNames name, int* pOption) const;
	void SetCapability(capabilityNames name, capabilities cap, std::wstring const& option = std::wstring());
	void SetCapability(capabilityNames name, capabilities cap, int option);

protected:
	struct t_cap
	{
		capabilities cap;
		std::wstring option;
		int number;
	};
	std::map<capabilityNames, t_cap> m_capabilityMap;
};

class CServerCapabilities final
{
public:
	// If return value isn't 'yes', pOptions remains unchanged
	static capabilities GetCapability(const CServer& server, capabilityNames name, std::wstring* pOption = 0);
	static capabilities GetCapability(const CServer& server, capabilityNames name, int* option);

	static void SetCapability(const CServer& server, capabilityNames name, capabilities cap, std::wstring const& option = std::wstring());
	static void SetCapability(const CServer& server, capabilityNames name, capabilities cap, int option);

protected:
	static std::map<CServer, CCapabilities> m_serverMap;
};

#endif
