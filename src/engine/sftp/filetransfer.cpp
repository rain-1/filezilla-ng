#include <filezilla.h>

#include "directorycache.h"
#include "filetransfer.h"

#include <libfilezilla/local_filesys.hpp>

enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_waitcwd,
	filetransfer_waitlist,
	filetransfer_mtime,
	filetransfer_transfer,
	filetransfer_chmtime
};

int CSftpFileTransferOpData::Send()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpFileTransferOpData::Send() in state %d", opState);
	
	if (opState == filetransfer_init) {

		if (localFile_.empty()) {
			if (!download_) {
				return FZ_REPLY_CRITICALERROR | FZ_REPLY_NOTSUPPORTED;
			}
			else {
				return FZ_REPLY_SYNTAXERROR;
			}
		}

		if (download_) {
			std::wstring filename = remotePath_.FormatFilename(remoteFile_);
			LogMessage(MessageType::Status, _("Starting download of %s"), filename);
		}
		else {
			LogMessage(MessageType::Status, _("Starting upload of %s"), localFile_);
		}

		int64_t size;
		bool isLink;
		if (fz::local_filesys::get_file_info(fz::to_native(localFile_), isLink, &size, 0, 0) == fz::local_filesys::file) {
			localFileSize_ = size;
		}

		opState = filetransfer_waitcwd;

		if (remotePath_.GetType() == DEFAULT) {
			remotePath_.SetType(currentServer_.GetType());
		}

		controlSocket_.ChangeDir(remotePath_);
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == filetransfer_transfer) {
		// Bit convoluted, but we need to guarantee that local filenames are passed as UTF-8 to fzsftp,
		// whereas we need to use server encoding for remote filenames.
		std::string cmd;
		std::wstring logstr;
		if (resume_) {
			cmd = "re";
			logstr = L"re";
		}
		if (download_) {
			if (!resume_) {
				controlSocket_.CreateLocalDir(localFile_);
			}

			engine_.transfer_status_.Init(remoteFileSize_, resume_ ? localFileSize_ : 0, false);
			cmd += "get ";
			logstr += L"get ";
			
			std::string remoteFile = controlSocket_.ConvToServer(controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_)));
			if (remoteFile.empty()) {
				LogMessage(MessageType::Error, _("Could not convert command to server encoding"));
				return FZ_REPLY_ERROR;
			}
			cmd += remoteFile + " ";
			logstr += controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_)) + L" "; 
			
			std::wstring localFile = controlSocket_.QuoteFilename(localFile_);
			cmd += fz::to_utf8(localFile);
			logstr += localFile;
		}
		else {
			engine_.transfer_status_.Init(localFileSize_, resume_ ? remoteFileSize_ : 0, false);
			cmd += "put ";
			logstr += L"put ";

			std::wstring localFile = controlSocket_.QuoteFilename(localFile_);
			cmd += fz::to_utf8(localFile) + " ";
			logstr += localFile + L" ";

			std::string remoteFile = controlSocket_.ConvToServer(controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_)));
			if (remoteFile.empty()) {
				LogMessage(MessageType::Error, _("Could not convert command to server encoding"));
				return FZ_REPLY_ERROR;
			}
			cmd += remoteFile;
			logstr += controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_));
		}
		engine_.transfer_status_.SetStartTime();
		transferInitiated_ = true;
		controlSocket_.SetWait(true);

		controlSocket_.LogMessageRaw(MessageType::Command, logstr);
		return controlSocket_.AddToStream(cmd + "\r\n");
	}
	else if (opState == filetransfer_mtime) {
		std::wstring quotedFilename = controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_));
		return controlSocket_.SendCommand(L"mtime " + controlSocket_.WildcardEscape(quotedFilename), L"mtime " + quotedFilename);
	}
	else if (opState == filetransfer_chmtime) {
		assert(!fileTime_.empty());
		if (download_) {
			LogMessage(MessageType::Debug_Info, L"  filetransfer_chmtime during download");
			return FZ_REPLY_INTERNALERROR;
		}

		std::wstring quotedFilename = controlSocket_.QuoteFilename(remotePath_.FormatFilename(remoteFile_, !tryAbsolutePath_));

		fz::datetime t = fileTime_;
		t -= fz::duration::from_minutes(currentServer_.GetTimezoneOffset());

		// Y2K38
		time_t ticks = t.get_time_t();
		std::wstring seconds = fz::sprintf(L"%d", ticks);
		return controlSocket_.SendCommand(L"chmtime " + seconds + L" " + controlSocket_.WildcardEscape(quotedFilename), L"chmtime " + seconds + L" " + quotedFilename);
	}

	return FZ_REPLY_INTERNALERROR;
}

