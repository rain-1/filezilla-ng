#ifndef FILEZILLA_ENGINE_LOGGIN_PRIVATE_HEADER
#define FILEZILLA_ENGINE_LOGGIN_PRIVATE_HEADER

#include "engineprivate.h"
#include <libfilezilla/format.hpp>
#include <libfilezilla/mutex.hpp>
#include <utility>

class CLogging
{
public:
	explicit CLogging(CFileZillaEnginePrivate & engine);
	virtual ~CLogging();

	CLogging(CLogging const&) = delete;
	CLogging& operator=(CLogging const&) = delete;

	template<typename String, typename...Args>
	void LogMessage(MessageType nMessageType, String&& msgFormat, Args&& ...args) const
	{
		if (!ShouldLog(nMessageType)) {
			return;
		}

		CLogmsgNotification *notification = new CLogmsgNotification(nMessageType);
		notification->msg = fz::to_wstring(fz::sprintf(std::forward<String>(msgFormat), std::forward<Args>(args)...));

		LogToFile(nMessageType, notification->msg);
		engine_.AddLogNotification(notification);
	}

	template<typename String>
	void LogMessageRaw(MessageType nMessageType, String&& msg) const
	{
		if (!ShouldLog(nMessageType)) {
			return;
		}

		CLogmsgNotification *notification = new CLogmsgNotification(nMessageType, fz::to_wstring(std::forward<String>(msg)));

		LogToFile(nMessageType, notification->msg);
		engine_.AddLogNotification(notification);
	}
	
	bool ShouldLog(MessageType nMessageType) const;

	// Only affects calling thread
	static void UpdateLogLevel(COptionsBase & options);

private:
	CFileZillaEnginePrivate & engine_;

	bool InitLogFile(fz::scoped_lock& l) const;
	void LogToFile(MessageType nMessageType, std::wstring const& msg) const;

	static bool m_logfile_initialized;
#ifdef FZ_WINDOWS
	static HANDLE m_log_fd;
#else
	static int m_log_fd;
#endif
	static std::string m_prefixes[static_cast<int>(MessageType::count)];
	static unsigned int m_pid;
	static int m_max_size;
	static fz::native_string m_file;

	static int m_refcount;

	static fz::mutex mutex_;

	static thread_local int debug_level_;
	static thread_local int raw_listing_;
};

#endif
