#ifndef FILEZILLA_ENGINE_NOTIFICATION_HEADER
#define FILEZILLA_ENGINE_NOTIFICATION_HEADER

// Notification overview
// ---------------------

// To inform the application about what's happening, the engine sends
// some notifications to the application.
// The handler needs to derive from EngineNotificationHandler and implement
// the OnEngineEvent method which takes the engine as parameter.
// Whenever you get a notification event,
// CFileZillaEngine::GetNextNotification has to be called until it returns 0,
// or you will lose important notifications or your memory will fill with
// pending notifications.
//
// Note: It may be called from a worker thread.

// A special class of notifications are the asynchronous requests. These
// requests have to be answered. Once processed, call
// CFileZillaEngine::SetAsyncRequestReply to continue the current operation.

#include "local_path.h"
#include <libfilezilla/time.hpp>

class CFileZillaEngine;

class EngineNotificationHandler
{
public:
	virtual ~EngineNotificationHandler() {}

	virtual void OnEngineEvent(CFileZillaEngine* engine) = 0;
};

enum NotificationId
{
	nId_logmsg,				// notification about new messages for the message log
	nId_operation,			// operation reply codes
	nId_connection,			// connection information: connects, disconnects, timeouts etc..
	nId_transferstatus,		// transfer information: bytes transferes, transfer speed and such
	nId_listing,			// directory listings
	nId_asyncrequest,		// asynchronous request
	nId_active,				// sent if data gets either received or sent
	nId_data,				// for memory downloads, indicates that new data is available.
	nId_sftp_encryption,	// information about key exchange, encryption algorithms and so on for SFTP
	nId_local_dir_created	// local directory has been created
};

// Async request IDs
enum RequestId
{
	reqId_fileexists,		// Target file already exists, awaiting further instructions
	reqId_interactiveLogin, // gives a challenge prompt for a password
	reqId_hostkey,			// used only by SSH/SFTP to indicate new host key
	reqId_hostkeyChanged,	// used only by SSH/SFTP to indicate changed host key
	reqId_certificate		// sent after a successful TLS handshake to allow certificate
							// validation
};

class CNotification
{
public:
	virtual ~CNotification() {}; // TODO: One GCC >= 4.8 is in Debian Stable (Jessie by then), make default and add testcase to configure.
	virtual NotificationId GetID() const = 0;

protected:
	CNotification() = default;
	CNotification(CNotification const&) = default;
	CNotification& operator=(CNotification const&) = default;
};

template<NotificationId id>
class CNotificationHelper : public CNotification
{
public:
	virtual NotificationId GetID() const final { return id; }

protected:
	CNotificationHelper<id>() = default;
	CNotificationHelper<id>(CNotificationHelper<id> const&) = default;
	CNotificationHelper<id>& operator=(CNotificationHelper<id> const&) = default;
};

class CLogmsgNotification final : public CNotificationHelper<nId_logmsg>
{
public:
	explicit CLogmsgNotification(MessageType t)
		: msgType(t)
	{}

	template<typename String>
	CLogmsgNotification(MessageType t, String && m)
		: msg(std::forward<String>(m))
		, msgType(t)
	{
	}

	std::wstring msg;
	MessageType msgType{MessageType::Status}; // Type of message, see logging.h for details
};

// If CFileZillaEngine does return with FZ_REPLY_WOULDBLOCK, you will receive
// a nId_operation notification once the operation ends.
class COperationNotification final : public CNotificationHelper<nId_operation>
{
public:
	int nReplyCode{};
	Command commandId{Command::none};
};

// You get this type of notification everytime a directory listing has been
// requested explicitely or when a directory listing was retrieved implicitely
// during another operation, e.g. file transfers.
class CDirectoryListing;
class CDirectoryListingNotification final : public CNotificationHelper<nId_listing>
{
public:
	explicit CDirectoryListingNotification(const CServerPath& path, const bool modified = false, const bool failed = false);
	bool Modified() const { return m_modified; }
	bool Failed() const { return m_failed; }
	const CServerPath GetPath() const { return m_path; }

protected:
	bool m_modified{};
	bool m_failed{};
	CServerPath m_path;
};

class CAsyncRequestNotification : public CNotificationHelper<nId_asyncrequest>
{
public:
	virtual RequestId GetRequestID() const = 0;
	unsigned int requestNumber{}; // Do never change this

protected:
	CAsyncRequestNotification() = default;
	CAsyncRequestNotification(CAsyncRequestNotification const&) = default;
	CAsyncRequestNotification& operator=(CAsyncRequestNotification const&) = default;
};

