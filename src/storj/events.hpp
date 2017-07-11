#ifndef FILEZILLA_STORJ_EVENTS_HEADER
#define FILEZILLA_STORJ_EVENTS_HEADER

enum class storjEvent {
	Unknown = -1,
	Reply = 0,
	Done,
	Error,
	ErrorMsg,
	Verbose,
	Info,
	Status,
	Recv,
	Send,
	Listentry,
	Transfer,
	UsedQuotaRecv,
	UsedQuotaSend,

	count
};

#define FZSTORJ_PROTOCOL_VERSION 1

#endif

