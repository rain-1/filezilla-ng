#include <filezilla.h>

#include "filetransfer.h"

#include <libfilezilla/local_filesys.hpp>

enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_waitfileexists,
	filetransfer_transfer
};

int CHttpFileTransferOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpFileTransferOpData::Send");

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

		if (!localFile_.empty()) {
			localFileSize_ = fz::local_filesys::get_size(fz::to_native(localFile_));

			opState = filetransfer_waitfileexists;
			int res = controlSocket_.CheckOverwriteFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}

			res = OpenFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}
		opState = filetransfer_transfer;

		if (resume_) {
			req_.headers_["Range"] = fz::sprintf("bytes=%d-", localFileSize_);
		}

		response_.on_data_ = [this](auto data, auto len) { return this->OnData(data, len); };

		controlSocket_.Request(req_, response_);
		return FZ_REPLY_CONTINUE;
	default:
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpFileTransferOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CHttpFileTransferOpData::ParseResponse");
	return FZ_REPLY_INTERNALERROR;
}

int CHttpFileTransferOpData::OpenFile()
{
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
	if (!end) {
		resume_ = false;
	}
	localFileSize_ = fz::local_filesys::get_size(fz::to_native(localFile_));
	return FZ_REPLY_OK;
}

int CHttpFileTransferOpData::OnData(unsigned char const* data, unsigned int len)
{
	// TODO: Check if first call

	/* if (engine_.transfer_status_.empty()) {
		engine_.transfer_status_.Init(pData->m_totalSize, 0, false);
		engine_.transfer_status_.SetStartTime();
	}

	if (pData->localFile.empty()) {
		char* q = new char[len];
		memcpy(q, p, len);
		engine_.AddNotification(new CDataNotification(q, len));
	}
	else {
		assert(pData->file.opened());

		auto write = static_cast<int64_t>(len);
		if (pData->file.write(p, write) != write) {
			LogMessage(MessageType::Error, _("Failed to write to file %s"), pData->localFile);
			return FZ_REPLY_ERROR;
		}
	}

	engine_.transfer_status_.Update(len);
*/
	return FZ_REPLY_ERROR;
}