class CFileExistsNotification final : public CAsyncRequestNotification
{
public:
	virtual RequestId GetRequestID() const;

	bool download{};

	std::wstring localFile;
	int64_t localSize{-1};
	fz::datetime localTime;

	std::wstring remoteFile;
	CServerPath remotePath;
	int64_t remoteSize{-1};
	fz::datetime remoteTime;

	bool ascii{};

	bool canResume{};

	// overwriteAction will be set by the request handler
	enum OverwriteAction : signed char
	{
		unknown = -1,
		ask,
		overwrite,
		overwriteNewer,	// Overwrite if source file is newer than target file
		overwriteSize,	// Overwrite if source file is is different in size than target file
		overwriteSizeOrNewer,	// Overwrite if source file is different in size or newer than target file
		resume, // Overwrites if cannot be resumed
		rename,
		skip,

		ACTION_COUNT
	};

	// Set overwriteAction to the desired action
	OverwriteAction overwriteAction{unknown};

	// Set to new filename if overwriteAction is rename. Might trigger further
	// file exists notifications if new target file exists as well.
	std::wstring newName;
};

class CInteractiveLoginNotification final : public CAsyncRequestNotification
{
public:
	enum type {
		interactive,
		keyfile
	};

	CInteractiveLoginNotification(type t, std::wstring const& challenge, bool repeated);
	virtual RequestId GetRequestID() const;

	// Set to true if you have set a password
	bool passwordSet{};

	// Set password by calling server.SetUser
	CServer server;

	std::wstring const& GetChallenge() const { return m_challenge; }

	type GetType() const { return m_type; }

	bool IsRepeated() const { return m_repeated; }

protected:
	// Password prompt string as given by the server
	std::wstring const m_challenge;

	type const m_type;

	bool const m_repeated;
};

// Indicate network action.
class CActiveNotification final : public CNotificationHelper<nId_active>
{
public:
	explicit CActiveNotification(int direction);

	int GetDirection() const { return m_direction; }
protected:
	const int m_direction;
};

class CTransferStatus final
{
public:
	CTransferStatus() {}
	CTransferStatus(int64_t total, int64_t start, bool l)
		: totalSize(total)
		, startOffset(start)
		, currentOffset(start)
		, list(l)
	{}

	fz::datetime started;
	int64_t totalSize{-1};		// Total size of the file to transfer, -1 if unknown
	int64_t startOffset{-1};
	int64_t currentOffset{-1};

	void clear() { startOffset = -1; }
	bool empty() const { return startOffset < 0; }

	explicit operator bool() const { return !empty(); }

	// True on download notifications iff currentOffset != startOffset.
	// True on FTP upload notifications iff currentOffset != startOffset
	// AND after the first accepted data after the first EWOULDBLOCK.
	// SFTP uploads: Set to true if currentOffset >= startOffset + 65536.
	bool madeProgress{};

	bool list{};
};

class CTransferStatusNotification final : public CNotificationHelper<nId_transferstatus>
{
public:
	CTransferStatusNotification() {}
	CTransferStatusNotification(CTransferStatus const& status);

	CTransferStatus const& GetStatus() const;

protected:
	CTransferStatus const status_;
};

class CSftpEncryptionDetails
{
public:
	virtual ~CSftpEncryptionDetails() = default;

	std::wstring hostKeyAlgorithm;
	std::wstring hostKeyFingerprintMD5;
	std::wstring hostKeyFingerprintSHA256;
	std::wstring kexAlgorithm;
	std::wstring kexHash;
	std::wstring kexCurve;
	std::wstring cipherClientToServer;
	std::wstring cipherServerToClient;
	std::wstring macClientToServer;
	std::wstring macServerToClient;
};

// Notification about new or changed hostkeys, only used by SSH/SFTP transfers.
// GetRequestID() returns either reqId_hostkey or reqId_hostkeyChanged
class CHostKeyNotification final : public CAsyncRequestNotification, public CSftpEncryptionDetails
{
public:
	CHostKeyNotification(std::wstring const& host, int port, CSftpEncryptionDetails const& details, bool changed = false);

	virtual RequestId GetRequestID() const;

	std::wstring GetHost() const;
	int GetPort() const;

	// Set to true if you trust the server
	bool m_trust{};

	// If m_truest is true, set this to true to always trust this server
	// in future.
	bool m_alwaysTrust{};

protected:

	const std::wstring m_host;
	const int m_port;
	const bool m_changed;
};

