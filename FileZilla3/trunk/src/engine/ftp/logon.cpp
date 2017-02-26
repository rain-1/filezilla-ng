#include <filezilla.h>

#include "logon.h"
#include "../proxy.h"
#include "../servercapabilities.h"
#include "../tlssocket.h"

CFtpLogonOpData::CFtpLogonOpData(CFtpControlSocket& controlSocket, CServer const& server)
    : CConnectOpData(server), CFtpOpData(controlSocket)
{
	for (int i = 0; i < LOGON_DONE; ++i) {
		neededCommands[i] = 1;
	}

	if (server.GetProtocol() != FTPES && server.GetProtocol() != FTP) {
		neededCommands[LOGON_AUTH_TLS] = 0;
		neededCommands[LOGON_AUTH_SSL] = 0;
		neededCommands[LOGON_AUTH_WAIT] = 0;
		if (server.GetProtocol() != FTPS) {
			neededCommands[LOGON_PBSZ] = 0;
			neededCommands[LOGON_PROT] = 0;
		}
	}
	if (server.GetPostLoginCommands().empty()) {
		neededCommands[LOGON_CUSTOMCOMMANDS] = 0;
	}

	const CharsetEncoding encoding = server.GetEncodingType();
	if (encoding == ENCODING_AUTO && CServerCapabilities::GetCapability(server, utf8_command) != no) {
		controlSocket_.m_useUTF8 = true;
	}
	else if (encoding == ENCODING_UTF8) {
		controlSocket_.m_useUTF8 = true;
	}
}

int CFtpLogonOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpLogonOpData::Send() in state %d", opState);

	switch (opState)
	{
	case LOGON_CONNECT:
	    {
		    if (!GetLoginSequence()) {
				return FZ_REPLY_INTERNALERROR;
			}

			// Do not use FTP proxy if generic proxy is set
			int generic_proxy_type = engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE);
			if ((generic_proxy_type <= CProxySocket::unknown || generic_proxy_type >= CProxySocket::proxytype_count) &&
			    (ftp_proxy_type = engine_.GetOptions().GetOptionVal(OPTION_FTP_PROXY_TYPE)) && !server_.GetBypassProxy())
			{
				host_ = engine_.GetOptions().GetOption(OPTION_FTP_PROXY_HOST);

				size_t pos = -1;
				if (!host_.empty() && host_[0] == '[') {
					// Probably IPv6 address
					pos = host_.find(']');
					if (pos == std::wstring::npos) {
						LogMessage(MessageType::Error, _("Proxy host starts with '[' but no closing bracket found."));
						return FZ_REPLY_DISCONNECTED | FZ_REPLY_CRITICALERROR;
					}
					if (host_.size() > (pos + 1) && host_[pos + 1]) {
						if (host_[pos + 1] != ':') {
							LogMessage(MessageType::Error, _("Invalid proxy host, after closing bracket only colon and port may follow."));
							return FZ_REPLY_DISCONNECTED | FZ_REPLY_CRITICALERROR;
						}
						++pos;
					}
					else {
						pos = std::wstring::npos;
					}
				}
				else {
					pos = host_.find(':');
				}

				if (pos != std::wstring::npos) {
					port_ = fz::to_integral<unsigned int>(host_.substr(pos + 1));
					host_ = host_.substr(0, pos);
				}
				else {
					port_ = 21;
				}

				if (host_.empty() || port_ < 1 || port_ > 65535) {
					LogMessage(MessageType::Error, _("Proxy set but proxy host or port invalid"));
					return FZ_REPLY_DISCONNECTED | FZ_REPLY_CRITICALERROR;
				}

				LogMessage(MessageType::Status, _("Connecting to %s through %s proxy"), server_.Format(ServerFormat::with_optional_port), L"FTP"); // @translator: Connecting to ftp.example.com through SOCKS5 proxy
			}
			else {
				ftp_proxy_type = 0;
				host_ = server_.GetHost();
				port_ = server_.GetPort();
			}

			opState = LOGON_WELCOME;
			return controlSocket_.DoConnect(server_);
	    }
	case LOGON_AUTH_WAIT:
		LogMessage(MessageType::Debug_Info, L"LogonSend() called during LOGON_AUTH_WAIT, ignoring");
		return FZ_REPLY_WOULDBLOCK;
	case LOGON_AUTH_TLS:
		return controlSocket_.SendCommand(L"AUTH TLS", false, false);
	case LOGON_AUTH_SSL:
		return controlSocket_.SendCommand(L"AUTH SSL", false, false);
	case LOGON_SYST:
		return controlSocket_.SendCommand(L"SYST");
	case LOGON_LOGON:
	    {
		    t_loginCommand cmd = loginSequence.front();
			switch (cmd.type)
			{
			case loginCommandType::user:
				if (currentServer_.GetLogonType() == INTERACTIVE) {
					waitChallenge = true;
					challenge.clear();
				}

				if (cmd.command.empty()) {
					return controlSocket_.SendCommand(L"USER " + currentServer_.GetUser());
				}
				else {
					return controlSocket_.SendCommand(cmd.command);
				}
			case loginCommandType::pass:
				if (!challenge.empty()) {
					CInteractiveLoginNotification *pNotification = new CInteractiveLoginNotification(CInteractiveLoginNotification::interactive, challenge, false);
					pNotification->server = currentServer_;
					challenge.clear();

					controlSocket_.SendAsyncRequest(pNotification);

					return FZ_REPLY_WOULDBLOCK;
				}

				if (cmd.command.empty()) {
					return controlSocket_.SendCommand(L"PASS " + currentServer_.GetPass(), true);
				}
				else {
					std::wstring c = cmd.command;
					std::wstring pass = currentServer_.GetPass();
					fz::replace_substrings(pass, L"%", L"%%");
					fz::replace_substrings(c, L"%p", pass);
					fz::replace_substrings(c, L"%%", L"%");
					return controlSocket_.SendCommand(c, true);
				}
				break;
			case loginCommandType::account:
				if (cmd.command.empty()) {
					return controlSocket_.SendCommand(L"ACCT " + currentServer_.GetAccount());
				}
				else {
					return controlSocket_.SendCommand(cmd.command);
				}
				break;
			case loginCommandType::other:
				assert(!cmd.command.empty());
				return controlSocket_.SendCommand(cmd.command, cmd.hide_arguments);
			default:
				return FZ_REPLY_INTERNALERROR;
			}
	    }
		break;
	case LOGON_FEAT:
		return controlSocket_.SendCommand(L"FEAT");
	case LOGON_CLNT:
		// Some servers refuse to enable UTF8 if client does not send CLNT command
		// to fix compatibility with Internet Explorer, but in the process breaking
		// compatibility with other clients.
		// Rather than forcing MS to fix Internet Explorer, letting other clients
		// suffer is a questionable decision in my opinion.
		return controlSocket_.SendCommand(L"CLNT FileZilla");
	case LOGON_OPTSUTF8:
		// Handle servers that disobey RFC 2640 by having UTF8 in their FEAT
		// response but do not use UTF8 unless OPTS UTF8 ON gets send.
		// However these servers obey a conflicting ietf draft:
		// http://www.ietf.org/proceedings/02nov/I-D/draft-ietf-ftpext-utf-8-option-00.txt
		// Example servers are, amongst others, G6 FTP Server and RaidenFTPd.
		return controlSocket_.SendCommand(L"OPTS UTF8 ON");
	case LOGON_PBSZ:
		return controlSocket_.SendCommand(L"PBSZ 0");
	case LOGON_PROT:
		return controlSocket_.SendCommand(L"PROT P");
	case LOGON_CUSTOMCOMMANDS:
		if (customCommandIndex >= currentServer_.GetPostLoginCommands().size()) {
			LogMessage(MessageType::Debug_Warning, L"pData->customCommandIndex >= m_pCurrentServer->GetPostLoginCommands().size()");
			return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
		}
		return controlSocket_.SendCommand(currentServer_.GetPostLoginCommands()[customCommandIndex]);
	case LOGON_OPTSMLST:
	    {
		    std::wstring args;
			CServerCapabilities::GetCapability(currentServer_, opst_mlst_command, &args);
			return controlSocket_.SendCommand(L"OPTS MLST " + args);
	    }
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CFtpLogonOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CFtpLogonOpData::ParseResponse() in state %d", opState);

	int code = controlSocket_.GetReplyCode();
	std::wstring const& response = controlSocket_.m_Response;

	if (opState == LOGON_WELCOME) {
		if (code != 2 && code != 3) {
			return FZ_REPLY_DISCONNECTED | (code == 5 ? FZ_REPLY_CRITICALERROR : FZ_REPLY_ERROR);
		}
	}
	else if (opState == LOGON_AUTH_TLS ||
	         opState == LOGON_AUTH_SSL)
	{
		if (code != 2 && code != 3) {
			CServerCapabilities::SetCapability(currentServer_, (opState == LOGON_AUTH_TLS) ? auth_tls_command : auth_ssl_command, no);
			if (opState == LOGON_AUTH_SSL) {
				if (currentServer_.GetProtocol() == FTP) {
					// For now. In future make TLS mandatory unless explicitly requested INSECURE_FTP as protocol
					LogMessage(MessageType::Status, _("Insecure server, it does not support FTP over TLS."));
					neededCommands[LOGON_PBSZ] = 0;
					neededCommands[LOGON_PROT] = 0;

					opState = LOGON_LOGON;
					return FZ_REPLY_CONTINUE;
				}
				else {
					return FZ_REPLY_DISCONNECTED | (code == 5 ? FZ_REPLY_CRITICALERROR : FZ_REPLY_ERROR);
				}
			}
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, (opState == LOGON_AUTH_TLS) ? auth_tls_command : auth_ssl_command, yes);

			LogMessage(MessageType::Status, _("Initializing TLS..."));

			assert(!controlSocket_.m_pTlsSocket);
			delete controlSocket_.m_pBackend;

			controlSocket_.m_pTlsSocket = new CTlsSocket(&controlSocket_, *controlSocket_.m_pSocket, &controlSocket_);
			controlSocket_.m_pBackend = controlSocket_.m_pTlsSocket;

			if (!controlSocket_.m_pTlsSocket->Init()) {
				LogMessage(MessageType::Error, _("Failed to initialize TLS."));
				return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
			}

			int res = controlSocket_.m_pTlsSocket->Handshake();
			if (res & FZ_REPLY_ERROR) {
				return res | FZ_REPLY_DISCONNECTED;
			}

			neededCommands[LOGON_AUTH_SSL] = 0;
			opState = LOGON_AUTH_WAIT;

			return FZ_REPLY_WOULDBLOCK;
		}
	}
	else if (opState == LOGON_LOGON) {
		t_loginCommand cmd = loginSequence.front();

		if (code != 2 && code != 3) {
			if (cmd.type == loginCommandType::user || cmd.type == loginCommandType::pass) {
				auto const user = currentServer_.GetUser();
				if (!user.empty() && (user.front() == ' ' || user.back() == ' ')) {
					LogMessage(MessageType::Status, _("Check your login credentials. The entered username starts or ends with a space character."));
				}
				auto const pw = currentServer_.GetPass();
				if (!pw.empty() && (pw.front() == ' ' || pw.back() == ' ')) {
					LogMessage(MessageType::Status, _("Check your login credentials. The entered password starts or ends with a space character."));
				}
			}

			if (currentServer_.GetEncodingType() == ENCODING_AUTO && controlSocket_.m_useUTF8) {
				// Fall back to local charset for the case that the server might not
				// support UTF8 and the login data contains non-ascii characters.
				bool asciiOnly = true;
				if (!fz::str_is_ascii(currentServer_.GetUser())) {
					asciiOnly = false;
				}
				if (!fz::str_is_ascii(currentServer_.GetPass())) {
					asciiOnly = false;
				}
				if (!fz::str_is_ascii(currentServer_.GetAccount())) {
					asciiOnly = false;
				}
				if (!asciiOnly) {
					if (ftp_proxy_type) {
						LogMessage(MessageType::Status, _("Login data contains non-ASCII characters and server might not be UTF-8 aware. Cannot fall back to local charset since using proxy."));
						int error = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR;
						if (cmd.type == loginCommandType::pass && code == 5) {
							error |= FZ_REPLY_PASSWORDFAILED;
						}
						return error;
					}
					LogMessage(MessageType::Status, _("Login data contains non-ASCII characters and server might not be UTF-8 aware. Trying local charset."));
					controlSocket_.m_useUTF8 = false;
					if (!GetLoginSequence()) {
						int error = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR;
						if (cmd.type == loginCommandType::pass && code == 5) {
							error |= FZ_REPLY_PASSWORDFAILED;
						}
						return error;
					}
					return FZ_REPLY_CONTINUE;
				}
			}

			int error = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR;
			if (cmd.type == loginCommandType::pass && code == 5) {
				error |= FZ_REPLY_CRITICALERROR | FZ_REPLY_PASSWORDFAILED;
			}
			return error;
		}

		loginSequence.pop_front();
		if (code == 2) {
			while (!loginSequence.empty() && loginSequence.front().optional) {
				loginSequence.pop_front();
			}
		}
		else if (code == 3 && loginSequence.empty()) {
			LogMessage(MessageType::Error, _("Login sequence fully executed yet not logged in, aborting."));
			if (cmd.type == loginCommandType::pass && currentServer_.GetAccount().empty()) {
				LogMessage(MessageType::Error, _("Server might require an account. Try specifying an account using the Site Manager"));
			}
			return FZ_REPLY_CRITICALERROR | FZ_REPLY_DISCONNECTED;
		}

		if (!loginSequence.empty()) {
			waitChallenge = false;

			return FZ_REPLY_CONTINUE;
		}
	}
	else if (opState == LOGON_SYST) {
		if (code == 2) {
			CServerCapabilities::SetCapability(currentServer_, syst_command, yes, response.substr(4));
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, syst_command, no);
		}

		if (currentServer_.GetType() == DEFAULT && code == 2) {
			if (response.size() > 7 && response.substr(3, 4) == L" MVS") {
				currentServer_.SetType(MVS);
			}
			else if (response.size() > 12 && fz::str_toupper_ascii(response.substr(3, 9)) == L" NONSTOP ") {
				currentServer_.SetType(HPNONSTOP);
			}

			if (!controlSocket_.m_MultilineResponseLines.empty() && fz::str_tolower_ascii(controlSocket_.m_MultilineResponseLines.front().substr(4, 4)) == L"z/vm") {
				CServerCapabilities::SetCapability(currentServer_, syst_command, yes, controlSocket_.m_MultilineResponseLines.front().substr(4) + L" " + response.substr(4));
				currentServer_.SetType(ZVM);
			}
		}

		if (response.find(L"FileZilla") != std::wstring::npos) {
			neededCommands[LOGON_CLNT] = 0;
			neededCommands[LOGON_OPTSUTF8] = 0;
		}
	}
	else if (opState == LOGON_FEAT) {
		if (code == 2) {
			CServerCapabilities::SetCapability(currentServer_, feat_command, yes);
			if (CServerCapabilities::GetCapability(currentServer_, utf8_command) != yes) {
				CServerCapabilities::SetCapability(currentServer_, utf8_command, no);
			}
			if (CServerCapabilities::GetCapability(currentServer_, clnt_command) != yes) {
				CServerCapabilities::SetCapability(currentServer_, clnt_command, no);
			}
		}
		else {
			CServerCapabilities::SetCapability(currentServer_, feat_command, no);
		}

		if (CServerCapabilities::GetCapability(currentServer_, tvfs_support) != yes) {
			CServerCapabilities::SetCapability(currentServer_, tvfs_support, no);
		}

		const CharsetEncoding encoding = currentServer_.GetEncodingType();
		if (encoding == ENCODING_AUTO && CServerCapabilities::GetCapability(currentServer_, utf8_command) != yes) {
			LogMessage(MessageType::Status, _("Server does not support non-ASCII characters."));
			controlSocket_.m_useUTF8 = false;
		}
	}
	else if (opState == LOGON_PROT) {
		if (code == 2 || code == 3) {
			controlSocket_.m_protectDataChannel = true;
		}
	}
	else if (opState == LOGON_CUSTOMCOMMANDS) {
		++customCommandIndex;
		if (customCommandIndex < currentServer_.GetPostLoginCommands().size()) {
			return FZ_REPLY_CONTINUE;
		}
	}

	for (;;) {
		++opState;

		if (opState == LOGON_DONE) {
			LogMessage(MessageType::Status, _("Logged in"));
			LogMessage(MessageType::Debug_Info, L"Measured latency of %d ms", controlSocket_.m_rtt.GetLatency());
			return FZ_REPLY_OK;
		}

		if (!neededCommands[opState]) {
			continue;
		}
		else if (opState == LOGON_SYST) {
			std::wstring system;
			capabilities cap = CServerCapabilities::GetCapability(currentServer_, syst_command, &system);
			if (cap == unknown) {
				break;
			}
			else if (cap == yes) {
				if (currentServer_.GetType() == DEFAULT) {
					if (system.substr(0, 3) == L"MVS") {
						currentServer_.SetType(MVS);
					}
					else if (fz::str_toupper_ascii(system.substr(0, 4)) == L"Z/VM") {
						currentServer_.SetType(ZVM);
					}
					else if (fz::str_toupper_ascii(system.substr(0, 8)) == L"NONSTOP ") {
						currentServer_.SetType(HPNONSTOP);
					}
				}

				if (system.find(L"FileZilla") != std::wstring::npos) {
					neededCommands[LOGON_CLNT] = 0;
					neededCommands[LOGON_OPTSUTF8] = 0;
				}
			}
		}
		else if (opState == LOGON_FEAT) {
			capabilities cap = CServerCapabilities::GetCapability(currentServer_, feat_command);
			if (cap == unknown) {
				break;
			}
			const CharsetEncoding encoding = currentServer_.GetEncodingType();
			if (encoding == ENCODING_AUTO && CServerCapabilities::GetCapability(currentServer_, utf8_command) != yes) {
				LogMessage(MessageType::Status, _("Server does not support non-ASCII characters."));
				controlSocket_.m_useUTF8 = false;
			}
		}
		else if (opState == LOGON_CLNT) {
			if (!controlSocket_.m_useUTF8) {
				continue;
			}

			if (CServerCapabilities::GetCapability(currentServer_, clnt_command) == yes) {
				break;
			}
		}
		else if (opState == LOGON_OPTSUTF8) {
			if (!controlSocket_.m_useUTF8) {
				continue;
			}

			if (CServerCapabilities::GetCapability(currentServer_, utf8_command) == yes) {
				break;
			}
		}
		else if (opState == LOGON_OPTSMLST) {
			std::wstring facts;
			if (CServerCapabilities::GetCapability(currentServer_, mlsd_command, &facts) != yes) {
				continue;
			}
			capabilities cap = CServerCapabilities::GetCapability(currentServer_, opst_mlst_command);
			if (cap == unknown) {
				facts = fz::str_tolower_ascii(facts);

				bool had_unset = false;
				std::wstring opts_facts;

				// Create a list of all facts understood by both FZ and the server.
				// Check if there's any supported fact not enabled by default, should that
				// be the case we need to send OPTS MLST
				while (!facts.empty()) {
					size_t delim = facts.find(';');
					if (delim == std::wstring::npos) {
						break;
					}

					if (!delim) {
						facts = facts.substr(1);
						continue;
					}

					bool enabled;
					std::wstring fact;

					if (facts[delim - 1] == '*') {
						if (delim == 1) {
							facts = facts.substr(delim + 1);
							continue;
						}
						enabled = true;
						fact = facts.substr(0, delim - 1);
					}
					else {
						enabled = false;
						fact = facts.substr(0, delim);
					}
					facts = facts.substr(delim + 1);

					if (fact == L"type" ||
					    fact == L"size" ||
					    fact == L"modify" ||
					    fact == L"perm" ||
					    fact == L"unix.mode" ||
					    fact == L"unix.owner" ||
					    fact == L"unix.ownername" ||
					    fact == L"unix.group" ||
					    fact == L"unix.groupname" ||
					    fact == L"unix.user" ||
					    fact == L"unix.uid" ||
					    fact == L"unix.gid" ||
					    fact == L"x.hidden")
					{
						had_unset |= !enabled;
						opts_facts += fact + L";";
					}
				}

				if (had_unset) {
					CServerCapabilities::SetCapability(currentServer_, opst_mlst_command, yes, opts_facts);
					break;
				}
				else {
					CServerCapabilities::SetCapability(currentServer_, opst_mlst_command, no);
				}
			}
			else if (cap == yes) {
				break;
			}
		}
		else {
			break;
		}
	}

	return FZ_REPLY_CONTINUE;
}

