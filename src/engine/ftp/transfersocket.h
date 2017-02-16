#ifndef FILEZILLA_ENGINE_FTP_TRANSFERSOCKET_HEADER
#define FILEZILLA_ENGINE_FTP_TRANSFERSOCKET_HEADER

#include "iothread.h"
#include "backend.h"
#include "ControlSocket.h"

class CFileZillaEnginePrivate;
class CFtpControlSocket;
class CDirectoryListingParser;

enum class TransferMode
{
	list,
	upload,
	download,
	resumetest
};

class CIOThread;
class CTlsSocket;
class CTransferSocket final : public fz::event_handler
{
public:
	CTransferSocket(CFileZillaEnginePrivate & engine, CFtpControlSocket & controlSocket, TransferMode transferMode);
	virtual ~CTransferSocket();

	std::wstring SetupActiveTransfer(std::string const& ip);
	bool SetupPassiveTransfer(std::wstring const& host, int port);

	void SetActive();

	CDirectoryListingParser *m_pDirectoryListingParser{};

	bool m_binaryMode{true};

	TransferEndReason GetTransferEndreason() const { return m_transferEndReason; }

	void SetIOThread(CIOThread* ioThread) { ioThread_ = ioThread; }

protected:
	bool CheckGetNextWriteBuffer();
	bool CheckGetNextReadBuffer();
	void FinalizeWrite();

	void TransferEnd(TransferEndReason reason);

	bool InitBackend();
	bool InitTls(const CTlsSocket* pPrimaryTlsSocket);

	void ResetSocket();

	void OnSocketEvent(CSocketEventSource* source, SocketEventType t, int error);
	void OnConnect();
	void OnAccept(int error);
	void OnReceive();
	void OnSend();
	void OnClose(int error);
	void OnTimer(fz::timer_id);

	// Create a socket server
	std::unique_ptr<CSocket> CreateSocketServer();
	std::unique_ptr<CSocket> CreateSocketServer(int port);

	void SetSocketBufferSizes(CSocket & socket);

	virtual void operator()(fz::event_base const& ev);
	void OnIOThreadEvent();

	std::unique_ptr<CSocket> socket_{};

	// Will be set only while creating active mode connections
	std::unique_ptr<CSocket> socketServer_;

	CFileZillaEnginePrivate & engine_;
	CFtpControlSocket & controlSocket_;

	bool m_bActive{};
	TransferEndReason m_transferEndReason{TransferEndReason::none};

	TransferMode const m_transferMode;

	char *m_pTransferBuffer{};
	int m_transferBufferLen{};

	// Set to true if OnClose got called
	// We now have to read all available data in the socket, ignoring any
	// speed limits
	bool m_onCloseCalled{};

	bool m_postponedReceive{};
	bool m_postponedSend{};
	void TriggerPostponedEvents();

	CBackend* m_pBackend{};

	CProxySocket* m_pProxyBackend{};

	CTlsSocket* m_pTlsSocket{};
	bool m_shutdown{};

	// Needed for the madeProgress field in CTransferStatus
	// Initially 0, 2 if made progress
	// On uploads, 1 after first WSAE_WOULDBLOCK
	int m_madeProgress{};

	CIOThread* ioThread_{};
};

#endif
