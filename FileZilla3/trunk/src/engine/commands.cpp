#include <filezilla.h>

CConnectCommand::CConnectCommand(CServer const& server, bool retry_connecting)
	: m_Server(server)
	, m_retry_connecting(retry_connecting)
{
}

CServer const& CConnectCommand::GetServer() const
{
	return m_Server;
}

CListCommand::CListCommand(int flags)
	: m_flags(flags)
{
}

CListCommand::CListCommand(CServerPath path, std::wstring const& subDir, int flags)
	: m_path(path), m_subDir(subDir), m_flags(flags)
{
}

CServerPath CListCommand::GetPath() const
{
	return m_path;
}

std::wstring CListCommand::GetSubDir() const
{
	return m_subDir;
}

bool CListCommand::valid() const
{
	if (GetPath().empty() && !GetSubDir().empty())
		return false;

	if (GetFlags() & LIST_FLAG_LINK && GetSubDir().empty())
		return false;

	bool const refresh = (m_flags & LIST_FLAG_REFRESH) != 0;
	bool const avoid = (m_flags & LIST_FLAG_AVOID) != 0;
	if (refresh && avoid)
		return false;

	return true;
}

CFileTransferCommand::CFileTransferCommand(std::wstring const& localFile, CServerPath const& remotePath,
										   std::wstring const& remoteFile, bool download,
										   CFileTransferCommand::t_transferSettings const& transferSettings)
	: m_localFile(localFile), m_remotePath(remotePath), m_remoteFile(remoteFile)
	, m_download(download)
	, m_transferSettings(transferSettings)
{
}

std::wstring CFileTransferCommand::GetLocalFile() const
{
	return m_localFile;
}

CServerPath CFileTransferCommand::GetRemotePath() const
{
	return m_remotePath;
}

std::wstring CFileTransferCommand::GetRemoteFile() const
{
	return m_remoteFile;
}

bool CFileTransferCommand::Download() const
{
	return m_download;
}

CRawCommand::CRawCommand(std::wstring const& command)
{
	m_command = command;
}

std::wstring CRawCommand::GetCommand() const
{
	return m_command;
}

CDeleteCommand::CDeleteCommand(const CServerPath& path, std::deque<std::wstring>&& files)
	: m_path(path), m_files(files)
{
}

CRemoveDirCommand::CRemoveDirCommand(const CServerPath& path, std::wstring const& subDir)
	: m_path(path), m_subDir(subDir)
{
}

bool CRemoveDirCommand::valid() const
{
	return !GetPath().empty() && !GetSubDir().empty();
}

CMkdirCommand::CMkdirCommand(const CServerPath& path)
	: m_path(path)
{
}

bool CMkdirCommand::valid() const
{
	return !GetPath().empty() && GetPath().HasParent();
}

CRenameCommand::CRenameCommand(CServerPath const& fromPath, std::wstring const& fromFile,
							   CServerPath const& toPath, std::wstring const& toFile)
	: m_fromPath(fromPath)
	, m_toPath(toPath)
	, m_fromFile(fromFile)
	, m_toFile(toFile)
{}

bool CRenameCommand::valid() const
{
	return !GetFromPath().empty() && !GetToPath().empty() && !GetFromFile().empty() && !GetToFile().empty();
}

CChmodCommand::CChmodCommand(CServerPath const& path, std::wstring const& file, std::wstring const& permission)
	: m_path(path)
	, m_file(file)
	, m_permission(permission)
{}

bool CChmodCommand::valid() const
{
	return !GetPath().empty() && !GetFile().empty() && !GetPermission().empty();
}