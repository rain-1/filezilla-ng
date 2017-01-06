#define FZSFTP_PROTOCOL_VERSION 8

typedef enum
{
    sftpUnknown = -1,
    sftpReply = 0,
    sftpDone,
    sftpError,
    sftpVerbose,
	sftpInfo,
    sftpStatus,
    sftpRecv, /* socket */
    sftpSend, /* socket */
    sftpListentry,
    sftpAskHostkey,
    sftpAskHostkeyChanged,
    sftpAskHostkeyBetteralg,
    sftpAskPassword,
    sftpTransfer, /* payload: when written to local file (download) or acknowledged by server (upload) */
    sftpRequestPreamble,
    sftpRequestInstruction,
    sftpUsedQuotaRecv,
    sftpUsedQuotaSend,
    sftpKexAlgorithm,
    sftpKexHash,
    sftpKexCurve,
    sftpCipherClientToServer,
    sftpCipherServerToClient,
    sftpMacClientToServer,
    sftpMacServerToClient,
    sftpHostkey
} sftpEventTypes;

int fznotify(sftpEventTypes type);

// Format the string. Each line of the string is prepended by type
int fzprintf(sftpEventTypes type, const char* p, ...);

// Format the string, then print the type and the string as-is. Caller is responsible to add trailing linebreak
int fzprintf_raw(sftpEventTypes type, const char* p, ...);

// Format the string, then print the type (if not sftpUnknown) and the string with linebreaks replaced by spaces.
int fzprintf_raw_untrusted(sftpEventTypes type, const char* p, ...);
int fznotify1(sftpEventTypes type, int data);
