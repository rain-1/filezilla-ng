#include <filezilla.h>

#include "filetransfer.h"

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
		req_.uri_ = fz::uri(fz::to_utf8(currentServer_.Format(ServerFormat::url)) + fz::to_utf8(remotePath.FormatFilename(remoteFile)));
		if (req_.uri_.empty()) {
			LogMessage(MessageType::Error, _("Could not create URI for this transfer."));
			return FZ_REPLY_ERROR;
		}

		return FZ_REPLY_INTERNALERROR;
		/*
		if (!localFile.empty()) {
			httpRequestOpData->localFileSize = fz::local_filesys::get_size(fz::to_native(httpRequestOpData->localFile));

			httpRequestOpData->opState = filetransfer_waitfileexists;
			int res = CheckOverwriteFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}

			httpRequestOpData->opState = filetransfer_transfer;

			res = OpenFile(pData);
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}
		else {
			httpRequestOpData->opState = filetransfer_transfer;
		}

		int res = InternalConnect(currentServer_.GetHost(), currentServer_.GetPort(), currentServer_.GetProtocol() == HTTPS);
		if (res != FZ_REPLY_OK) {
			return res;
		}
		*/
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
