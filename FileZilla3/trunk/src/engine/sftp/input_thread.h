#ifndef FILEZILLA_ENGINE_SFTP_INPUTTHREAD_HEADER
#define FILEZILLA_ENGINE_SFTP_INPUTTHREAD_HEADER

class CSftpControlSocket;

#include <libfilezilla/thread_pool.hpp>

namespace fz {
class process;
}

class CSftpInputThread final
{
public:
	CSftpInputThread(CSftpControlSocket & owner, fz::process& proc);
	~CSftpInputThread();

	bool spawn(fz::thread_pool & pool);

protected:

	std::wstring ReadLine(std::wstring &error);

	void entry();

	fz::process& process_;
	CSftpControlSocket& owner_;

	fz::async_task thread_;
};

#endif
