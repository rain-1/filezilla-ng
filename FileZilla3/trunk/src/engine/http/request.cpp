#include <filezilla.h>

#include "request.h"

#include <string.h>

#include "backend.h"

#include <libfilezilla/encode.hpp>


CHttpRequestOpData::CHttpRequestOpData(CHttpControlSocket & controlSocket, std::shared_ptr<HttpRequestResponseInterface> const& request)
	: COpData(PrivCommand::http_request)
	, CHttpOpData(controlSocket)
{
	opState = request_init | request_reading;

	request->request().flags_ = 0;
	request->response().flags_ = 0;

	requests_.emplace_back(request);
}

CHttpRequestOpData::CHttpRequestOpData(CHttpControlSocket & controlSocket, std::deque<std::shared_ptr<HttpRequestResponseInterface>> && requests)
	: COpData(PrivCommand::http_request)
	, CHttpOpData(controlSocket)
	, requests_(requests)
{
	for (auto & rr : requests_) {
		rr->request().flags_ = 0;
		rr->response().flags_ = 0;
	}
	opState = request_init | request_reading;
}

void CHttpRequestOpData::AddRequest(std::shared_ptr<HttpRequestResponseInterface> const& rr)
{
	if (!(opState & request_send_mask)) {
		bool wait = false;
		if (!requests_.empty()) {
			if (!requests_.back() && !read_state_.keep_alive_) {
				wait = true;
			}
			else if (requests_.back() && !(requests_.back()->request().keep_alive() || requests_.back()->response().keep_alive())) {
				wait = true;
			}
		}
		if (wait) {
			opState |= request_send_wait_for_read;
		}
		else {
			opState |= request_init;
			if (controlSocket_.m_pBackend) {
				controlSocket_.send_event<fz::socket_event>(controlSocket_.m_pBackend, fz::socket_event_flag::write, 0);
			}
		}
	}
	rr->request().flags_ = 0;
	rr->response().flags_ = 0;
	requests_.push_back(rr);
}

