#include <filezilla.h>

#include "connect.h"
#include "event.h"
#include "input_thread.h"
#include "proxy.h"

#include <libfilezilla/process.hpp>
#include <libfilezilla/uri.hpp>

int CStorjConnectOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjConnectOpData::Send() in state %d", opState);

	switch (opState)
	{
	case connect_init:
		{
			auto executable = fz::to_native(engine_.GetOptions().GetOption(OPTION_FZSTORJ_EXECUTABLE));
			if (executable.empty()) {
				executable = fzT("fzstorj");
			}
			LogMessage(MessageType::Debug_Verbose, L"Going to execute %s", executable);

			std::vector<fz::native_string> args;
			if (!controlSocket_.process_->spawn(executable, args)) {
				LogMessage(MessageType::Debug_Warning, L"Could not create process");
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;;
			}

			controlSocket_.input_thread_ = std::make_unique<CStorjInputThread>(controlSocket_, *controlSocket_.process_);
			if (!controlSocket_.input_thread_->spawn(engine_.GetThreadPool())) {
				LogMessage(MessageType::Debug_Warning, L"Thread creation failed");
				controlSocket_.input_thread_.reset();
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;;
			}
		}
		return FZ_REPLY_WOULDBLOCK;
	case connect_timeout:
		return controlSocket_.SendCommand(fz::sprintf(L"timeout %d", engine_.GetOptions().GetOptionVal(OPTION_TIMEOUT)));
	case connect_proxy:
		{
			fz::uri proxy_uri;
			switch (engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE))
			{
			case 0:
				opState = connect_host;
				return FZ_REPLY_CONTINUE;
			case CProxySocket::HTTP:
				proxy_uri.scheme_ = "http";
				break;
			case CProxySocket::SOCKS5:
				proxy_uri.scheme_ = "socks5h";
				break;
			case CProxySocket::SOCKS4:
				proxy_uri.scheme_ = "socks4a";
				break;
			default:
				LogMessage(MessageType::Debug_Warning, L"Unsupported proxy type");
				return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
			}

			proxy_uri.host_ = fz::to_utf8(engine_.GetOptions().GetOption(OPTION_PROXY_HOST));
			proxy_uri.port_ = engine_.GetOptions().GetOptionVal(OPTION_PROXY_PORT);
			proxy_uri.user_ = fz::to_utf8(engine_.GetOptions().GetOption(OPTION_PROXY_USER));
			proxy_uri.pass_ = fz::to_utf8(engine_.GetOptions().GetOption(OPTION_PROXY_PASS));

			auto cmd = L"proxy " + fz::to_wstring(proxy_uri.to_string());
			proxy_uri.pass_.clear();
			auto show = L"proxy " + fz::to_wstring(proxy_uri.to_string());
			return controlSocket_.SendCommand(cmd, show);
		}
	case connect_host:
		return controlSocket_.SendCommand(fz::sprintf(L"host %s", currentServer_.Format(ServerFormat::with_optional_port)));
	case connect_user:
		return controlSocket_.SendCommand(fz::sprintf(L"user %s", currentServer_.GetUser()));
	case connect_pass:
		{
			std::wstring pass = credentials_.GetPass();
			size_t pos = pass.rfind('|');
			if (pos == std::wstring::npos) {
				LogMessage(MessageType::Error, _("Password or encryption key is not set"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			pass = pass.substr(0, pos);
			return controlSocket_.SendCommand(fz::sprintf(L"pass %s", pass), fz::sprintf(L"pass %s", std::wstring(pass.size(), '*')));
		}
	case connect_key:
		{
			std::wstring key = credentials_.GetPass();
			size_t pos = key.rfind('|');
			if (pos == std::wstring::npos) {
				LogMessage(MessageType::Error, _("Password or encryption key is not set"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			key = key.substr(pos + 1);
			return controlSocket_.SendCommand(fz::sprintf(L"key %s", key), fz::sprintf(L"key %s", std::wstring(key.size(), '*')));
		}
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
}

int CStorjConnectOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjConnectOpData::ParseResponse() in state %d", opState);

	if (controlSocket_.result_ != FZ_REPLY_OK) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	switch (opState)
	{
	case connect_init:
		if (controlSocket_.response_ != fz::sprintf(L"fzStorj started, protocol_version=%d", FZSTORJ_PROTOCOL_VERSION)) {
			LogMessage(MessageType::Error, _("fzstorj belongs to a different version of FileZilla"));
			return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
		}
		opState = connect_timeout;
		break;
	case connect_timeout:
		opState = connect_host;
		break;
	case connect_proxy:
		opState = connect_host;
		break;
	case connect_host:
		opState = connect_user;
		break;
	case connect_user:
		opState = connect_pass;
		break;
	case connect_pass:
		opState = connect_key;
		break;
	case connect_key:
		return FZ_REPLY_OK;
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown op state: %d", opState);
		return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_CONTINUE;
}
