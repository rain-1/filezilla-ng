#include <filezilla.h>

#include "connect.h"
#include "delete.h"
#include "event.h"
#include "input_thread.h"
#include "directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "file_transfer.h"
#include "list.h"
#include "mkd.h"
#include "pathcache.h"
#include "proxy.h"
#include "resolve.h"
#include "rmd.h"
#include "servercapabilities.h"
#include "storjcontrolsocket.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>
#include <libfilezilla/thread_pool.hpp>

#include <algorithm>
#include <cwchar>

CStorjControlSocket::CStorjControlSocket(CFileZillaEnginePrivate & engine)
	: CControlSocket(engine)
{
	m_useUTF8 = true;
}

CStorjControlSocket::~CStorjControlSocket()
{
	remove_handler();
	DoClose();
}

void CStorjControlSocket::Connect(CServer const &server, Credentials const& credentials)
{
	LogMessage(MessageType::Status, _("Connecting to %s..."), server.Format(ServerFormat::with_optional_port));
	SetWait(true);

	currentServer_ = server;

	process_ = std::make_unique<fz::process>();

	engine_.GetRateLimiter().AddObject(this);
	Push(std::make_unique<CStorjConnectOpData>(*this, credentials));
}

void CStorjControlSocket::List(CServerPath const& path, std::wstring const& subDir, int flags)
{
	CServerPath newPath = currentPath_;
	if (!path.empty()) {
		newPath = path;
	}
	if (!newPath.ChangePath(subDir)) {
		newPath.clear();
	}

	if (newPath.empty()) {
		LogMessage(MessageType::Status, _("Retrieving directory listing..."));
	}
	else {
		LogMessage(MessageType::Status, _("Retrieving directory listing of \"%s\"..."), newPath.GetPath());
	}

	Push(std::make_unique<CStorjListOpData>(*this, newPath, std::wstring(), flags, operations_.empty()));
}

void CStorjControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
						 std::wstring const& remoteFile, bool download,
						 CFileTransferCommand::t_transferSettings const& transferSettings)
{
	auto pData = std::make_unique<CStorjFileTransferOpData>(*this, download, localFile, remoteFile, remotePath);
	pData->transferSettings_ = transferSettings;
	Push(std::move(pData));
}


void CStorjControlSocket::Delete(CServerPath const& path, std::deque<std::wstring>&& files)
{
	// CFileZillaEnginePrivate should have checked this already
	assert(!files.empty());

	LogMessage(MessageType::Debug_Verbose, L"CStorjControlSocket::Delete");

	Push(std::make_unique<CStorjDeleteOpData>(*this, path, std::move(files)));
}

void CStorjControlSocket::Resolve(CServerPath const& path, std::wstring const& file, std::wstring & bucket, std::wstring * fileId, bool ignore_missing_file)
{
	Push(std::make_unique<CStorjResolveOpData>(*this, path, file, bucket, fileId, ignore_missing_file));
}

void CStorjControlSocket::Resolve(CServerPath const& path, std::deque<std::wstring> const& files, std::wstring & bucket, std::deque<std::wstring> & fileIds)
{
	Push(std::make_unique<CStorjResolveManyOpData>(*this, path, files, bucket, fileIds));
}

void CStorjControlSocket::Mkdir(CServerPath const& path)
{
	if (operations_.empty()) {
		LogMessage(MessageType::Status, _("Creating directory '%s'..."), path.GetPath());
	}

	auto pData = std::make_unique<CStorjMkdirOpData>(*this);
	pData->path_ = path;
	Push(std::move(pData));
}

void CStorjControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjControlSocket::RemoveDir");

	auto pData = std::make_unique<CStorjRemoveDirOpData>(*this);
	pData->path_ = path;
	if (!subDir.empty()) {
		pData->path_.ChangePath(subDir);
	}
	Push(std::move(pData));
}

