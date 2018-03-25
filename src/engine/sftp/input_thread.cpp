#include <filezilla.h>

#include "event.h"
#include "input_thread.h"
#include "sftpcontrolsocket.h"

#include <libfilezilla/process.hpp>

CSftpInputThread::CSftpInputThread(CSftpControlSocket& owner, fz::process& proc)
	: process_(proc)
	, owner_(owner)
{
}

CSftpInputThread::~CSftpInputThread()
{
	thread_.join();
}

bool CSftpInputThread::spawn(fz::thread_pool & pool)
{
	if (!thread_) {
		thread_ = pool.spawn([this]() { entry(); });
	}
	return thread_.operator bool();
}

uint64_t CSftpInputThread::ReadUInt(std::wstring &error)
{
	uint64_t ret{};

	while (true) {
		if (!readFromProcess(error, true)) {
			return 0;
		}

		auto const* p = recv_buffer_.get();
		size_t i;
		for (i = 0; i < recv_buffer_.size(); ++i) {
			unsigned char const c = p[i];
			if (c == '\n') {
				recv_buffer_.consume(i + 1);
				return ret;
			}
			if (c == '\r') {
				continue;
			}

			if (c < '0' || c > '9') {
				error = L"Unexpected character";
				return 0;
			}
			ret *= 10;
			ret += c - '0';

		}
		recv_buffer_.clear();
	}

	return 0;
}

std::wstring CSftpInputThread::ReadLine(std::wstring &error)
{
	int len = 0;
	const int buffersize = 4096;
	char buffer[buffersize];

	while (true) {
		if (!readFromProcess(error, true)) {
			return std::wstring();
		}

		auto const* p = recv_buffer_.get();
		size_t i;
		for (i = 0; i < recv_buffer_.size(); ++i) {
			unsigned char const c = p[i];

			if (c == '\n') {
				recv_buffer_.consume(i + 1);

				while (len && buffer[len - 1] == '\r') {
					--len;
				}

				std::wstring const line = owner_.ConvToLocal(buffer, len);
				if (len && line.empty()) {
					error = L"Failed to convert reply to local character set.";
				}

				return line;
			}

			if (len >= buffersize - 1) {
				// Cap string length
				continue;
			}

			buffer[len++] = c;
		}
		recv_buffer_.clear();
	}

	return std::wstring();
}

bool CSftpInputThread::readFromProcess(std::wstring & error, bool eof_is_error)
{
	if (recv_buffer_.empty()) {
		int read = process_.read(reinterpret_cast<char *>(recv_buffer_.get(1024)), 1024);
		if (read > 0) {
			recv_buffer_.add(read);
		}
		else {
			if (!read) {
				if (eof_is_error) {
					error = L"Unexpected EOF.";
				}
			}
			else {
				error = L"Unknown error reading from process";
			}
			return false;
		}
	}

	return true;
}

void CSftpInputThread::processEvent(sftpEvent eventType, std::wstring & error)
{
	int lines{};
	switch (eventType)
	{
	case sftpEvent::count:
	case sftpEvent::Unknown:
		error = fz::sprintf(L"Unknown eventType");
		return;
	case sftpEvent::Recv:
	case sftpEvent::Send:
	case sftpEvent::UsedQuotaRecv:
	case sftpEvent::UsedQuotaSend:
		break;
	case sftpEvent::Reply:
	case sftpEvent::Done:
	case sftpEvent::Error:
	case sftpEvent::Verbose:
	case sftpEvent::Info:
	case sftpEvent::Status:
	case sftpEvent::Transfer:
	case sftpEvent::AskPassword:
	case sftpEvent::RequestPreamble:
	case sftpEvent::RequestInstruction:
	case sftpEvent::KexAlgorithm:
	case sftpEvent::KexHash:
	case sftpEvent::KexCurve:
	case sftpEvent::CipherClientToServer:
	case sftpEvent::CipherServerToClient:
	case sftpEvent::MacClientToServer:
	case sftpEvent::MacServerToClient:
	case sftpEvent::Hostkey:
		lines = 1;
		break;
	case sftpEvent::AskHostkey:
	case sftpEvent::AskHostkeyChanged:
	case sftpEvent::AskHostkeyBetteralg:
		lines = 2;
		break;
	case sftpEvent::Listentry:
		{
			auto msg = new CSftpListEvent;
			auto & message = std::get<0>(msg->v_);
			message.text = ReadLine(error);
			message.mtime = ReadUInt(error);
			message.name = ReadLine(error);

			if (error.empty()) {
				owner_.send_event(msg);
			}
			else {
				delete msg;
			}
		}
		return;
	};

	auto msg = new CSftpEvent;
	auto & message = std::get<0>(msg->v_);
	message.type = eventType;
	for (int i = 0; i < lines && error.empty(); ++i) {
		message.text[i] = ReadLine(error);
	}

	if (!error.empty()) {
		delete msg;
		return;
	}

	owner_.send_event(msg);
}

void CSftpInputThread::entry()
{
	std::wstring error;
	while (error.empty()) {
		if (!readFromProcess(error, false)) {
			break;
		}

		unsigned char readType = *recv_buffer_.get();
		recv_buffer_.consume(1);

		readType -= '0';

		if (readType >= static_cast<unsigned char>(sftpEvent::count)) {
			error = fz::sprintf(L"Unknown eventType %d", readType);
			break;
		}

		sftpEvent eventType = static_cast<sftpEvent>(readType);

		processEvent(eventType, error);
	}

	owner_.send_event<CTerminateEvent>(error);
}