bool CFtpLogonOpData::GetLoginSequence()
{
	loginSequence.clear();

	if (!ftp_proxy_type) {
		// User
		t_loginCommand cmd = {false, false, loginCommandType::user, L""};
		loginSequence.push_back(cmd);

		// Password
		cmd.optional = true;
		cmd.hide_arguments = true;
		cmd.type = loginCommandType::pass;
		loginSequence.push_back(cmd);

		// Optional account
		if (!server_.GetAccount().empty()) {
			cmd.hide_arguments = false;
			cmd.type = loginCommandType::account;
			loginSequence.push_back(cmd);
		}
	}
	else if (ftp_proxy_type == 1) {
		std::wstring const proxyUser = engine_.GetOptions().GetOption(OPTION_FTP_PROXY_USER);
		if (!proxyUser.empty()) {
			// Proxy logon (if credendials are set)
			t_loginCommand cmd = {false, false, loginCommandType::other, L"USER " + proxyUser};
			loginSequence.push_back(cmd);
			cmd.optional = true;
			cmd.hide_arguments = true;
			cmd.command = L"PASS " + engine_.GetOptions().GetOption(OPTION_FTP_PROXY_PASS);
			loginSequence.push_back(cmd);
		}
		// User@host
		t_loginCommand cmd = {false, false, loginCommandType::user, fz::sprintf(L"USER %s@%s", server_.GetUser(), server_.Format(ServerFormat::with_optional_port))};
		loginSequence.push_back(cmd);

		// Password
		cmd.optional = true;
		cmd.hide_arguments = true;
		cmd.type = loginCommandType::pass;
		cmd.command = L"";
		loginSequence.push_back(cmd);

		// Optional account
		if (!server_.GetAccount().empty()) {
			cmd.hide_arguments = false;
			cmd.type = loginCommandType::account;
			loginSequence.push_back(cmd);
		}
	}
	else if (ftp_proxy_type == 2 || ftp_proxy_type == 3) {
		std::wstring const proxyUser = engine_.GetOptions().GetOption(OPTION_FTP_PROXY_USER);
		if (!proxyUser.empty()) {
			// Proxy logon (if credendials are set)
			t_loginCommand cmd = {false, false, loginCommandType::other, L"USER " + proxyUser};
			loginSequence.push_back(cmd);
			cmd.optional = true;
			cmd.hide_arguments = true;
			cmd.command = L"PASS " + engine_.GetOptions().GetOption(OPTION_FTP_PROXY_PASS);
			loginSequence.push_back(cmd);
		}

		// Site or Open
		t_loginCommand cmd = {false, false, loginCommandType::user, L""};
		if (ftp_proxy_type == 2) {
			cmd.command = L"SITE " + server_.Format(ServerFormat::with_optional_port);
		}
		else {
			cmd.command = L"OPEN " + server_.Format(ServerFormat::with_optional_port);
		}
		loginSequence.push_back(cmd);

		// User
		cmd.type = loginCommandType::user;
		cmd.command = L"";
		loginSequence.push_back(cmd);

		// Password
		cmd.optional = true;
		cmd.hide_arguments = true;
		cmd.type = loginCommandType::pass;
		loginSequence.push_back(cmd);

		// Optional account
		if (!server_.GetAccount().empty()) {
			cmd.hide_arguments = false;
			cmd.type = loginCommandType::account;
			loginSequence.push_back(cmd);
		}
	}
	else if (ftp_proxy_type == 4) {
		std::wstring proxyUser = engine_.GetOptions().GetOption(OPTION_FTP_PROXY_USER);
		std::wstring proxyPass = engine_.GetOptions().GetOption(OPTION_FTP_PROXY_PASS);
		std::wstring host = server_.Format(ServerFormat::with_optional_port);
		std::wstring user = server_.GetUser();
		std::wstring account = server_.GetAccount();
		fz::replace_substrings(proxyUser, L"%", L"%%");
		fz::replace_substrings(proxyPass, L"%", L"%%");
		fz::replace_substrings(host, L"%", L"%%");
		fz::replace_substrings(user, L"%", L"%%");
		fz::replace_substrings(account, L"%", L"%%");

		std::wstring const loginSequenceStr = engine_.GetOptions().GetOption(OPTION_FTP_PROXY_CUSTOMLOGINSEQUENCE);
		std::vector<std::wstring> const tokens = fz::strtok(loginSequenceStr, L"\r\n");

		for (auto token : tokens) {
			bool isHost = false;
			bool isUser = false;
			bool password = false;
			bool isProxyUser = false;
			bool isProxyPass = false;
			if (token.find(L"%h") != token.npos) {
				isHost = true;
			}
			if (token.find(L"%u") != token.npos) {
				isUser = true;
			}
			if (token.find(L"%p") != token.npos) {
				password = true;
			}
			if (token.find(L"%s") != token.npos) {
				isProxyUser = true;
			}
			if (token.find(L"%w") != token.npos) {
				isProxyPass = true;
			}

			// Skip account if empty
			bool isAccount = false;
			if (token.find(L"%a") != token.npos) {
				if (account.empty()) {
					continue;
				}
				else {
					isAccount = true;
				}
			}

			if (isProxyUser && !isHost && !isUser && proxyUser.empty()) {
				continue;
			}
			if (isProxyPass && !isHost && !isUser && proxyUser.empty()) {
				continue;
			}

			fz::replace_substrings(token, L"%s", proxyUser);
			fz::replace_substrings(token, L"%w", proxyPass);
			fz::replace_substrings(token, L"%h", host);
			fz::replace_substrings(token, L"%u", user);
			fz::replace_substrings(token, L"%a", account);
			// Pass will be replaced before sending to cope with interactve login

			if (!password) {
				fz::replace_substrings(token, L"%%", L"%");
			}

			t_loginCommand cmd;
			if (password || isProxyPass) {
				cmd.hide_arguments = true;
			}
			else {
				cmd.hide_arguments = false;
			}

			if (isUser && !password && !isAccount) {
				cmd.optional = false;
				cmd.type = loginCommandType::user;
			}
			else if (password && !isUser && !isAccount) {
				cmd.optional = true;
				cmd.type = loginCommandType::pass;
			}
			else if (isAccount && !isUser && !password) {
				cmd.optional = true;
				cmd.type = loginCommandType::account;
			}
			else {
				cmd.optional = false;
				cmd.type = loginCommandType::other;
			}

			cmd.command = token;

			loginSequence.push_back(cmd);
		}

		if (loginSequence.empty()) {
			LogMessage(MessageType::Error, _("Could not generate custom login sequence."));
			return false;
		}
	}
	else {
		LogMessage(MessageType::Error, _("Unknown FTP proxy type, cannot generate login sequence."));
		return false;
	}

	return true;
}

