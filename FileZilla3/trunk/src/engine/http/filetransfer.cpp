#include <filezilla.h>

#include "filetransfer.h"

#include <libfilezilla/local_filesys.hpp>

#include <string.h>

enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_waitfileexists,
	filetransfer_transfer,
	filetransfer_waittransfer
};

int CHttpFileTransferOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpFileTransferOpData::Send() in state %d", opState);

	switch (opState) {
	case filetransfer_init:
		if (!download_) {
			return FZ_REPLY_NOTSUPPORTED;
		}

		// TODO: Ordinarily we need to percent-encode the filename. With the current API we then however would not be able to pass the query part of the URL
		req_.uri_ = fz::uri(fz::to_utf8(currentServer_.Format(ServerFormat::url)) + fz::to_utf8(remotePath_.FormatFilename(remoteFile_)));
		if (req_.uri_.empty()) {
			LogMessage(MessageType::Error, _("Could not create URI for this transfer."));
			return FZ_REPLY_ERROR;
		}

		req_.verb_ = "GET";

		opState = filetransfer_waitfileexists;
		if (!localFile_.empty()) {
			localFileSize_ = fz::local_filesys::get_size(fz::to_native(localFile_));

			int res = controlSocket_.CheckOverwriteFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}
		return FZ_REPLY_CONTINUE;
	case filetransfer_waitfileexists:
		if (!localFile_.empty()) {
			int res = OpenFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}
		opState = filetransfer_transfer;
		return FZ_REPLY_CONTINUE;
	case filetransfer_transfer:
		if (resume_) {
			req_.headers_["Range"] = fz::sprintf("bytes=%d-", localFileSize_);
		}

		response_ = HttpResponse();
		response_.on_header_ = [this]() { return this->OnHeader(); };
		response_.on_data_ = [this](auto data, auto len) { return this->OnData(data, len); };

		opState = filetransfer_waittransfer;
		controlSocket_.Request(req_, response_);
		return FZ_REPLY_CONTINUE;
	default:
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpFileTransferOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpFileTransferOpData::ParseResponse() in state %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CHttpFileTransferOpData::OpenFile()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpFileTransferOpData::OpenFile");
	file_.close();

	controlSocket_.CreateLocalDir(localFile_);

	if (!file_.open(fz::to_native(localFile_),
		download_ ? fz::file::writing : fz::file::reading,
		fz::file::existing))
	{
		LogMessage(MessageType::Error, _("Failed to open \"%s\" for writing"), localFile_);
		return FZ_REPLY_ERROR;
	}

	assert(download_);
	int64_t end = file_.seek(0, fz::file::end);
	if (end < 0) {
		LogMessage(MessageType::Error, _("Could not seek to the end of the file"));
		return FZ_REPLY_ERROR;
	}
	if (!end) {
		resume_ = false;
	}
	localFileSize_ = fz::local_filesys::get_size(fz::to_native(localFile_));
	return FZ_REPLY_OK;
}

int CHttpFileTransferOpData::OnHeader()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpFileTransferOpData::OnHeader");

	if (response_.code_ < 200 || response_.code_ >= 400) {
		return FZ_REPLY_ERROR;
	}

	if (response_.code_ == 416 && resume_) {
		assert(file_.opened());
		if (file_.seek(0, fz::file::begin) != 0) {
			LogMessage(MessageType::Error, _("Could not seek to the beginning of the file"));
			return FZ_REPLY_ERROR;
		}
		resume_ = false;

		opState = filetransfer_transfer;
		return FZ_REPLY_ERROR;
	}

	// Handle any redirects
	if (response_.code_ >= 300) {

		if (++redirectCount_ >= 6) {
			LogMessage(MessageType::Error, _("Too many redirects"));
			return FZ_REPLY_ERROR;
		}

		if (response_.code_ == 305) {
			LogMessage(MessageType::Error, _("Unsupported redirect"));
			return FZ_REPLY_ERROR;
		}

		fz::uri location = fz::uri(response_.get_header("Location"));
		if (!location.empty()) {
			location.resolve(req_.uri_);
		}
		
		if (location.scheme_.empty() || location.host_.empty() || !location.is_absolute()) {
			LogMessage(MessageType::Error, _("Redirection to invalid or unsupported URI: %s"), location.to_string());
			return FZ_REPLY_ERROR;
		}

		ServerProtocol protocol = CServer::GetProtocolFromPrefix(fz::to_wstring_from_utf8(location.scheme_));
		if (protocol != HTTP && protocol != HTTPS) {
			LogMessage(MessageType::Error, _("Redirection to invalid or unsupported address: %s"), location.to_string());
			return FZ_REPLY_ERROR;
		}

		// International domain names
		std::wstring host = fz::to_wstring_from_utf8(location.host_);
		if (host.empty()) {
			LogMessage(MessageType::Error, _("Invalid hostname: %s"), location.to_string());
			return FZ_REPLY_ERROR;
		}

		req_.uri_ = location;

		opState = filetransfer_transfer;
		return FZ_REPLY_ERROR;
	}

	// Check if the server disallowed resume
	if (resume_ && response_.code_ != 206) {
		assert(file_.opened());
		if (file_.seek(0, fz::file::begin) != 0) {
			LogMessage(MessageType::Error, _("Could not seek to the beginning of the file"));
			return FZ_REPLY_ERROR;
		}
		resume_ = false;
	}

	int64_t totalSize = fz::to_integral<int64_t>(response_.get_header("Content-Length"), -1);
	if (totalSize == -1) {
		if (remoteFileSize_ != -1) {
			totalSize = remoteFileSize_;
		}
	}

	if (engine_.transfer_status_.empty()) {
		engine_.transfer_status_.Init(totalSize, resume_ ? localFileSize_ : 0, false);
		engine_.transfer_status_.SetStartTime();
	}

	return FZ_REPLY_CONTINUE;
}

int CHttpFileTransferOpData::OnData(unsigned char const* data, unsigned int len)
{
	if (opState != filetransfer_waittransfer) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (localFile_.empty()) {
		char* q = new char[len];
		memcpy(q, data, len);
		engine_.AddNotification(new CDataNotification(q, len));
	}
	else {
		assert(file_.opened());

		auto write = static_cast<int64_t>(len);
		if (file_.write(data, write) != write) {
			LogMessage(MessageType::Error, _("Failed to write to file %s"), localFile_);
			return FZ_REPLY_ERROR;
		}
	}

	engine_.transfer_status_.Update(len);

	return FZ_REPLY_CONTINUE;
}

int CHttpFileTransferOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpFileTransferOpData::SubcommandResult() in state %d", opState);

	if (opState == filetransfer_transfer) {
		return FZ_REPLY_CONTINUE;
	}
	return prevResult;
}
