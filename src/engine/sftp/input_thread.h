#ifndef FILEZILLA_ENGINE_SFTP_INPUTTHREAD_HEADER
#define FILEZILLA_ENGINE_SFTP_INPUTTHREAD_HEADER

class CSftpControlSocket;

#include <libfilezilla/buffer.hpp>
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

	bool readFromProcess(std::wstring & error, bool eof_is_error);
	std::wstring ReadLine(std::wstring & error);
	uint64_t ReadUInt(std::wstring & error);

	void entry();

	void processEvent(sftpEvent eventType, std::wstring & error);

	fz::process& process_;
	CSftpControlSocket& owner_;

	fz::async_task thread_;

	fz::buffer recv_buffer_;
};

#endif
