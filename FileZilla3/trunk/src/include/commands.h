#ifndef __COMMANDS_H__
#define __COMMANDS_H__

// See below for actual commands and their parameters

// Command IDs
// -----------
enum class Command
{
	none = 0,
	connect,
	disconnect,
	cancel,
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
	rawtransfer
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
#define FZ_REPLY_LINKNOTDIR		(0x4000)

// --------------- //
// Actual commands //
// --------------- //

class CCommand
{
public:
	CCommand() = default;
	virtual ~CCommand() {};

	virtual Command GetId() const = 0;
	virtual CCommand *Clone() const = 0;

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
	explicit CConnectCommand(const CServer &server, bool retry_conncting = true);

	const CServer GetServer() const;
	bool RetryConnecting() const { return m_retry_connecting; }
protected:
	CServer m_Server;
	bool m_retry_connecting;
};

typedef CBasicCommand<Command::disconnect> CDisconnectCommand;
typedef CBasicCommand<Command::cancel> CCancelCommand;

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
	explicit CListCommand(CServerPath path, wxString subDir = wxString(), int flags = 0);

	CServerPath GetPath() const;
	wxString GetSubDir() const;

	int GetFlags() const { return m_flags; }

protected:
	CServerPath m_path;
	wxString m_subDir;
	int m_flags;
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
	CFileTransferCommand(const wxString &localFile, const CServerPath& remotePath, const wxString &remoteFile, bool download, const t_transferSettings& m_transferSettings);

	wxString GetLocalFile() const;
	CServerPath GetRemotePath() const;
	wxString GetRemoteFile() const;
	bool Download() const;
	const t_transferSettings& GetTransferSettings() const { return m_transferSettings; }

protected:
	wxString m_localFile;
	CServerPath m_remotePath;
	wxString m_remoteFile;
	bool m_download;
	t_transferSettings m_transferSettings;
};

class CRawCommand final : public CCommandHelper<CRawCommand, Command::raw>
{
public:
	explicit CRawCommand(const wxString &command);

	wxString GetCommand() const;

protected:
	wxString m_command;
};

class CDeleteCommand final : public CCommandHelper<CDeleteCommand, Command::del>
{
public:
	CDeleteCommand(const CServerPath& path, const std::list<wxString>& files);

	CServerPath GetPath() const { return m_path; }
	const std::list<wxString>& GetFiles() const { return m_files; }

protected:
	const CServerPath m_path;
	const std::list<wxString> m_files;
};

class CRemoveDirCommand final : public CCommandHelper<CRemoveDirCommand, Command::removedir>
{
public:
	// Directories can either be given as absolute path or as
	// pair of an absolute path and the very last path segments.
	CRemoveDirCommand(const CServerPath& path, const wxString& subdDir);

	CServerPath GetPath() const { return m_path; }
	wxString GetSubDir() const { return m_subDir; }

protected:
	CServerPath m_path;
	wxString m_subDir;
};

class CMkdirCommand final : public CCommandHelper<CMkdirCommand, Command::mkdir>
{
public:
	explicit CMkdirCommand(const CServerPath& path);

	CServerPath GetPath() const { return m_path; }

protected:
	CServerPath m_path;
};

class CRenameCommand final : public CCommandHelper<CRenameCommand, Command::rename>
{
public:
	CRenameCommand(const CServerPath& fromPath, const wxString& fromFile,
				   const CServerPath& toPath, const wxString& toFile);

	CServerPath GetFromPath() const { return m_fromPath; }
	CServerPath GetToPath() const { return m_toPath; }
	wxString GetFromFile() const { return m_fromFile; }
	wxString GetToFile() const { return m_toFile; }

protected:
	CServerPath m_fromPath;
	CServerPath m_toPath;
	wxString m_fromFile;
	wxString m_toFile;
};

class CChmodCommand final : public CCommandHelper<CChmodCommand, Command::chmod>
{
public:
	// The permission string should be given in a format understandable by the server.
	// Most likely it's the defaut octal representation used by the unix chmod command,
	// i.e. chmod 755 foo.bar
	CChmodCommand(const CServerPath& path, const wxString& file, const wxString& permission);

	CServerPath GetPath() const { return m_path; }
	wxString GetFile() const { return m_file; }
	wxString GetPermission() const { return m_permission; }

protected:
	CServerPath m_path;
	wxString m_file;
	wxString m_permission;
};

#endif
