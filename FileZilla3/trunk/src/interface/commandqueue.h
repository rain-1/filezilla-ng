#ifndef __COMMANDQUEUE_H__
#define __COMMANDQUEUE_H__

class CFileZillaEngine;
class CNotification;
class CState;
class CMainFrame;

DECLARE_EVENT_TYPE(fzEVT_GRANTEXCLUSIVEENGINEACCESS, -1)

class CCommandQueue
{
public:
	enum command_origin
	{
		any = -1,
		normal, // Most user actions
		recursiveOperation
	};

	CCommandQueue(CFileZillaEngine *pEngine, CMainFrame* pMainFrame, CState& state);
	~CCommandQueue();

	void ProcessCommand(CCommand *pCommand, command_origin origin = normal);
	void ProcessNextCommand();
	bool Idle(command_origin origin = any) const;
	bool Cancel();
	bool Quit();
	void Finish(std::unique_ptr<COperationNotification> && pNotification);

	void RequestExclusiveEngine(bool requestExclusive);

	CFileZillaEngine* GetEngineExclusive(int requestId);
	void ReleaseEngine();
	bool EngineLocked() const { return m_exclusiveEngineLock; }

	void ProcessDirectoryListing(CDirectoryListingNotification const& listingNotification);

protected:
	void ProcessReply(int nReplyCode, Command commandId);

	void GrantExclusiveEngineRequest();

	CFileZillaEngine *m_pEngine;
	CMainFrame* m_pMainFrame;
	CState& m_state;
	bool m_exclusiveEngineRequest;
	bool m_exclusiveEngineLock;
	int m_requestId;
	static int m_requestIdCounter;

	// Used to make this class reentrance-safe
	int m_inside_commandqueue{};

	struct CommandInfo {
		CommandInfo() = default;
		CommandInfo(command_origin o, std::unique_ptr<CCommand> && c)
			: origin(o)
			, command(std::move(c))
		{}

		command_origin origin;
		std::unique_ptr<CCommand> command;
		bool didReconnect{};
	};
	std::deque<CommandInfo> m_CommandList;

	bool m_quit{};
};

#endif //__COMMANDQUEUE_H__