class CDataNotification final : public CNotificationHelper<nId_data>
{
public:
	CDataNotification(char* pData, int len);
	virtual ~CDataNotification();

	char* Detach(int& len);

protected:
	char* m_pData;
	unsigned int m_len;
};

class CCertificate final
{
public:
	CCertificate() = default;
	CCertificate(CCertificate const& op) = default;

	CCertificate(
		std::vector<uint8_t> const& rawData,
		fz::datetime const& activationTime, fz::datetime const& expirationTime,
		std::wstring const& serial,
		std::wstring const& pkalgoname, unsigned int bits,
		std::wstring const& signalgoname,
		std::wstring const& fingerprint_sha256,
		std::wstring const& fingerprint_sha1,
		std::wstring const& issuer,
		std::wstring const& subject,
		std::vector<std::wstring> const& altSubjectNames);

	CCertificate(
		std::vector<uint8_t> && rawdata,
		fz::datetime const& activationTime, fz::datetime const& expirationTime,
		std::wstring const& serial,
		std::wstring const& pkalgoname, unsigned int bits,
		std::wstring const& signalgoname,
		std::wstring const& fingerprint_sha256,
		std::wstring const& fingerprint_sha1,
		std::wstring const& issuer,
		std::wstring const& subject,
		std::vector<std::wstring> && altSubjectNames);


	std::vector<uint8_t> GetRawData() const { return m_rawData; }
	fz::datetime GetActivationTime() const { return m_activationTime; }
	fz::datetime GetExpirationTime() const { return m_expirationTime; }

	std::wstring const& GetSerial() const { return m_serial; }
	std::wstring const& GetPkAlgoName() const { return m_pkalgoname; }
	unsigned int GetPkAlgoBits() const { return m_pkalgobits; }

	std::wstring const& GetSignatureAlgorithm() const { return m_signalgoname; }

	std::wstring const& GetFingerPrintSHA256() const { return m_fingerprint_sha256; }
	std::wstring const& GetFingerPrintSHA1() const { return m_fingerprint_sha1; }

	std::wstring const& GetSubject() const { return m_subject; }
	std::wstring const& GetIssuer() const { return m_issuer; }

	std::vector<std::wstring> const& GetAltSubjectNames() const { return m_altSubjectNames; }

private:
	fz::datetime m_activationTime;
	fz::datetime m_expirationTime;

	std::vector<uint8_t> m_rawData{};

	std::wstring m_serial;
	std::wstring m_pkalgoname;
	unsigned int m_pkalgobits{};

	std::wstring m_signalgoname;

	std::wstring m_fingerprint_sha256;
	std::wstring m_fingerprint_sha1;

	std::wstring m_issuer;
	std::wstring m_subject;

	std::vector<std::wstring> m_altSubjectNames;
};

class CCertificateNotification final : public CAsyncRequestNotification
{
public:
	CCertificateNotification(std::wstring const& host, unsigned int port,
		std::wstring const& protocol,
		std::wstring const& keyExchange,
		std::wstring const& sessionCipher,
		std::wstring const& sessionMac,
		int algorithmWarnings,
		std::vector<CCertificate> && certificates);
	virtual RequestId GetRequestID() const { return reqId_certificate; }

	std::wstring const& GetHost() const { return m_host; }
	unsigned int GetPort() const { return m_port; }

	std::wstring const& GetSessionCipher() const { return m_sessionCipher; }
	std::wstring const& GetSessionMac() const { return m_sessionMac; }

	bool m_trusted{};

	const std::vector<CCertificate> GetCertificates() const { return m_certificates; }

	std::wstring const& GetProtocol() const { return m_protocol; }
	std::wstring const& GetKeyExchange() const { return m_keyExchange; }

	enum algorithm_warnings_t
	{
		tlsver = 1,
		cipher = 2,
		mac = 4,
		kex = 8
	};

	int GetAlgorithmWarnings() const { return m_algorithmWarnings; }

private:
	std::wstring m_host;
	unsigned int m_port{};

	std::wstring m_protocol;
	std::wstring m_keyExchange;
	std::wstring m_sessionCipher;
	std::wstring m_sessionMac;
	int m_algorithmWarnings{};

	std::vector<CCertificate> m_certificates;
};

class CSftpEncryptionNotification final : public CNotificationHelper<nId_sftp_encryption>, public CSftpEncryptionDetails
{
};

class CLocalDirCreatedNotification final : public CNotificationHelper<nId_local_dir_created>
{
public:
	CLocalPath dir;
};

#endif
