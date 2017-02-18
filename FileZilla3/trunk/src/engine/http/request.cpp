#include <filezilla.h>

#include "request.h"

enum requestStates
{
	request_init = 0,
	request_wait_connect,
	request_send_header,
	request_send,
	request_read
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

			for (auto const& header : request_.headers_) {
				command += fz::sprintf("%s: %s\r\n", header.first, header.second);
			}

			command += "\r\n";

			// FIXME: Send body
			opState = request_read;

			return controlSocket_.Send(command.c_str(), command.size());
		}
	case request_read:
		return FZ_REPLY_WOULDBLOCK;
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
			ParseChunkedData();
		}
		else {
			if (!read) {
				assert(!m_recvBufferPos);
				return FZ_REPLY_OK;
			}
			else {
				receivedData_ += m_recvBufferPos;
				if (response_.on_data_) {
					int res = response_.on_data_(recv_buffer_.get(), m_recvBufferPos);
					if (res != FZ_REPLY_CONTINUE) {
						return res;
					}
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
					LogMessage(MessageType::Error, _("Malformed reply, server not sending proper line endings"));
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
				// End of header, data from now on
				got_header_ = true;

				memmove(recv_buffer_.get(), recv_buffer_.get() + 2, m_recvBufferPos - 2);
				m_recvBufferPos -= 2;

				if (!m_recvBufferPos) {
					return FZ_REPLY_WOULDBLOCK;
				}

				if (transfer_encoding_ == chunked) {
					return ParseChunkedData();
				}
				else {
					receivedData_ += m_recvBufferPos;
					if (response_.on_data_) {
						int res = response_.on_data_(recv_buffer_.get(), m_recvBufferPos);
						if (res != FZ_REPLY_CONTINUE) {
							return res;
						}
					}
					m_recvBufferPos = 0;
					return FZ_REPLY_WOULDBLOCK;
				}
			}

			std::string line(recv_buffer_.get(), recv_buffer_.get() + i);

			auto pos = line.find(": ");
			if (pos == std::string::npos || !pos) {
				LogMessage(MessageType::Error, _("Malformed header: %s"), _("Invalid line"));
				return FZ_REPLY_ERROR;
			}

			response_.headers_[line.substr(0, pos)] = line.substr(pos + 2);
			/*
			else if (m_recvBufferPos > 21 && !memcmp(m_pRecvBuffer, "Transfer-Encoding: ", 19)) {
				if (!strcmp(m_pRecvBuffer + 19, "chunked")) {
					transfer_encoding_ = CHttpOpData::chunked;
				}
				else if (!strcmp(m_pRecvBuffer + 19, "identity")) {
					transfer_encoding_ = CHttpOpData::identity;
				}
				else {
					transfer_encoding_ = CHttpOpData::unknown;
				}
			}
			else if (i > 16 && !memcmp(m_pRecvBuffer, "Content-Length: ", 16)) {
				m_totalSize = 0;
				char* p = m_pRecvBuffer + 16;
				while (*p) {
					if (*p < '0' || *p > '9') {
						LogMessage(MessageType::Error, _("Malformed header: %s"), _("Invalid Content-Length"));
						ResetOperation(FZ_REPLY_ERROR);
						return FZ_REPLY_ERROR;
					}
					m_totalSize = m_totalSize * 10 + *p++ - '0';
				}
			}*/
		}

		memmove(recv_buffer_.get(), recv_buffer_.get() + i + 2, m_recvBufferPos - i - 2);
		m_recvBufferPos -= i + 2;

		if (!m_recvBufferPos) {
			break;
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::ParseChunkedData()
{
	/*
	char* p = m_pRecvBuffer;
	unsigned int len = m_recvBufferPos;

	for (;;) {
		if (pData->m_chunkData.size != 0) {
			unsigned int dataLen = len;
			if (pData->m_chunkData.size < len) {
				dataLen = static_cast<unsigned int>(pData->m_chunkData.size);
			}
			pData->m_receivedData += dataLen;
			int res = ProcessData(p, dataLen);
			if (res != FZ_REPLY_WOULDBLOCK) {
				return res;
			}

			pData->m_chunkData.size -= dataLen;
			p += dataLen;
			len -= dataLen;

			if (pData->m_chunkData.size == 0) {
				pData->m_chunkData.terminateChunk = true;
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
					ResetOperation(FZ_REPLY_ERROR);
					return FZ_REPLY_ERROR;
				}
				break;
			}
		}
		if ((i + 1) >= len) {
			if (len == m_recvBufferLen) {
				// We don't support lines larger than 4096
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Line length exceeded"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
			break;
		}

		p[i] = 0;

		if (pData->m_chunkData.terminateChunk) {
			if (i) {
				// The chunk data has to end with CRLF. If i is nonzero,
				// it didn't end with just CRLF.
				LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Chunk data improperly terminated"));
				ResetOperation(FZ_REPLY_ERROR);
				return FZ_REPLY_ERROR;
			}
			pData->m_chunkData.terminateChunk = false;
		}
		else if (pData->m_chunkData.getTrailer) {
			if (!i) {
				// We're done
				return ProcessData(0, 0);
			}

			// Ignore the trailer
		}
		else
		{
			// Read chunk size
			for (char* q = p; *q && *q != ';' && *q != ' '; ++q) {
				pData->m_chunkData.size *= 16;
				if (*q >= '0' && *q <= '9') {
					pData->m_chunkData.size += *q - '0';
				}
				else if (*q >= 'A' && *q <= 'F') {
					pData->m_chunkData.size += *q - 'A' + 10;
				}
				else if (*q >= 'a' && *q <= 'f') {
					pData->m_chunkData.size += *q - 'a' + 10;
				}
				else {
					// Invalid size
					LogMessage(MessageType::Error, _("Malformed chunk data: %s"), _("Invalid chunk size"));
					ResetOperation(FZ_REPLY_ERROR);
					return FZ_REPLY_ERROR;
				}
			}
			if (!pData->m_chunkData.size) {
				pData->m_chunkData.getTrailer = true;
			}
		}

		p += i + 2;
		len -= i + 2;

		if (!len) {
			break;
		}
	}

	if (p != m_pRecvBuffer) {
		memmove(m_pRecvBuffer, p, len);
		m_recvBufferPos = len;
	}

	return FZ_REPLY_WOULDBLOCK;
	*/
	return FZ_REPLY_INTERNALERROR;
}