void CStorjControlSocket::OnStorjEvent(storj_message const& message)
{
	if (!currentServer_) {
		return;
	}

	if (!input_thread_) {
		return;
	}

	switch (message.type)
	{
	case storjEvent::Reply:
		LogMessageRaw(MessageType::Response, message.text[0]);
		ProcessReply(FZ_REPLY_OK, message.text[0]);
		break;
	case storjEvent::Done:
		ProcessReply(FZ_REPLY_OK, std::wstring());
		break;
	case storjEvent::Error:
		LogMessageRaw(MessageType::Error, message.text[0]);
		ProcessReply(FZ_REPLY_ERROR, message.text[0]);
		break;
	case storjEvent::ErrorMsg:
		LogMessageRaw(MessageType::Error, message.text[0]);
		break;
	case storjEvent::Verbose:
		LogMessageRaw(MessageType::Debug_Info, message.text[0]);
		break;
	case storjEvent::Info:
		LogMessageRaw(MessageType::Command, message.text[0]); // Not exactly the right message type, but it's a silent one.
		break;
	case storjEvent::Status:
		LogMessageRaw(MessageType::Status, message.text[0]);
		break;
	case storjEvent::Recv:
		SetActive(CFileZillaEngine::recv);
		break;
	case storjEvent::Send:
		SetActive(CFileZillaEngine::send);
		break;
	case storjEvent::Listentry:
		if (operations_.empty() || operations_.back()->opId != Command::list) {
			LogMessage(MessageType::Debug_Warning, L"storjEvent::Listentry outside list operation, ignoring.");
			break;
		}
		else {
			int res = static_cast<CStorjListOpData&>(*operations_.back()).ParseEntry(std::move(message.text[0]), message.text[1], std::move(message.text[2]), message.text[3]);
			if (res != FZ_REPLY_WOULDBLOCK) {
				ResetOperation(res);
			}
		}
		break;
	case storjEvent::UsedQuotaRecv:
		OnQuotaRequest(CRateLimiter::inbound);
		break;
	case storjEvent::UsedQuotaSend:
		OnQuotaRequest(CRateLimiter::outbound);
		break;
	case storjEvent::Transfer:
		{
			auto value = fz::to_integral<int64_t>(message.text[0]);

			if (!operations_.empty() && operations_.back()->opId == Command::transfer) {
				auto & data = static_cast<CStorjFileTransferOpData &>(*operations_.back());

				SetActive(data.download_ ? CFileZillaEngine::recv : CFileZillaEngine::send);

				bool tmp;
				CTransferStatus status = engine_.transfer_status_.Get(tmp);
				if (!status.empty() && !status.madeProgress) {
					if (data.download_) {
						if (value > 0) {
							engine_.transfer_status_.SetMadeProgress();
						}
					}
					else {
						if (status.currentOffset > status.startOffset + 65565) {
							engine_.transfer_status_.SetMadeProgress();
						}
					}
				}
			}

			engine_.transfer_status_.Update(value);
		}
		break;
	default:
		LogMessage(MessageType::Debug_Warning, L"Message type %d not handled", message.type);
		break;
	}
}

void CStorjControlSocket::OnTerminate(std::wstring const& error)
{
	if (!error.empty()) {
		LogMessageRaw(MessageType::Error, error);
	}
	else {
		LogMessageRaw(MessageType::Debug_Info, L"CStorjControlSocket::OnTerminate without error");
	}
	if (process_) {
		DoClose();
	}
}

int CStorjControlSocket::SendCommand(std::wstring const& cmd, std::wstring const& show)
{
	if (cmd.substr(0, 4) != L"get " && cmd.substr(0, 4) != L"put ") {
		SetWait(true);
	}

	LogMessageRaw(MessageType::Command, show.empty() ? cmd : show);

	// Check for newlines in command
	// a command like "ls\nrm foo/bar" is dangerous
	if (cmd.find('\n') != std::wstring::npos ||
		cmd.find('\r') != std::wstring::npos)
	{
		LogMessage(MessageType::Debug_Warning, L"Command containing newline characters, aborting.");
		return FZ_REPLY_INTERNALERROR;
	}

	return AddToStream(cmd + L"\n");
}

