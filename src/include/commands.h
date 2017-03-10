#ifndef FILEZILLA_ENGINE_COMMANDS_HEADER
#define FILEZILLA_ENGINE_COMMANDS_HEADER

// See below for actual commands and their parameters

// Command IDs
// -----------
enum class Command
{
	none = 0,
	connect,
	disconnect,
	list,
	transfer,
	del,
	removedir,
	mkdir,
	rename,
	chmod,
	raw,

	// Only used internally
	cwd,
	private1,
	private2,
	private3
};

// Reply codes
// -----------
#define FZ_REPLY_OK				(0x0000)
#define FZ_REPLY_WOULDBLOCK		(0x0001)
#define FZ_REPLY_ERROR			(0x0002)
#define FZ_REPLY_CRITICALERROR	(0x0004 | FZ_REPLY_ERROR) // If there is no point to retry an operation, this
														  // code is returned.
#define FZ_REPLY_CANCELED		(0x0008 | FZ_REPLY_ERROR)
#define FZ_REPLY_SYNTAXERROR	(0x0010 | FZ_REPLY_ERROR)
#define FZ_REPLY_NOTCONNECTED	(0x0020 | FZ_REPLY_ERROR)
#define FZ_REPLY_DISCONNECTED	(0x0040)
#define FZ_REPLY_INTERNALERROR	(0x0080 | FZ_REPLY_ERROR) // If you get this reply, the error description will be
														  // given by the last Debug_Warning log message. This
														  // should not happen unless there is a bug in FileZilla 3.
#define FZ_REPLY_BUSY			(0x0100 | FZ_REPLY_ERROR)
#define FZ_REPLY_ALREADYCONNECTED	(0x0200 | FZ_REPLY_ERROR) // Will be returned by connect if already connected
#define FZ_REPLY_PASSWORDFAILED	0x0400 // Will be returned if PASS fails with 5yz reply code.
#define FZ_REPLY_TIMEOUT		(0x0800 | FZ_REPLY_ERROR)
#define FZ_REPLY_NOTSUPPORTED	(0x1000 | FZ_REPLY_ERROR) // Will be returned if command not supported by that protocol
#define FZ_REPLY_WRITEFAILED	(0x2000 | FZ_REPLY_ERROR) // Happens if local file could not be written during transfer
#define FZ_REPLY_LINKNOTDIR		(0x4000 | FZ_REPLY_ERROR)

#define FZ_REPLY_CONTINUE 0x8000 // Used internally

// --------------- //
// Actual commands //
// --------------- //

class CCommand
{
public:
	CCommand() = default;
	virtual ~CCommand() {}; // TODO: One GCC >= 4.8 is in Debian Stable (Jessie by then), make default and add testcase to configure.

	virtual Command GetId() const = 0;
	virtual CCommand *Clone() const = 0;

	virtual bool valid() const { return true; }

protected:
	CCommand(CCommand const&) = default;
	CCommand& operator=(CCommand const&) = default;
};

template<typename Derived, Command id>
class CCommandHelper : public CCommand
{
public:
	virtual Command GetId() const final { return id; }

	virtual CCommand* Clone() const final {
		return new Derived(static_cast<Derived const&>(*this));
	}

protected:
	CCommandHelper<Derived, id>() = default;
	CCommandHelper<Derived, id>(CCommandHelper<Derived, id> const&) = default;
	CCommandHelper<Derived, id>& operator=(CCommandHelper<Derived, id> const&) = default;
};

template<Command id>
class CBasicCommand final : public CCommandHelper<CBasicCommand<id>, id>
{
};

class CConnectCommand final : public CCommandHelper<CConnectCommand, Command::connect>
{
public:
	explicit CConnectCommand(CServer const& server, bool retry_conncting = true);

	CServer const& GetServer() const;
	bool RetryConnecting() const { return m_retry_connecting; }
protected:
	CServer const m_Server;
	bool const m_retry_connecting;
};

typedef CBasicCommand<Command::disconnect> CDisconnectCommand;

#define LIST_FLAG_REFRESH 1
#define LIST_FLAG_AVOID 2
#define LIST_FLAG_FALLBACK_CURRENT 4
#define LIST_FLAG_LINK 8
class CListCommand final : public CCommandHelper<CListCommand, Command::list>
{
	// Without a given directory, the current directory will be listed.
	// Directories can either be given as absolute path or as
	// pair of an absolute path and the very last path segments.