int CHttpRequestOpData::Send()
{
	LogMessage(MessageType::Debug_Debug, L"CHttpRequestOpData::Send() in state %d", opState);

	if (opState & request_init) {
		if (send_pos_ >= requests_.size()) {
			opState &= ~request_init;
			return FZ_REPLY_CONTINUE;
		}

		auto & rr = *requests_[send_pos_];
		auto & req = rr.request();

		int res = req.reset();
		if (res != FZ_REPLY_CONTINUE) {
			return res;
		}

		res = rr.response().reset();
		if (res != FZ_REPLY_CONTINUE) {
			return res;
		}

		if (req.verb_.empty()) {
			LogMessage(MessageType::Debug_Warning, L"No request verb");
			return FZ_REPLY_INTERNALERROR;
		}
		std::string host_header = req.uri_.host_;
		if (req.uri_.port_ != 0) {
			host_header += ':';
			host_header += fz::to_string(req.uri_.port_);
		}
		req.headers_["Host"] = host_header;
		auto pos = req.headers_.find("Connection");
		if (pos == req.headers_.end()) {
			// TODO: consider making keep-alive the default
			req.headers_["Connection"] = "close";
		}
		req.headers_["User-Agent"] = fz::replaced_substrings(PACKAGE_STRING, " ", "/");

		opState &= ~request_init;
		opState |= request_wait_connect;
		return FZ_REPLY_CONTINUE;
	}

	if (opState & request_send_wait_for_read) {
		if (send_pos_ > 0) {
			return FZ_REPLY_WOULDBLOCK;
		}

		opState &= ~request_send_wait_for_read;
		opState |= request_init;

		return FZ_REPLY_CONTINUE;
	}

	if (opState & request_wait_connect) {
		if (send_pos_ >= requests_.size()) {
			LogMessage(MessageType::Debug_Warning, L"Bad state: opState & request_wait_connect yet send_pos_ >= requests_.size()");
			return FZ_REPLY_INTERNALERROR;
		}

		auto & rr = *requests_[send_pos_];
		auto & req = rr.request();

		auto const& uri = req.uri_;
		int res = controlSocket_.InternalConnect(fz::to_wstring_from_utf8(uri.host_), uri.port_, uri.scheme_ == "https", !send_pos_);
		if (res == FZ_REPLY_OK) {
			opState &= ~request_wait_connect;
			opState |= request_send;
			res = FZ_REPLY_CONTINUE;
		}
		return res;
	}

	if (opState & request_send) {
		if (send_pos_ >= requests_.size()) {
			opState &= ~request_send;
		}
		else {
			auto & req = requests_[send_pos_]->request();
			if (!(req.flags_ & HttpRequest::flag_sent_header)) {
				if (req.body_) {
					req.set_content_length(req.body_->size());
				}
				else {
					if (req.verb_ == "GET" || req.verb_ == "HEAD" || req.verb_ == "OPTIONS") {
						req.headers_.erase("Content-Length");
					}
					else {
						req.set_content_length(0);
					}
				}

				auto const cl = req.get_header("Content-Length");
				if (!cl.empty()) {
					int64_t requestContentLength = fz::to_integral<int64_t>(cl, -1);
					if (requestContentLength < 0) {
						LogMessage(MessageType::Error, _("Malformed request header: %s"), _("Invalid Content-Length"));
						return FZ_REPLY_INTERNALERROR;
					}
					dataToSend_ = static_cast<uint64_t>(requestContentLength);
				}

				// Assemble request and headers
				std::string command = fz::sprintf("%s %s HTTP/1.1", req.verb_, req.uri_.get_request());
				LogMessage(MessageType::Command, "%s", command);
				command += "\r\n";

				for (auto const& header : req.headers_) {
					std::string line = fz::sprintf("%s: %s", header.first, header.second);
					if (header.first == "Authorization") {
						LogMessage(MessageType::Command, "%s: %s", header.first, std::string(header.second.size(), '*'));
					}
					else {
						LogMessage(MessageType::Command, "%s", line);
					}
					command += line + "\r\n";
				}

				command += "\r\n";

				req.flags_ |= HttpRequest::flag_sent_header;
				if (!req.body_) {
					LogMessage(MessageType::Debug_Info, "Finished sending request header. Request has no body");
					opState &= ~request_send;
					++send_pos_;
					if (send_pos_ < requests_.size()) {
						if (!req.keep_alive()) {
							opState |= request_send_wait_for_read;
							LogMessage(MessageType::Debug_Info, L"Request did not ask for keep-alive. Waiting for response to finish before sending next request a new connection.");
						}
						else {
							opState |= request_init;
						}
					}
				}
				else {
					LogMessage(MessageType::Debug_Info, "Finish sending request header.");
				}

				auto result = controlSocket_.Send(command.c_str(), command.size());
				if (result == FZ_REPLY_WOULDBLOCK && !controlSocket_.sendBuffer_) {
					result = FZ_REPLY_CONTINUE;
				}

				return result;
			}
			else {
				int const chunkSize = 65536;

				while (dataToSend_ || controlSocket_.sendBuffer_) {
					if (!controlSocket_.sendBuffer_) {
						unsigned int len = chunkSize;
						if (chunkSize > dataToSend_) {
							len = static_cast<unsigned int>(dataToSend_);
						}
						int res = req.body_->data_request(controlSocket_.sendBuffer_.get(len), len);
						if (res != FZ_REPLY_CONTINUE) {
							return res;
						}
						if (len > dataToSend_) {
							LogMessage(MessageType::Debug_Warning, L"req.body_ returned too much data");
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

				LogMessage(MessageType::Debug_Info, "Finished sending request body");

				req.flags_ |= HttpRequest::flag_sent_body;

				opState &= ~request_send;
				++send_pos_;

				if (send_pos_ < requests_.size()) {
					if (!req.keep_alive()) {
						opState |= request_send_wait_for_read;
						LogMessage(MessageType::Debug_Info, L"Request did not ask for keep-alive. Waiting for response to finish before sending next request a new connection.");
					}
					else {
						opState |= request_init;
					}
				}
				return FZ_REPLY_CONTINUE;
			}
		}
	}
	
	if (opState & request_reading) {
		return FZ_REPLY_WOULDBLOCK;
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

	if (opState & request_wait_connect) {
		opState &= ~request_wait_connect;
		opState |= request_send;
	}

	return FZ_REPLY_CONTINUE;
}

int CHttpRequestOpData::ParseReceiveBuffer(bool eof)
{
	auto & shared_response = requests_.front();
	if (shared_response) {
		auto & request = shared_response->request();
		if (!(request.flags_ & HttpRequest::flag_sent_header)) {
			if (eof) {
				LogMessage(MessageType::Debug_Verbose, L"Socket closed before request got sent");
				LogMessage(MessageType::Error, _("Connection closed by server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			else if (!recv_buffer_.empty()) {
				LogMessage(MessageType::Error, _("Server sent data even before request headers were sent"));
				return FZ_REPLY_ERROR;
			}
		}
		auto & response = shared_response->response();

		if (!response.got_header()) {
			if (eof) {
				LogMessage(MessageType::Debug_Verbose, L"Socket closed before headers got received");
				LogMessage(MessageType::Error, _("Connection closed by server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}

			return ParseHeader();
		}
	}

	if (read_state_.transfer_encoding_ == chunked) {
		if (eof) {
			LogMessage(MessageType::Debug_Verbose, L"Socket closed, chunk incomplete");
			LogMessage(MessageType::Error, _("Connection closed by server"));
			return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
		}
		return ParseChunkedData();
	}
	else {
		if (eof) {
			assert(recv_buffer_.empty());

			if (read_state_.responseContentLength_ != -1 && read_state_.receivedData_ != read_state_.responseContentLength_) {
				LogMessage(MessageType::Debug_Verbose, L"Socket closed, content length not reached");
				LogMessage(MessageType::Error, _("Connection closed by server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			else {
				if (shared_response) {
					shared_response->response().flags_ |= HttpResponse::flag_got_body;
				}
				return FZ_REPLY_OK;
			}
		}
		else {
			size_t size = recv_buffer_.size();
			if (!size) {
				return FZ_REPLY_WOULDBLOCK;
			}

			if (read_state_.responseContentLength_ != -1 && read_state_.receivedData_ + static_cast<int64_t>(size) > read_state_.responseContentLength_) {
				size = static_cast<size_t>(read_state_.responseContentLength_ - read_state_.receivedData_);
			}

			int res = ProcessData(recv_buffer_.get(), size);
			recv_buffer_.consume(size);
			return res;
		}
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpRequestOpData::OnReceive()
{
	while (controlSocket_.socket_) {
		const fz::socket::socket_state state = controlSocket_.socket_->get_state();
		if (state != fz::socket::connected && state != fz::socket::closing) {
			return FZ_REPLY_WOULDBLOCK;
		}

		int error;
		size_t const recv_size = 1024 * 64;
		int read = controlSocket_.m_pBackend->Read(recv_buffer_.get(recv_size), recv_size, error);
		if (read <= -1) {
			if (error != EAGAIN) {
				LogMessage(MessageType::Error, _("Could not read from socket: %s"), fz::socket::error_description(error));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			return FZ_REPLY_WOULDBLOCK;
		}
		recv_buffer_.add(static_cast<size_t>(read));

		controlSocket_.SetActive(CFileZillaEngine::recv);

		while (!requests_.empty()) {
			assert(!requests_.empty());
			int res = ParseReceiveBuffer(read == 0);
			if (res == FZ_REPLY_WOULDBLOCK) {
				break;
			}

			if (res == FZ_REPLY_OK) {
				LogMessage(MessageType::Debug_Info, L"Finished a response");
				requests_.pop_front();
				--send_pos_;

				bool keep_alive = read_state_.keep_alive_;
				if (!keep_alive) {
					if (!recv_buffer_.empty()) {
						LogMessage(MessageType::Error, _("Malformed response: %s"), _("Server sent too much data."));
						return FZ_REPLY_ERROR;
					}

					controlSocket_.ResetSocket();
				}

				read_state_ = read_state();

				if (requests_.empty()) {
					LogMessage(MessageType::Debug_Info, L"Done reading last response");
					opState &= ~request_reading;

					if (!recv_buffer_.empty()) {
						LogMessage(MessageType::Error, _("Malformed response: %s"), _("Server sent too much data."));
						return FZ_REPLY_ERROR;
					}
					return FZ_REPLY_OK;
				}

				if (!keep_alive) {
					send_pos_ = 0;
					opState = request_init | request_reading;
					return FZ_REPLY_CONTINUE;
				}
			}
			else if (res != FZ_REPLY_CONTINUE) {
				return res;
			}
		}

		if (requests_.empty() && !recv_buffer_.empty()) {
			LogMessage(MessageType::Error, _("Malformed response: %s"), _("Server sent too much data."));
			return FZ_REPLY_ERROR;
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

		auto & response = requests_.front()->response();
		if (!response.got_code()) {
			if (recv_buffer_.size() < 15 || memcmp(recv_buffer_.get(), "HTTP/1.", 7)) {
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

			response.code_ = (recv_buffer_[9] - '0') * 100 + (recv_buffer_[10] - '0') * 10 + recv_buffer_[11] - '0';
			response.flags_ |= HttpResponse::flag_got_code;
		}
		else {
			if (!i) {
				recv_buffer_.consume(2);

				// End of header
				return ProcessCompleteHeader();
			}

			std::string line(recv_buffer_.get(), recv_buffer_.get() + i);

			auto delim_pos = line.find(':');
			if (delim_pos == std::string::npos || !delim_pos) {
				LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Invalid line"));
				return FZ_REPLY_ERROR;
			}

			std::string value;
			auto value_start = line.find_first_not_of(" \t", delim_pos + 1);
			if (value_start != std::string::npos) {
				int value_stop = line.find_last_not_of(" \t"); // Cannot fail
				value = line.substr(value_start, value_stop - value_start + 1);
			}

			auto & header = response.headers_[line.substr(0, delim_pos)];
			if (header.empty()) {
				header = value;
			}
			else if (!value.empty()) {
				header += ", " + value;
			}
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

	auto & srr = requests_.front();
	auto & request = srr->request();
	auto & response = srr->response();
	if (response.code_ == 100) {
		// 100 Continue header. Ignore it and start over.
		response.reset();
		return FZ_REPLY_CONTINUE;
	}

	response.flags_ |= HttpResponse::flag_got_header;
	if (request.verb_ == "HEAD" || response.code_prohobits_body()) {
		response.flags_ |= HttpResponse::flag_no_body;
	}

	auto const te = fz::str_tolower_ascii(response.get_header("Transfer-Encoding"));
	if (te == "chunked") {
		read_state_.transfer_encoding_ = chunked;
	}
	else if (te.empty() || te == "identity") {
		read_state_.transfer_encoding_ = identity;
	}
	else {
		LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Unknown transfer encoding"));
		return FZ_REPLY_ERROR;
	}
	

	int64_t length{-1};
	auto const cl = response.get_header("Content-Length");
	if (!cl.empty()) {
		length = fz::to_integral<int64_t>(cl, -1);
		if (length < 0) {
			LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Invalid Content-Length"));
			return FZ_REPLY_ERROR;
		}
	}

	if (response.no_body()) {
		read_state_.responseContentLength_ = 0;
	}
	else {
		read_state_.responseContentLength_ = length;
	}

	read_state_.keep_alive_ = response.keep_alive() && request.keep_alive();

	int res = FZ_REPLY_CONTINUE;
	if (response.on_header_) {
		res = response.on_header_(srr);

		if (res != FZ_REPLY_CONTINUE) {
			response.flags_ |= HttpResponse::flag_ignore_body;
			if (res == FZ_REPLY_OK) {
				// Clear the pointer, we no longer need the request to finish, all needed information is in read_state_
				srr.reset();
				res = FZ_REPLY_CONTINUE;
			}
		}
	}

	if (res == FZ_REPLY_CONTINUE) {
		if (!read_state_.responseContentLength_) {
			res = FZ_REPLY_OK;
		}
	}

	return res;
}

int CHttpRequestOpData::ParseChunkedData()
{
	while (!recv_buffer_.empty()) {
		if (read_state_.chunk_data_.size != 0) {
			size_t dataLen = recv_buffer_.size();
			if (read_state_.chunk_data_.size < recv_buffer_.size()) {
				dataLen = static_cast<size_t>(read_state_.chunk_data_.size);
			}
			int res = ProcessData(recv_buffer_.get(), dataLen);
			if (res != FZ_REPLY_CONTINUE) {
				return res;
			}
			recv_buffer_.consume(dataLen);
			read_state_.chunk_data_.size -= dataLen;

			if (read_state_.chunk_data_.size == 0) {
				read_state_.chunk_data_.terminateChunk = true;
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
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Null character in line"));
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

		if (read_state_.chunk_data_.terminateChunk) {
			if (i) {
				// The chunk data has to end with CRLF. If i is nonzero,
				// it didn't end with just CRLF.
				LogMessage(MessageType::Debug_Debug, L"%u characters preceeding line-ending with value %s", i, fz::hex_encode<std::string>(std::string(recv_buffer_.get(), recv_buffer_.get() + recv_buffer_.size())));
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Chunk data improperly terminated"));
				return FZ_REPLY_ERROR;
			}
			read_state_.chunk_data_.terminateChunk = false;
		}
		else if (read_state_.chunk_data_.getTrailer) {
			if (!i) {
				// We're done
				recv_buffer_.consume(2);

				auto & response = requests_.front();
				if (response) {
					response->response().flags_ |= HttpResponse::flag_got_body;
				}
				return FZ_REPLY_OK;
			}

			// Ignore the trailer
		}
		else {
			// Read chunk size
			unsigned char const* end = recv_buffer_.get() + i;
			for (unsigned char* q = recv_buffer_.get(); q != end && *q != ';' && *q != ' '; ++q) {
				read_state_.chunk_data_.size *= 16;
				if (*q >= '0' && *q <= '9') {
					read_state_.chunk_data_.size += *q - '0';
				}
				else if (*q >= 'A' && *q <= 'F') {
					read_state_.chunk_data_.size += *q - 'A' + 10;
				}
				else if (*q >= 'a' && *q <= 'f') {
					read_state_.chunk_data_.size += *q - 'a' + 10;
				}
				else {
					// Invalid size
					LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Invalid chunk size"));
					return FZ_REPLY_ERROR;
				}
			}
			if (!read_state_.chunk_data_.size) {
				read_state_.chunk_data_.getTrailer = true;
			}
		}

		recv_buffer_.consume(i + 2);
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::ProcessData(unsigned char* data, unsigned int len)
{
	read_state_.receivedData_ += len;

	int res = FZ_REPLY_CONTINUE;

	auto & shared_response = requests_.front();
	if (shared_response) {
		auto & response = shared_response->response();
		if (response.on_data_ && !(response.flags_ & HttpResponse::flag_ignore_body)) {
			res = response.on_data_(data, len);
		}
	}

	if (res == FZ_REPLY_CONTINUE && read_state_.receivedData_ == read_state_.responseContentLength_) {
		if (shared_response) {
			shared_response->response().flags_ |= HttpResponse::flag_got_body;
		}
		res = FZ_REPLY_OK;
	}

	return res;
}

int CHttpRequestOpData::OnClose()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::OnClose()");

	if (send_pos_ < requests_.size()) {
		LogMessage(MessageType::Debug_Verbose, L"Socket closed before all data got sent");
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	if (!requests_.empty()) {

		auto & response = requests_.front();
		if (response && !response->response().got_header()) {
			LogMessage(MessageType::Debug_Verbose, L"Socket closed before headers got received");
			return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
		}

		if (read_state_.transfer_encoding_ == chunked) {
			if (!read_state_.chunk_data_.getTrailer) {
				LogMessage(MessageType::Debug_Verbose, L"Socket closed, chunk incomplete");
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
		}
		else {
			if (read_state_.responseContentLength_ != -1 && read_state_.receivedData_ != read_state_.responseContentLength_) {
				LogMessage(MessageType::Debug_Verbose, L"Socket closed, content length not reached");
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
		}

		if (response) {
			response->response().flags_ |= HttpResponse::flag_got_body;
		}
		requests_.pop_front();
		--send_pos_;
		read_state_ = read_state();

		if (!requests_.empty()) {
			if (!recv_buffer_.empty()) {
				LogMessage(MessageType::Error, _("Malformed response: %s"), _("Server sent too much data."));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}

			LogMessage(MessageType::Debug_Verbose, L"Socket closed before responses got fully received. Re-issuing");
			send_pos_ = 0;
			opState = request_init | request_reading;
			controlSocket_.ResetSocket();
			return FZ_REPLY_CONTINUE;
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
	else if (!recv_buffer_.empty()) {
		LogMessage(MessageType::Debug_Verbose, L"Closing connection, the receive buffer isn't empty but at %d", recv_buffer_.size());
		controlSocket_.ResetSocket();
	}
	else {
		if (controlSocket_.m_pBackend) {
			controlSocket_.send_event<fz::socket_event>(controlSocket_.m_pBackend, fz::socket_event_flag::read, 0);
		}
	}

	return result;
}
