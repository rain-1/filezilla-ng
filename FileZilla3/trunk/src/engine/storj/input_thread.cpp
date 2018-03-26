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

			if (len == buffersize - 1) {
				// Cap string length
				continue;
			}

			buffer[len++] = c;
		}
		recv_buffer_.clear();
	}

	return std::wstring();
}

bool CStorjInputThread::readFromProcess(std::wstring & error, bool eof_is_error)
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

void CStorjInputThread::processEvent(storjEvent eventType, std::wstring &error)
{
	int lines{};
	switch (eventType)
	{
	case storjEvent::count:
	case storjEvent::Unknown:
		error = fz::sprintf(L"Unknown eventType");
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
		return;
	}

	owner_.send_event(msg);
}

void CStorjInputThread::entry()
{
	std::wstring error;
	while (error.empty()) {
		if (!readFromProcess(error, false)) {
			break;
		}

		unsigned char readType = *recv_buffer_.get();
		recv_buffer_.consume(1);

		readType -= '0';

		if (readType >= static_cast<unsigned char>(storjEvent::count) ) {
			error = fz::sprintf(L"Unknown eventType %d", readType);
			break;
		}

		storjEvent eventType = static_cast<storjEvent>(readType);

		processEvent(eventType, error);
	}

	owner_.send_event<StorjTerminateEvent>(error);
}