int CSftpFileTransferOpData::ParseResponse()
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpFileTransferOpData::ParseResponse() in state %d", opState);

	if (opState == filetransfer_transfer) {
		if (controlSocket_.result_ == FZ_REPLY_OK && engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS)) {
			if (download_) {
				if (!fileTime_.empty()) {
					if (!fz::local_filesys::set_modification_time(fz::to_native(localFile_), fileTime_))
						LogMessage(MessageType::Debug_Warning, L"Could not set modification time");
				}
			}
			else {
				fileTime_ = fz::local_filesys::get_modification_time(fz::to_native(localFile_));
				if (!fileTime_.empty()) {
					opState = filetransfer_chmtime;
					return FZ_REPLY_CONTINUE;
				}
			}
		}
		return controlSocket_.result_;
	}
	else if (opState == filetransfer_mtime) {
		if (controlSocket_.result_ == FZ_REPLY_OK && !controlSocket_.response_.empty()) {
			time_t seconds = 0;
			bool parsed = true;
			for (auto const& c : controlSocket_.response_) {
				if (c < '0' || c > '9') {
					parsed = false;
					break;
				}
				seconds *= 10;
				seconds += c - '0';
			}
			if (parsed) {
				fz::datetime fileTime = fz::datetime(seconds, fz::datetime::seconds);
				if (!fileTime.empty()) {
					fileTime_ = fileTime;
					fileTime_ += fz::duration::from_minutes(currentServer_.GetTimezoneOffset());
				}
			}
		}
		opState = filetransfer_transfer;
		int res = controlSocket_.CheckOverwriteFile();
		if (res != FZ_REPLY_OK) {
			return res;
		}

		return FZ_REPLY_CONTINUE;
	}
	else if (opState == filetransfer_chmtime) {
		if (download_) {
			LogMessage(MessageType::Debug_Info, L"  filetransfer_chmtime during download");
			return FZ_REPLY_INTERNALERROR;
		}
		return FZ_REPLY_OK;
	}
	else {
		LogMessage(MessageType::Debug_Info, L"  Called at improper time: opState == %d", opState);
	}

	return FZ_REPLY_INTERNALERROR;
}

int CSftpFileTransferOpData::SubcommandResult(int prevResult, COpData const&)
{
	LogMessage(MessageType::Debug_Verbose, L"CSftpFileTransferOpData::SubcommandResult() in state %d", opState);

	if (opState == filetransfer_waitcwd) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, tryAbsolutePath_ ? remotePath_ : currentPath_, remoteFile_, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist) {
					opState = filetransfer_waitlist;
				}
				else if (download_ && engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS)) {
					opState = filetransfer_mtime;
				}
				else {
					opState = filetransfer_transfer;
				}
			}
			else {
				if (entry.is_unsure()) {
					opState = filetransfer_waitlist;
				}
				else {
					if (matchedCase) {
						remoteFileSize_ = entry.size;
						if (entry.has_date()) {
							fileTime_ = entry.time;
						}

						if (download_ && !entry.has_time() &&
							engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
						{
							opState = filetransfer_mtime;
						}
						else {
							opState = filetransfer_transfer;
						}
					}
					else {
						opState = filetransfer_mtime;
					}
				}
			}
			if (opState == filetransfer_waitlist) {
				controlSocket_.List(CServerPath(), L"", LIST_FLAG_REFRESH);
				return FZ_REPLY_CONTINUE;
			}
			else if (opState == filetransfer_transfer) {
				int res = controlSocket_.CheckOverwriteFile();
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
		}
		else {
			tryAbsolutePath_ = true;
			opState = filetransfer_mtime;
		}
	}
	else if (opState == filetransfer_waitlist) {
		if (prevResult == FZ_REPLY_OK) {
			CDirentry entry;
			bool dirDidExist;
			bool matchedCase;
			bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, tryAbsolutePath_ ? remotePath_ : currentPath_, remoteFile_, dirDidExist, matchedCase);
			if (!found) {
				if (!dirDidExist) {
					opState = filetransfer_mtime;
				}
				else if (download_ &&
					engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
				{
					opState = filetransfer_mtime;
				}
				else {
					opState = filetransfer_transfer;
				}
			}
			else {
				if (matchedCase && !entry.is_unsure()) {
					remoteFileSize_ = entry.size;
					if (!entry.has_date()) {
						fileTime_ = entry.time;
					}

					if (download_ && !entry.has_time() &&
						engine_.GetOptions().GetOptionVal(OPTION_PRESERVE_TIMESTAMPS))
					{
						opState = filetransfer_mtime;
					}
					else {
						opState = filetransfer_transfer;
					}
				}
				else {
					opState = filetransfer_mtime;
				}
			}
			if (opState == filetransfer_transfer) {
				int res = controlSocket_.CheckOverwriteFile();
				if (res != FZ_REPLY_OK) {
					return res;
				}
			}
		}
		else {
			opState = filetransfer_mtime;
		}
	}
	else {
		LogMessage(MessageType::Debug_Warning, L"  Unknown opState (%d)", opState);
		return FZ_REPLY_INTERNALERROR;
	}

	return FZ_REPLY_CONTINUE;
}
