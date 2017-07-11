#ifndef FILEZILLA_ENGINE_STORJ_INPUT_THREAD_HEADER
#define FILEZILLA_ENGINE_STORJ_INPUT_THREAD_HEADER

class CStorjControlSocket;

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

	std::wstring ReadLine(std::wstring &error);

	void entry();

	fz::process& process_;
	CStorjControlSocket& owner_;

	fz::async_task thread_;
};

#endif
