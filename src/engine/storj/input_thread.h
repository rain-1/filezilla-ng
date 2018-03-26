#ifndef FILEZILLA_ENGINE_STORJ_INPUT_THREAD_HEADER
#define FILEZILLA_ENGINE_STORJ_INPUT_THREAD_HEADER

class CStorjControlSocket;

#include <libfilezilla/buffer.hpp>
#include <libfilezilla/thread_pool.hpp>

namespace fz {
class process;
}

class CStorjInputThread final
{
public:
	CStorjInputThread(CStorjControlSocket & owner, fz::process& proc);
	~CStorjInputThread();

	bool spawn(fz::thread_pool & pool);

protected:

	bool readFromProcess(std::wstring & error, bool eof_is_error);
	std::wstring ReadLine(std::wstring &error);

	void entry();

	void processEvent(storjEvent eventType, std::wstring & error);

	fz::process& process_;
	CStorjControlSocket& owner_;

	fz::async_task thread_;

	fz::buffer recv_buffer_;
};

#endif
