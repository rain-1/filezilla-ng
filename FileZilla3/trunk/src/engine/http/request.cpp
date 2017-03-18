#include <filezilla.h>

#include "request.h"

#include <string.h>

#include "backend.h"

int CHttpRequestOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::Send() in state %d", opState);

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

			if ((dataToSend_ > 0) != (request_.data_request_ != 0)) {
				LogMessage(MessageType::Debug_Warning, L"dataToSend_ > 0 does not match request_.data_request_ != 0");
				return FZ_REPLY_INTERNALERROR;
			}

			response_.code_ = 0;
			response_.headers_.clear();

			opState = request_wait_connect;
			controlSocket_.InternalConnect(fz::to_wstring_from_utf8(request_.uri_.host_), request_.uri_.port_, request_.uri_.scheme_ == "https");
			return FZ_REPLY_CONTINUE;
		}
	case request_send_header:
		{
			// Assemble request and headers
			std::string command = fz::sprintf("%s %s HTTP/1.1\r\n", request_.verb_, request_.uri_.get_request());

			for (auto const& header : request_.headers_) {
				command += fz::sprintf("%s: %s\r\n", header.first, header.second);
			}

			command += "\r\n";

			if (request_.data_request_) {
				opState = request_send;
			}
			else {
				opState = request_read;
			}

			return controlSocket_.Send(command.c_str(), command.size());
		}
	case request_send:
		{
			int const chunkSize = 65536;

			while (dataToSend_) {
				controlSocket_.SendBufferReserve(chunkSize);

				if (!controlSocket_.sendBufferSize_) {
					unsigned int len = chunkSize;
					if (chunkSize > dataToSend_) {
						len = static_cast<unsigned int>(dataToSend_);
					}
					int res = request_.data_request_(controlSocket_.sendBuffer_ + controlSocket_.sendBufferSize_, len);
					if (res != FZ_REPLY_CONTINUE) {
						return res;
					}
					if (len > dataToSend_) {
						LogMessage(MessageType::Debug_Warning, L"request_.data_request_ returned too much data");
						return FZ_REPLY_INTERNALERROR;
					}

					controlSocket_.sendBufferSize_ = len;
				}

				int error;
				int written = controlSocket_.m_pBackend->Write(controlSocket_.sendBuffer_ + controlSocket_.sendBufferPos_, controlSocket_.sendBufferSize_ - controlSocket_.sendBufferPos_, error);
				if (written < 0) {
					if (error != EAGAIN) {
						LogMessage(MessageType::Error, _("Could not write to socket: %s"), CSocket::GetErrorDescription(error));
						LogMessage(MessageType::Error, _("Disconnected from server"));
						return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
					}
					written = 0;
				}

				dataToSend_ -= written;
				if (static_cast<unsigned int>(written) != controlSocket_.sendBufferSize_ - controlSocket_.sendBufferPos_) {
					controlSocket_.sendBufferPos_ += written;
					controlSocket_.sendBufferSize_ -= written;
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
	while (true) {
		const CSocket::SocketState state = controlSocket_.m_pSocket->GetState();
		if (state != CSocket::connected && state != CSocket::closing) {
			return FZ_REPLY_WOULDBLOCK;
		}

		if (!recv_buffer_) {
			recv_buffer_ = std::make_unique<unsigned char[]>(m_recvBufferLen);
			m_recvBufferPos = 0;
		}

		unsigned int len = m_recvBufferLen - m_recvBufferPos;
		if (!len) {
			LogMessage(MessageType::Debug_Warning, L"Read data isn't being consumed.");
			return FZ_REPLY_INTERNALERROR;
		}

		int error;
		int read = controlSocket_.m_pBackend->Read(recv_buffer_.get() + m_recvBufferPos, len, error);
		if (read == -1) {
			if (error != EAGAIN) {
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			return FZ_REPLY_WOULDBLOCK;
		}

		controlSocket_.SetActive(CFileZillaEngine::recv);

		m_recvBufferPos += read;

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
				assert(!m_recvBufferPos);
				return FZ_REPLY_OK;
			}
			else {
				int res = ProcessData(recv_buffer_.get(), m_recvBufferPos);
				if (res != FZ_REPLY_CONTINUE) {
					return res;
				}		
				m_recvBufferPos = 0;
			}
		}
	} while (controlSocket_.m_pSocket);

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::ParseHeader()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpRequestOpData::ParseHeader()");

	// Parse the HTTP header.
	// We do just the neccessary parsing and silently ignore most header fields
	// Redirects are supported though if the server sends the Location field.

	for (;;) {
		// Find line ending
		unsigned int i = 0;
		for (i = 0; (i + 1) < m_recvBufferPos; ++i) {
			if (recv_buffer_[i] == '\r') {
				if (recv_buffer_[i + 1] != '\n') {
					LogMessage(MessageType::Error, _("Malformed response header: %s"), _("Server not sending proper line endings"));
					return FZ_REPLY_ERROR;
				}
				break;
			}
		}
		if ((i + 1) >= m_recvBufferPos) {
			if (m_recvBufferPos == m_recvBufferLen) {
				// We don't support header lines larger than 4096
				LogMessage(MessageType::Error, _("Too long header line"));
				return FZ_REPLY_ERROR;
			}
			return FZ_REPLY_WOULDBLOCK;
		}

		recv_buffer_[i] = 0;
		std::wstring wline = fz::to_wstring_from_utf8(reinterpret_cast<char*>(recv_buffer_.get()));
		if (wline.empty()) {
			wline = fz::to_wstring(reinterpret_cast<char*>(recv_buffer_.get()));
		}
		if (!wline.empty()) {
			controlSocket_.LogMessageRaw(MessageType::Response, wline);
		}

		if (!response_.code_) {
			if (m_recvBufferPos < 16 || memcmp(recv_buffer_.get(), "HTTP/1.", 7)) {
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
				memmove(recv_buffer_.get(), recv_buffer_.get() + 2, m_recvBufferPos - 2);
				m_recvBufferPos -= 2;

				// End of header
				int res = ProcessCompleteHeader();
				if (res != FZ_REPLY_CONTINUE) {
					return res;
				}

				if (!m_recvBufferPos) {
					return FZ_REPLY_WOULDBLOCK;
				}

				if (!got_header_) {
					// In case we got 100 Continue
					continue;
				}

				if (transfer_encoding_ == chunked) {
					return ParseChunkedData();
				}
				else {
					int res = ProcessData(recv_buffer_.get(), m_recvBufferPos);
					if (res != FZ_REPLY_CONTINUE) {
						return res;
					}
					m_recvBufferPos = 0;
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

		memmove(recv_buffer_.get(), recv_buffer_.get() + i + 2, m_recvBufferPos - i - 2);
		m_recvBufferPos -= i + 2;

		if (!m_recvBufferPos) {
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

	return res;
}

int CHttpRequestOpData::ParseChunkedData()
{
	unsigned char* p = recv_buffer_.get();
	unsigned int len = m_recvBufferPos;

	for (;;) {
		if (chunk_data_.size != 0) {
			unsigned int dataLen = len;
			if (chunk_data_.size < len) {
				dataLen = static_cast<unsigned int>(chunk_data_.size);
			}
			int res = ProcessData(p, dataLen);
			if (res != FZ_REPLY_CONTINUE) {
				return res;
			}

			chunk_data_.size -= dataLen;
			p += dataLen;
			len -= dataLen;

			if (chunk_data_.size == 0) {
				chunk_data_.terminateChunk = true;
			}

			if (!len) {
				break;
			}
		}

		// Find line ending
		unsigned int i = 0;
		for (i = 0; (i + 1) < len; ++i) {
			if (p[i] == '\r') {
				if (p[i + 1] != '\n') {
					LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Wrong line endings"));
					return FZ_REPLY_ERROR;
				}
				break;
			}
		}
		if ((i + 1) >= len) {
			if (len == m_recvBufferLen) {
				// We don't support lines larger than 4096
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Line length exceeded"));
				return FZ_REPLY_ERROR;
			}
			break;
		}

		p[i] = 0;

		if (chunk_data_.terminateChunk) {
			if (i) {
				// The chunk data has to end with CRLF. If i is nonzero,
				// it didn't end with just CRLF.
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Chunk data improperly terminated"));
				return FZ_REPLY_ERROR;
			}
			chunk_data_.terminateChunk = false;
		}
		else if (chunk_data_.getTrailer) {
			if (!i) {
				// We're done
				return FZ_REPLY_OK;
			}

			// Ignore the trailer
		}
		else {
			// Read chunk size
			for (unsigned char* q = p; *q && *q != ';' && *q != ' '; ++q) {
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

		p += i + 2;
		len -= i + 2;

		if (!len) {
			break;
		}
	}

	if (p != recv_buffer_.get()) {
		memmove(recv_buffer_.get(), p, len);
		m_recvBufferPos = len;
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