	// Set LIST_FLAG_REFRESH to get a directory listing even if a cache
	// lookup can be made after finding out true remote directory.
	//
	// Set LIST_FLAG_AVOID to get a directory listing only if cache lookup
	// fails or contains unsure entries, otherwise don't send listing.
	//
	// If LIST_FLAG_FALLBACK_CURRENT is set and CWD fails, list whatever
	// directory we are currently in. Useful for initial reconnect to the
	// server when we don't know if remote directory still exists
	//
	// LIST_FLAG_LINK is used for symlink discovery. There's unfortunately
	// no sane way to distinguish between symlinks to files and symlinks to
	// directories.
public:
	explicit CListCommand(int flags = 0);
	explicit CListCommand(CServerPath path, std::wstring const& subDir = std::wstring(), int flags = 0);

	CServerPath GetPath() const;
	std::wstring GetSubDir() const;

	int GetFlags() const { return m_flags; }

	bool valid() const;

protected:
	CServerPath const m_path;
	std::wstring const m_subDir;
	int const m_flags;
};

class CFileTransferCommand final : public CCommandHelper<CFileTransferCommand, Command::transfer>
{
public:
	class t_transferSettings final
	{
	public:
		t_transferSettings()
			: binary(true)
		{}

		bool binary;
	};

	// For uploads, set download to false.
	// For downloads, localFile can be left empty if supported by protocol.
	// Check for nId_data notification.
	// FIXME: localFile empty iff protocol is HTTP.
	CFileTransferCommand(std::wstring const& localFile, CServerPath const& remotePath, std::wstring const& remoteFile, bool download, t_transferSettings const& m_transferSettings);

	std::wstring GetLocalFile() const;
	CServerPath GetRemotePath() const;
	std::wstring GetRemoteFile() const;
	bool Download() const;
	const t_transferSettings& GetTransferSettings() const { return m_transferSettings; }

protected:
	std::wstring const m_localFile;
	CServerPath const m_remotePath;
	std::wstring const m_remoteFile;
	bool const m_download;
	t_transferSettings const m_transferSettings;
};

class CRawCommand final : public CCommandHelper<CRawCommand, Command::raw>
{
public:
	explicit CRawCommand(std::wstring const& command);

	std::wstring GetCommand() const;

	bool valid() const { return !m_command.empty(); }

protected:
	std::wstring m_command;
};

class CDeleteCommand final : public CCommandHelper<CDeleteCommand, Command::del>
{
public:
	CDeleteCommand(CServerPath const& path, std::deque<std::wstring> && files);

	CServerPath GetPath() const { return m_path; }
	const std::deque<std::wstring>& GetFiles() const { return m_files; }
	std::deque<std::wstring>&& ExtractFiles() { return std::move(m_files); }

	bool valid() const { return !GetPath().empty() && !GetFiles().empty(); }
protected:
	CServerPath const m_path;
	std::deque<std::wstring> m_files;
};

class CRemoveDirCommand final : public CCommandHelper<CRemoveDirCommand, Command::removedir>
{
public:
	// Directories can either be given as absolute path or as
	// pair of an absolute path and the very last path segments.
	CRemoveDirCommand(CServerPath const& path, std::wstring const& subdDir);

	CServerPath GetPath() const { return m_path; }
	std::wstring GetSubDir() const { return m_subDir; }

	bool valid() const;

protected:
	CServerPath const m_path;
	std::wstring const m_subDir;
};

class CMkdirCommand final : public CCommandHelper<CMkdirCommand, Command::mkdir>
{
public:
	explicit CMkdirCommand(CServerPath const& path);

	CServerPath GetPath() const { return m_path; }

	bool valid() const;

protected:
	CServerPath const m_path;
};

class CRenameCommand final : public CCommandHelper<CRenameCommand, Command::rename>
{
public:
	CRenameCommand(CServerPath const& fromPath, std::wstring const& fromFile,
				   CServerPath const& toPath, std::wstring const& toFile);

	CServerPath GetFromPath() const { return m_fromPath; }
	CServerPath GetToPath() const { return m_toPath; }
	std::wstring GetFromFile() const { return m_fromFile; }
	std::wstring GetToFile() const { return m_toFile; }

	bool valid() const;

protected:
	CServerPath const m_fromPath;
	CServerPath const m_toPath;
	std::wstring const m_fromFile;
	std::wstring const m_toFile;
};

class CChmodCommand final : public CCommandHelper<CChmodCommand, Command::chmod>
{
public:
	// The permission string should be given in a format understandable by the server.
	// Most likely it's the defaut octal representation used by the unix chmod command,
	// i.e. chmod 755 foo.bar
	CChmodCommand(CServerPath const& path, std::wstring const& file, std::wstring const& permission);

	CServerPath GetPath() const { return m_path; }
	std::wstring GetFile() const { return m_file; }
	std::wstring GetPermission() const { return m_permission; }

	bool valid() const;

protected:
	CServerPath const m_path;
	std::wstring const m_file;
	std::wstring const m_permission;
};

#endif