int CStorjControlSocket::AddToStream(std::wstring const& cmd)
{
	if (!process_) {
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	std::string const str = ConvToServer(cmd, true);
	if (str.empty()) {
		LogMessage(MessageType::Error, _("Could not convert command to server encoding"));
		return FZ_REPLY_ERROR;
	}

	if (!process_->write(str)) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_WOULDBLOCK;
}

bool CStorjControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (operations_.empty() || !operations_.back()->waitForAsyncRequest) {
		LogMessage(MessageType::Debug_Info, L"Not waiting for request reply, ignoring request reply %d", pNotification->GetRequestID());
		return false;
	}

	operations_.back()->waitForAsyncRequest = false;

	RequestId const requestId = pNotification->GetRequestID();
	switch(requestId)
	{
	case reqId_fileexists:
		{
			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
	default:
		LogMessage(MessageType::Debug_Warning, L"Unknown async request reply id: %d", requestId);
		return false;
	}

	return true;
}

void CStorjControlSocket::ProcessReply(int result, std::wstring const& reply)
{
	result_ = result;
	response_ = reply;

	SetWait(false);

	if (operations_.empty()) {
		LogMessage(MessageType::Debug_Info, L"Skipping reply without active operation.");
		return;
	}

	auto & data = *operations_.back();
	int res = data.ParseResponse();
	if (res == FZ_REPLY_OK) {
		ResetOperation(FZ_REPLY_OK);
	}
	else if (res == FZ_REPLY_CONTINUE) {
		SendNextCommand();
	}
	else if (res & FZ_REPLY_DISCONNECTED) {
		DoClose(res);
	}
	else if (res & FZ_REPLY_ERROR) {
		if (data.opId == Command::connect) {
			DoClose(res | FZ_REPLY_DISCONNECTED);
		}
		else {
			ResetOperation(res);
		}
	}
}

int CStorjControlSocket::ResetOperation(int nErrorCode)
{
	LogMessage(MessageType::Debug_Verbose, L"CStorjControlSocket::ResetOperation(%d)", nErrorCode);

	if (!operations_.empty() && operations_.back()->opId == Command::connect) {
		auto &data = static_cast<CStorjConnectOpData &>(*operations_.back());
		if (data.opState == connect_init && nErrorCode & FZ_REPLY_ERROR && (nErrorCode & FZ_REPLY_CANCELED) != FZ_REPLY_CANCELED) {
			LogMessage(MessageType::Error, _("fzstorj could not be started"));
		}
	}
	if (!operations_.empty() && operations_.back()->opId == Command::del && !(nErrorCode & FZ_REPLY_DISCONNECTED)) {
		auto &data = static_cast<CStorjDeleteOpData &>(*operations_.back());
		if (data.needSendListing_) {
			SendDirectoryListingNotification(data.path_, false, false);
		}
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

int CStorjControlSocket::DoClose(int nErrorCode)
{
	engine_.GetRateLimiter().RemoveObject(this);

	if (process_) {
		process_->kill();
	}

	if (input_thread_) {
		input_thread_.reset();

		auto threadEventsFilter = [&](fz::event_loop::Events::value_type const& ev) -> bool {
			if (ev.first != this) {
				return false;
			}
			else if (ev.second->derived_type() == CStorjEvent::type() || ev.second->derived_type() == StorjTerminateEvent::type()) {
				return true;
			}
			return false;
		};

		event_loop_.filter_events(threadEventsFilter);
	}
	process_.reset();
	return CControlSocket::DoClose(nErrorCode);
}

void CStorjControlSocket::Cancel()
{
	if (GetCurrentCommandId() != Command::none) {
		DoClose(FZ_REPLY_CANCELED);
	}
}

void CStorjControlSocket::OnRateAvailable(CRateLimiter::rate_direction direction)
{
	//OnQuotaRequest(direction);
}

void CStorjControlSocket::OnQuotaRequest(CRateLimiter::rate_direction direction)
{
	/*int64_t bytes = GetAvailableBytes(direction);
	if (bytes > 0) {
		int b;
		if (bytes > INT_MAX) {
			b = INT_MAX;
		}
		else {
			b = bytes;
		}
		AddToStream(fz::sprintf(L"-%d%d,%d\n", direction, b, engine_.GetOptions().GetOptionVal(OPTION_SPEEDLIMIT_INBOUND + static_cast<int>(direction))));
		UpdateUsage(direction, b);
	}
	else if (bytes == 0) {
		Wait(direction);
	}
	else if (bytes < 0) {
		AddToStream(fz::sprintf(L"-%d-\n", direction));
	}*/
}

void CStorjControlSocket::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<CStorjEvent, StorjTerminateEvent>(ev, this,
		&CStorjControlSocket::OnStorjEvent,
		&CStorjControlSocket::OnTerminate)) {
		return;
	}

	CControlSocket::operator()(ev);
}

std::wstring CStorjControlSocket::QuoteFilename(std::wstring const& filename)
{
	return L"\"" + fz::replaced_substrings(filename, L"\"", L"\"\"") + L"\"";
}
