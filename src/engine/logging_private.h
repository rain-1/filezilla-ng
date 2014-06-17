#ifndef __LOGGING_PRIVATE_H__
#define __LOGGING_PRIVATE_H__

#include <utility>

class CLogging
{
public:
	CLogging(CFileZillaEnginePrivate *pEngine);
	virtual ~CLogging();

	template<typename...Args>
	void LogMessage(MessageType nMessageType, wxChar const* msgFormat, Args&& ...args) const
	{
		if( !ShouldLog(nMessageType) ) {
			return;
		}

		CLogmsgNotification *notification = new CLogmsgNotification;
		notification->msgType = nMessageType;
		notification->msg.Printf(msgFormat, std::forward<Args>(args)...);

		LogToFile(nMessageType, notification->msg);
		m_pEngine->AddNotification(notification);
	}

	void LogMessageRaw(MessageType nMessageType, const wxChar *msg) const;

	template<typename...Args>
	void LogMessage(wxString sourceFile, int nSourceLine, void *pInstance, MessageType nMessageType
					, wxChar const* msgFormat, Args&& ...args) const
	{
		if( !ShouldLog(nMessageType) ) {
			return;
		}

		int pos = sourceFile.Find('\\', true);
		if (pos != -1)
			sourceFile = sourceFile.Mid(pos+1);

		pos = sourceFile.Find('/', true);
		if (pos != -1)
			sourceFile = sourceFile.Mid(pos+1);

		wxString text = wxString::Format(msgFormat, std::forward<Args>(args)...);

		CLogmsgNotification *notification = new CLogmsgNotification;
		notification->msgType = nMessageType;
		notification->msg.Printf(_T("%s(%d): %s   caller=%p"), sourceFile, nSourceLine, text, pInstance);

		LogToFile(nMessageType, notification->msg);
		m_pEngine->AddNotification(notification);
	}

	bool ShouldLog(MessageType nMessageType) const;

private:
	CFileZillaEnginePrivate *m_pEngine;

	void InitLogFile() const;
	void LogToFile(MessageType nMessageType, const wxString& msg) const;

	static bool m_logfile_initialized;
#ifdef __WXMSW__
	static HANDLE m_log_fd;
#else
	static int m_log_fd;
#endif
	static wxString m_prefixes[MessageTypeCount];
	static unsigned int m_pid;
	static int m_max_size;
	static wxString m_file;

	static int m_refcount;
};

#endif