namespace {
bool HasFeature(std::wstring const& line, std::wstring const& feature)
{
	if (line == feature) {
		return true;
	}
	return line.size() > feature.size() && line.substr(0, feature.size()) == feature && line[feature.size()] == ' ';
}
}

void CFtpLogonOpData::ParseFeat(std::wstring line)
{
	fz::trim(line);
	std::wstring up = fz::str_toupper_ascii(line);

	if (HasFeature(up, L"UTF8")) {
		CServerCapabilities::SetCapability(currentServer_, utf8_command, yes);
	}
	else if (HasFeature(up, L"CLNT")) {
		CServerCapabilities::SetCapability(currentServer_, clnt_command, yes);
	}
	else if (HasFeature(up, L"MLSD")) {
		std::wstring facts;
		// FEAT output for MLST overrides MLSD
		if (CServerCapabilities::GetCapability(currentServer_, mlsd_command, &facts) != yes || facts.empty()) {
			if (line.size() > 5) {
				facts = line.substr(5);
			}
			else {
				facts.clear();
			}
		}
		CServerCapabilities::SetCapability(currentServer_, mlsd_command, yes, facts);

		// MLST/MLSD specs require use of UTC
		CServerCapabilities::SetCapability(currentServer_, timezone_offset, no);
	}
	else if (HasFeature(up, L"MLST")) {
		std::wstring facts;
		if (line.size() > 5) {
			facts = line.substr(5);
		}
		// FEAT output for MLST overrides MLSD
		if (facts.empty()) {
			if (CServerCapabilities::GetCapability(currentServer_, mlsd_command, &facts) != yes) {
				facts.clear();
			}
		}
		CServerCapabilities::SetCapability(currentServer_, mlsd_command, yes, facts);

		// MLST/MLSD specs require use of UTC
		CServerCapabilities::SetCapability(currentServer_, timezone_offset, no);
	}
	else if (HasFeature(up, L"MODE Z")) {
		CServerCapabilities::SetCapability(currentServer_, mode_z_support, yes);
	}
	else if (HasFeature(up, L"MFMT")) {
		CServerCapabilities::SetCapability(currentServer_, mfmt_command, yes);
	}
	else if (HasFeature(up, L"MDTM")) {
		CServerCapabilities::SetCapability(currentServer_, mdtm_command, yes);
	}
	else if (HasFeature(up, L"SIZE")) {
		CServerCapabilities::SetCapability(currentServer_, size_command, yes);
	}
	else if (HasFeature(up, L"TVFS")) {
		CServerCapabilities::SetCapability(currentServer_, tvfs_support, yes);
	}
	else if (HasFeature(up, L"REST STREAM")) {
		CServerCapabilities::SetCapability(currentServer_, rest_stream, yes);
	}
	else if (HasFeature(up, L"EPSV")) {
		CServerCapabilities::SetCapability(currentServer_, epsv_command, yes);
	}
}
