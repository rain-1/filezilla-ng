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

std::wstring CSftpInputThread::ReadLine(std::wstring &error)
{
	int len = 0;
	const int buffersize = 4096;
	char buffer[buffersize];

	while (true) {
		char c;
		int read = process_.read(&c, 1);
		if (read != 1) {
			if (!read) {
				error = L"Unexpected EOF.";
			}
			else {
				error = L"Unknown error reading from process";
			}
			return std::wstring();
		}

		if (c == '\n') {
			break;
		}

		if (len == buffersize - 1) {
			// Cap string length
			continue;
		}

		buffer[len++] = c;
	}

	while (len && buffer[len - 1] == '\r') {
		--len;
	}

	std::wstring const line = owner_.ConvToLocal(buffer, len);
	if (len && line.empty()) {
		error = L"Failed to convert reply to local character set.";
	}

	return line;
}

void CSftpInputThread::entry()
{
	std::wstring error;
	while (error.empty()) {
		char readType = 0;
		int read = process_.read(&readType, 1);
		if (read != 1) {
			break;
		}

		readType -= '0';

		if (readType < 0 || readType >= static_cast<char>(sftpEvent::count)) {
			error = fz::sprintf(L"Unknown eventType %d", readType);
			break;
		}

		sftpEvent eventType = static_cast<sftpEvent>(readType);

		int lines{};
		switch (eventType)
		{
		case sftpEvent::count:
		case sftpEvent::Unknown:
			error = fz::sprintf(L"Unknown eventType %d", readType);
			break;
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
			lines = 3;
			break;
		};

		auto msg = new CSftpEvent;
		auto & message = std::get<0>(msg->v_);
		message.type = eventType;
		for (int i = 0; i < lines && error.empty(); ++i) {
			message.text[i] = ReadLine(error);
		}

		if (!error.empty()) {
			delete msg;
			break;
		}

		owner_.send_event(msg);
	}

	owner_.send_event<CTerminateEvent>(error);
}
