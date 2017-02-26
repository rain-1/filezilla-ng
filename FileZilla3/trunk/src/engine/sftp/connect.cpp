#include <filezilla.h>

#include "connect.h"
#include "event.h"
#include "input_thread.h"
#include "proxy.h"

#include <libfilezilla/process.hpp>

int CSftpConnectOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpConnectOpData::Send() in state %d", opState);
	
	switch (opState)
	{
	case connect_init:
		{
			auto executable = fz::to_native(engine_.GetOptions().GetOption(OPTION_FZSFTP_EXECUTABLE));
			if (executable.empty()) {
				executable = fzT("fzsftp");
			}
			LogMessage(MessageType::Debug_Verbose, L"Going to execute %s", executable);

			std::vector<fz::native_string> args = { fzT("-v") };
			if (engine_.GetOptions().GetOptionVal(OPTION_SFTP_COMPRESSION)) {
				args.push_back(fzT("-C"));
			}
			if (!controlSocket_.process_->spawn(executable, args)) {
				LogMessage(MessageType::Debug_Warning, L"Could not create process");
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;;
			}

			controlSocket_.input_thread_ = std::make_unique<CSftpInputThread>(controlSocket_, *controlSocket_.process_);
			if (!controlSocket_.input_thread_->spawn(engine_.GetThreadPool())) {
				LogMessage(MessageType::Debug_Warning, L"Thread creation failed");
				controlSocket_.input_thread_.reset();
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;;
			}
		}
		return FZ_REPLY_WOULDBLOCK;
	case connect_proxy:
		{
			int type;
			switch (engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE))
			{
			case CProxySocket::HTTP:
				type = 1;
				break;
			case CProxySocket::SOCKS5:
				type = 2;
				break;
			case CProxySocket::SOCKS4:
				type = 3;
				break;
			default:
				LogMessage(MessageType::Debug_Warning, L"Unsupported proxy type");
				return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
			}

			std::wstring cmd = fz::sprintf(L"proxy %d \"%s\" %d", type,
				engine_.GetOptions().GetOption(OPTION_PROXY_HOST),
				engine_.GetOptions().GetOptionVal(OPTION_PROXY_PORT));
			std::wstring user = engine_.GetOptions().GetOption(OPTION_PROXY_USER);
			if (!user.empty()) {
				cmd += L" \"" + user + L"\"";
			}

			std::wstring show = cmd;
			std::wstring pass = engine_.GetOptions().GetOption(OPTION_PROXY_PASS);
			if (!pass.empty()) {
				cmd += L" \"" + pass + L"\"";
				show += L" \"" + std::wstring(pass.size(), '*') + L"\"";
			}
			return controlSocket_.SendCommand(cmd, show);
		}
		break;
	case connect_keys:
		return controlSocket_.SendCommand(L"keyfile \"" + *(keyfile_++) + L"\"");
	case connect_open:
		return controlSocket_.SendCommand(fz::sprintf(L"open \"%s@%s\" %d", currentServer_.GetUser(), controlSocket_.ConvertDomainName(currentServer_.GetHost()), currentServer_.GetPort()));
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
}

int CSftpConnectOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpConnectOpData::ParseResponse() in state %d", opState);

	if (controlSocket_.result_ != FZ_REPLY_OK) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	switch (opState)
	{
	case connect_init:
		if (controlSocket_.response_ != fz::sprintf(L"fzSftp started, protocol_version=%d", FZSFTP_PROTOCOL_VERSION)) {
			LogMessage(MessageType::Error, _("fzsftp belongs to a different version of FileZilla"));
			return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;;
		}
		if (engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE) && !currentServer_.GetBypassProxy()) {
			opState = connect_proxy;
		}
		else if (keyfile_ != keyfiles_.cend()) {
			opState = connect_keys;
		}
		else {
			opState = connect_open;
		}
		break;
	case connect_proxy:
		if (keyfile_ != keyfiles_.cend()) {
			opState = connect_keys;
		}
		else {
			opState = connect_open;
		}
		break;
	case connect_keys:
		if (keyfile_ == keyfiles_.cend()) {
			opState = connect_open;
		}
		break;
	case connect_open:
		engine_.AddNotification(new CSftpEncryptionNotification(controlSocket_.m_sftpEncryptionDetails));
		return FZ_REPLY_OK;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown op state: %d", opState);
		return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_CONTINUE;
}