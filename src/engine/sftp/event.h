#ifndef FILEZILLA_ENGINE_SFTP_EVENT_HEADER
#define FILEZILLA_ENGINE_SFTP_EVENT_HEADER

#define FZSFTP_PROTOCOL_VERSION 8

enum class sftpEvent {
	Unknown = -1,
	Reply = 0,
	Done,
	Error,
	Verbose,
	Info,
	Status,
	Recv,
	Send,
	Listentry,
	AskHostkey,
	AskHostkeyChanged,
	AskHostkeyBetteralg,
	AskPassword,
	Transfer,
	RequestPreamble,
	RequestInstruction,
	UsedQuotaRecv,
	UsedQuotaSend,
	KexAlgorithm,
	KexHash,
	KexCurve,
	CipherClientToServer,
	CipherServerToClient,
	MacClientToServer,
	MacServerToClient,
	Hostkey,

	count
};

struct sftp_message
{
	sftpEvent type;
	mutable std::wstring text[2];
};

struct sftp_event_type;
typedef fz::simple_event<sftp_event_type, sftp_message> CSftpEvent;

struct sftp_list_message
{
	mutable std::wstring text;
	mutable std::wstring name;
	uint64_t mtime;
};

struct sftp_list_event_type;
typedef fz::simple_event<sftp_list_event_type, sftp_list_message> CSftpListEvent;

struct terminate_event_type;
typedef fz::simple_event<terminate_event_type, std::wstring> CTerminateEvent;

#endif
