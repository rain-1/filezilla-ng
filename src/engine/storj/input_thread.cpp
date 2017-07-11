#include <filezilla.h>

#include "event.h"
#include "input_thread.h"
#include "storjcontrolsocket.h"

#include <libfilezilla/process.hpp>

CStorjInputThread::CStorjInputThread(CStorjControlSocket& owner, fz::process& proc)
	: process_(proc)
	, owner_(owner)
{
}

CStorjInputThread::~CStorjInputThread()
{
	thread_.join();
}

bool CStorjInputThread::spawn(fz::thread_pool & pool)
{
	if (!thread_) {
		thread_ = pool.spawn([this]() { entry(); });
	}
	return thread_.operator bool();
}

std::wstring CStorjInputThread::ReadLine(std::wstring &error)
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

void CStorjInputThread::entry()
{
	std::wstring error;
	while (error.empty()) {
		char readType = 0;
		int read = process_.read(&readType, 1);
		if (read != 1) {
			break;
		}

		readType -= '0';

		if (readType < 0 || readType >= static_cast<char>(storjEvent::count) ) {
			error = fz::sprintf(L"Unknown eventType %d", readType);
			break;
		}

		storjEvent eventType = static_cast<storjEvent>(readType);

		int lines{};
		switch (eventType)
		{
		case storjEvent::count:
		case storjEvent::Unknown:
			error = fz::sprintf(L"Unknown eventType %d", readType);
			break;
		case storjEvent::Done:
		case storjEvent::Recv:
		case storjEvent::Send:
		case storjEvent::UsedQuotaRecv:
		case storjEvent::UsedQuotaSend:
			break;
		case storjEvent::Reply:
		case storjEvent::Error:
		case storjEvent::ErrorMsg:
		case storjEvent::Verbose:
		case storjEvent::Info:
		case storjEvent::Status:
		case storjEvent::Transfer:
			lines = 1;
			break;
		case storjEvent::Listentry:
			lines = 4;
			break;
		};

		auto msg = new CStorjEvent;
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

	owner_.send_event<StorjTerminateEvent>(error);
}
