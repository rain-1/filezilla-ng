#include <filezilla.h>

#include "request.h"

#include <string.h>

#include "backend.h"

#include <libfilezilla/encode.hpp>

int CHttpRequestOpData::Send()
{
	LogMessage((opState == request_send) ? MessageType::Debug_Debug : MessageType::Debug_Verbose, L"CHttpRequestOpData::Send() in state %d", opState);

	switch (opState) {
	case request_init:
		{
			if (request_.verb_.empty()) {
				LogMessage(MessageType::Debug_Warning, L"No request verb");
				return FZ_REPLY_INTERNALERROR;
			}
			std::string host_header = request_.uri_.host_;
			if (request_.uri_.port_ != 0) {
				host_header += ':';
				host_header += fz::to_string(request_.uri_.port_);
			}
			request_.headers_["Host"] = host_header;
			auto pos = request_.headers_.find("Connection");
			if (pos == request_.headers_.end()) {
				request_.headers_["Connection"] = "close";
			}
			request_.headers_["User-Agent"] = fz::replaced_substrings(PACKAGE_STRING, " ", "/");

			auto const cl = request_.get_header("Content-Length");
			if (!cl.empty()) {
				int64_t requestContentLength = fz::to_integral<int64_t>(cl, -1);
				if (requestContentLength < 0) {
					LogMessage(MessageType::Error, _("Malformed request header: %s"), _("Invalid Content-Length"));
					return FZ_REPLY_ERROR;
				}
				dataToSend_ = static_cast<uint64_t>(requestContentLength);
			}

			if ((dataToSend_ > 0) && !request_.data_request_) {
				LogMessage(MessageType::Debug_Warning, L"(dataToSend_ > 0) && !request_.data_request_");
				return FZ_REPLY_INTERNALERROR;
			}

			response_.code_ = 0;
			response_.headers_.clear();

			opState = request_wait_connect;

			controlSocket_.InternalConnect(fz::to_wstring_from_utf8(request_.uri_.host_), request_.uri_.port_, request_.uri_.scheme_ == "https");
			if (controlSocket_.is_reusing_) {
				opState = request_send_header;
			}
			return FZ_REPLY_CONTINUE;
		}
	case request_send_header:
		{
			// Assemble request and headers
			std::string command = fz::sprintf("%s %s HTTP/1.1", request_.verb_, request_.uri_.get_request());
			LogMessage(MessageType::Command, "%s", command);
			command += "\r\n";

			for (auto const& header : request_.headers_) {
				std::string line = fz::sprintf("%s: %s", header.first, header.second);
				LogMessage(MessageType::Command, "%s", line);
				command += line + "\r\n";
			}

			command += "\r\n";

			if (request_.data_request_) {
				opState = request_send;
			}
			else {
				opState = request_read;
			}

			auto result = controlSocket_.Send(command.c_str(), command.size());
			if (result == FZ_REPLY_WOULDBLOCK && opState == request_send && !controlSocket_.sendBuffer_) {
				result = FZ_REPLY_CONTINUE;
			}
			return result;
		}
	case request_send:
		{
			int const chunkSize = 65536;

			while (dataToSend_ || controlSocket_.sendBuffer_) {
				if (!controlSocket_.sendBuffer_) {
					unsigned int len = chunkSize;
					if (chunkSize > dataToSend_) {
						len = static_cast<unsigned int>(dataToSend_);
					}
					int res = request_.data_request_(controlSocket_.sendBuffer_.get(len), len);
					if (res != FZ_REPLY_CONTINUE) {
						return res;
					}
					if (len > dataToSend_) {
						LogMessage(MessageType::Debug_Warning, L"request_.data_request_ returned too much data");
						return FZ_REPLY_INTERNALERROR;
					}

					controlSocket_.sendBuffer_.add(len);
					dataToSend_ -= len;
				}

				int error;
				int written = controlSocket_.m_pBackend->Write(controlSocket_.sendBuffer_.get(), controlSocket_.sendBuffer_.size(), error);
				if (written < 0) {
					if (error != EAGAIN) {
						LogMessage(MessageType::Error, _("Could not write to socket: %s"), fz::socket::error_description(error));
						LogMessage(MessageType::Error, _("Disconnected from server"));
						return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
					}
					return FZ_REPLY_WOULDBLOCK;
				}

				if (written) {
					controlSocket_.SetActive(CFileZillaEngine::send);

					controlSocket_.sendBuffer_.consume(static_cast<size_t>(written));
				}
			}
			opState = request_read;
			return FZ_REPLY_WOULDBLOCK;
		}
		return FZ_REPLY_WOULDBLOCK;
	case request_read:
		return FZ_REPLY_WOULDBLOCK;
	default:
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpRequestOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::ParseResponse() in state %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CHttpRequestOpData::SubcommandResult(int, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::SubcommandResult() in state %d", opState);

	opState = request_send_header;
	return FZ_REPLY_CONTINUE;
}

int CHttpRequestOpData::OnReceive()
{
	while (controlSocket_.socket_) {
		const fz::socket::socket_state state = controlSocket_.socket_->get_state();
		if (state != fz::socket::connected && state != fz::socket::closing) {
			return FZ_REPLY_WOULDBLOCK;
		}

		int error;
		size_t const recv_size = 1024 * 16;
		int read = controlSocket_.m_pBackend->Read(recv_buffer_.get(recv_size), recv_size, error);
		if (read <= -1) {
			if (error != EAGAIN) {
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			return FZ_REPLY_WOULDBLOCK;
		}
		recv_buffer_.add(static_cast<size_t>(read));

		controlSocket_.SetActive(CFileZillaEngine::recv);

		if (!got_header_) {
			if (!read) {
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}

			int res = ParseHeader();
			if (res != FZ_REPLY_WOULDBLOCK) {
				return res;
			}
		}
		else if (transfer_encoding_ == chunked) {
			if (!read) {
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			int res = ParseChunkedData();
			if (res != FZ_REPLY_WOULDBLOCK) {
				return res;
			}
		}
		else {
			if (!read) {
				assert(recv_buffer_.empty());

				if (responseContentLength_ != -1 && receivedData_ != responseContentLength_) {
					return FZ_REPLY_ERROR;
				}
				else {
					return FZ_REPLY_OK;
				}
			}
			else {
				int res = ProcessData(recv_buffer_.get(), recv_buffer_.size());
				recv_buffer_.clear();
				if (res != FZ_REPLY_CONTINUE) {
					return res;
				}
			}
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::ParseHeader()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::ParseHeader()");

	// Parse the HTTP header.
	// We do just the neccessary parsing and silently ignore most header fields
	// The calling operation is responsible for things like redirect parsing.
	for (;;) {
		// Find line ending
		size_t i = 0;
		for (i = 0; (i + 1) < recv_buffer_.size(); ++i) {
			if (recv_buffer_[i] == '\r') {
				if (recv_buffer_[i + 1] != '\n') {
					LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Server not sending proper line endings"));
					return FZ_REPLY_ERROR;
				}
				break;
			}
			if (!recv_buffer_[i]) {
				LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Null character in line"));
				return FZ_REPLY_ERROR;
			}
		}
		if ((i + 1) >= recv_buffer_.size()) {
			size_t const max_line_size = 8192;
			if (recv_buffer_.size() >= max_line_size) {
				LogMessage(MessageType::Error, _("Too long header line"));
				return FZ_REPLY_ERROR;
			}
			return FZ_REPLY_WOULDBLOCK;
		}

		std::wstring wline = fz::to_wstring_from_utf8(reinterpret_cast<char const*>(recv_buffer_.get()), i);
		if (wline.empty()) {
			wline = fz::to_wstring(std::string(recv_buffer_.get(), recv_buffer_.get() + i));
		}
		if (!wline.empty()) {
			controlSocket_.LogMessageRaw(MessageType::Response, wline);
		}

		if (!response_.code_) {
			if (recv_buffer_.size() < 16 || memcmp(recv_buffer_.get(), "HTTP/1.", 7)) {
				// Invalid HTTP Status-Line
				LogMessage(MessageType::Error, _("Invalid HTTP Response"));
				return FZ_REPLY_ERROR;
			}

			if (recv_buffer_[9] < '1' || recv_buffer_[9] > '5' ||
				recv_buffer_[10] < '0' || recv_buffer_[10] > '9' ||
				recv_buffer_[11] < '0' || recv_buffer_[11] > '9')
			{
				// Invalid response code
				LogMessage(MessageType::Error, _("Invalid response code"));
				return FZ_REPLY_ERROR;
			}

			response_.code_ = (recv_buffer_[9] - '0') * 100 + (recv_buffer_[10] - '0') * 10 + recv_buffer_[11] - '0';
		}
		else {
			if (!i) {
				recv_buffer_.consume(2);

				// End of header
				int res = ProcessCompleteHeader();
				if (res != FZ_REPLY_CONTINUE) {
					return res;
				}

				if (!got_header_) {
					// In case we got 100 Continue
					continue;
				}

				if (transfer_encoding_ == chunked) {
					return ParseChunkedData();
				}
				else {
					if (recv_buffer_.empty()) {
						if (!responseContentLength_) {
							opState = request_done;
							return FZ_REPLY_OK;
						}
						else {
							return FZ_REPLY_WOULDBLOCK;
						}
					}

					int res = ProcessData(recv_buffer_.get(), recv_buffer_.size());
					recv_buffer_.clear();
					if (res != FZ_REPLY_CONTINUE) {
						return res;
					}
					return FZ_REPLY_WOULDBLOCK;
				}
			}

			std::string line(recv_buffer_.get(), recv_buffer_.get() + i);

			auto pos = line.find(": ");
			if (pos == std::string::npos || !pos) {
				LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Invalid line"));
				return FZ_REPLY_ERROR;
			}

			response_.headers_[line.substr(0, pos)] = line.substr(pos + 2);
		}

		recv_buffer_.consume(i + 2);

		if (recv_buffer_.empty()) {
			break;
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::ProcessCompleteHeader()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::ParseHeader()");

	if (response_.code_ == 100) {
		// 100 Continue header. Ignore it and start over.
		response_.code_ = 0;
		response_.headers_.clear();
		return FZ_REPLY_CONTINUE;
	}

	got_header_ = true;

	auto const te = fz::str_tolower_ascii(response_.get_header("Transfer-Encoding"));
	if (te == "chunked") {
		transfer_encoding_ = chunked;
	}
	else if (te == "identity") {
		transfer_encoding_ = identity;
	}
	else if (!te.empty()) {
		LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Unknown transfer encoding"));
		return FZ_REPLY_ERROR;
	}
	
	auto const cl = response_.get_header("Content-Length");
	if (!cl.empty()) {
		responseContentLength_ = fz::to_integral<int64_t>(cl, -1);
		if (responseContentLength_ < 0) {
			LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Invalid Content-Length"));
			return FZ_REPLY_ERROR;
		}
	}

	int res = FZ_REPLY_CONTINUE;
	if (response_.on_header_) {
		res = response_.on_header_();
	}

	if (res == FZ_REPLY_CONTINUE) {
		if (request_.verb_ == "HEAD" || response_.code_prohobits_body()) {
			if (!recv_buffer_.empty()) {
				LogMessage(MessageType::Error, _("Malformed response: Server sent response body with a request verb or response code that disallows a response body."));
				return FZ_REPLY_ERROR;
			}
			else {
				opState = request_done;
				return FZ_REPLY_OK;
			}
		}
	}

	return res;
}

int CHttpRequestOpData::ParseChunkedData()
{
	while (!recv_buffer_.empty()) {
		if (chunk_data_.size != 0) {
			size_t dataLen = recv_buffer_.size();
			if (chunk_data_.size < recv_buffer_.size()) {
				dataLen = chunk_data_.size;
			}
			int res = ProcessData(recv_buffer_.get(), dataLen);
			if (res != FZ_REPLY_CONTINUE) {
				return res;
			}
			recv_buffer_.consume(dataLen);
			chunk_data_.size -= dataLen;

			if (chunk_data_.size == 0) {
				chunk_data_.terminateChunk = true;
			}
		}

		// Find line ending
		size_t i = 0;
		for (i = 0; (i + 1) < recv_buffer_.size(); ++i) {
			if (recv_buffer_[i] == '\r') {
				if (recv_buffer_[i + 1] != '\n') {
					LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Wrong line endings"));
					return FZ_REPLY_ERROR;
				}
				break;
			}
			if (!recv_buffer_[i]) {
				LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Null character in line"));
				return FZ_REPLY_ERROR;
			}
		}
		if ((i + 1) >= recv_buffer_.size()) {
			size_t const max_line_size = 8192;
			if (recv_buffer_.size() >= max_line_size) {
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Line length exceeded"));
				return FZ_REPLY_ERROR;
			}
			break;
		}

		if (chunk_data_.terminateChunk) {
			if (i) {
				// The chunk data has to end with CRLF. If i is nonzero,
				// it didn't end with just CRLF.
				LogMessage(MessageType::Debug_Debug, L"%u characters preceeding line-ending with value %s", i, fz::hex_encode<std::string>(std::string(recv_buffer_.get(), recv_buffer_.get() + recv_buffer_.size())));
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Chunk data improperly terminated"));
				return FZ_REPLY_ERROR;
			}
			chunk_data_.terminateChunk = false;
		}
		else if (chunk_data_.getTrailer) {
			if (!i) {
				// We're done
				opState = request_done;

				recv_buffer_.consume(2);

				return FZ_REPLY_OK;
			}

			// Ignore the trailer
		}
		else {
			// Read chunk size
			unsigned char const* end = recv_buffer_.get() + i;
			for (unsigned char* q = recv_buffer_.get(); q != end && *q != ';' && *q != ' '; ++q) {
				chunk_data_.size *= 16;
				if (*q >= '0' && *q <= '9') {
					chunk_data_.size += *q - '0';
				}
				else if (*q >= 'A' && *q <= 'F') {
					chunk_data_.size += *q - 'A' + 10;
				}
				else if (*q >= 'a' && *q <= 'f') {
					chunk_data_.size += *q - 'a' + 10;
				}
				else {
					// Invalid size
					LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Invalid chunk size"));
					return FZ_REPLY_ERROR;
				}
			}
			if (!chunk_data_.size) {
				chunk_data_.getTrailer = true;
			}
		}

		recv_buffer_.consume(i + 2);
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::ProcessData(unsigned char* data, unsigned int len)
{
	receivedData_ += len;
	if (responseContentLength_ != -1 && receivedData_ > responseContentLength_) {
		LogMessage(MessageType::Error, _("Malformed response body: %s"), _("Server sent too much data."));
		return FZ_REPLY_ERROR;
	}

	int res = FZ_REPLY_CONTINUE;
	if (response_.on_data_) {
		res = response_.on_data_(data, len);
	}

	if (res == FZ_REPLY_CONTINUE && receivedData_ == responseContentLength_) {
		opState = request_done;
		res = FZ_REPLY_OK;
	}

	return res;
}

int CHttpRequestOpData::OnClose()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::OnClose()");

	if (dataToSend_) {
		LogMessage(MessageType::Debug_Verbose, L"Socket closed before all data got sent");
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	if (!got_header_) {
		LogMessage(MessageType::Debug_Verbose, L"Socket closed before headers got received");
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	if (transfer_encoding_ == chunked) {
		if (!chunk_data_.getTrailer) {
			LogMessage(MessageType::Debug_Verbose, L"Socket closed, chunk incomplete");
			return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
		}
	}
	else {
		if (responseContentLength_ != -1 && receivedData_ != responseContentLength_) {
			LogMessage(MessageType::Debug_Verbose, L"Socket closed, content length not reached");
			return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
		}
	}

	return FZ_REPLY_OK;
}

int CHttpRequestOpData::Reset(int result)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::Reset(%d) in state %d", result, opState);

	if (result != FZ_REPLY_OK) {
		controlSocket_.ResetSocket();
	}
	else if (opState != request_done) {
		controlSocket_.ResetSocket();
	}
	else if (response_.get_header("Connection") == "close" || request_.get_header("Connection") == "close") {
		if (request_.get_header("Connection") != "close") {
			LogMessage(MessageType::Debug_Verbose, L"Server closed connection despite request to use keep-alive");
		}
		controlSocket_.ResetSocket();
	}
	else if (!recv_buffer_.empty()) {
		LogMessage(MessageType::Debug_Verbose, L"Closing connection, the receive buffer isn't empty but at %d", recv_buffer_.size());
		controlSocket_.ResetSocket();
	}
	else {
		controlSocket_.send_event<fz::socket_event>(controlSocket_.m_pBackend, fz::socket_event_flag::read, 0);
	}

	return result;
}
