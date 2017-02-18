#include <filezilla.h>

#include "request.h"

enum requestStates
{
	request_init = 0,
	request_wait_connect,
	request_send_header,
	request_send
};

int CHttpRequestOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::Send");

	switch (opState) {
	case request_init:
		{
			std::string host_header = request_.uri_.host_;
			if (request_.uri_.port_ != 0) {
				host_header += ':';
				host_header += fz::to_string(request_.uri_.port_);
			}
			request_.headers_["Host"] = host_header;
			request_.headers_["Connection"] = "close";
			request_.headers_["User-Agent"] = PACKAGE_STRING;

			opState = request_wait_connect;
			controlSocket_.InternalConnect(fz::to_wstring_from_utf8(request_.uri_.host_), request_.uri_.port_, request_.uri_.scheme_ == "https");
			return FZ_REPLY_CONTINUE;
		}
	case request_send_header:
		{
			// Assemble request and headers
			std::string command = fz::sprintf("%s %s HTTP/1.1\r\n", request_.verb_, request_.uri_.get_request());

			command += "\r\n";

			for (auto const& header : request_.headers_) {
				command += fz::sprintf("%s: %s\r\n", header.first, header.second);
			}

			command += "\r\n";

			// FIXME: Send body

			return controlSocket_.Send(command.c_str(), command.size());
		}
	default:
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpRequestOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::ParseResponse");
	return FZ_REPLY_INTERNALERROR;
}

int CHttpRequestOpData::SubcommandResult(int prevResult, COpData const& previousOperation)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::SubcommandResult");

	opState = request_send_header;
	return FZ_REPLY_CONTINUE;
}
