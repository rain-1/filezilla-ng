#ifndef __TRANSFERSOCKET_H__
#define __TRANSFERSOCKET_H__

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
class CTransferSocket : public CEventHandler, public CSocketEventHandler
{
public:
	CTransferSocket(CFileZillaEnginePrivate *pEngine, CFtpControlSocket *pControlSocket, TransferMode transferMode);
	virtual ~CTransferSocket();

	wxString SetupActiveTransfer(const wxString& ip);
	bool SetupPassiveTransfer(wxString host, int port);

	void SetActive();

	CDirectoryListingParser *m_pDirectoryListingParser;

	bool m_binaryMode;

	TransferEndReason GetTransferEndreason() const { return m_transferEndReason; }

protected:
	bool CheckGetNextWriteBuffer();
	bool CheckGetNextReadBuffer();
	void FinalizeWrite();

	void TransferEnd(TransferEndReason reason);

	bool InitBackend();
	bool InitTls(const CTlsSocket* pPrimaryTlsSocket);

	void ResetSocket();

	virtual void OnSocketEvent(CSocketEvent &event);
	virtual void OnConnect();
	virtual void OnAccept(int error);
	virtual void OnReceive();
	virtual void OnSend();
	virtual void OnClose(int error);

	// Create a socket server
	CSocket* CreateSocketServer();
	CSocket* CreateSocketServer(int port);

	void SetSocketBufferSizes(CSocket* pSocket);

	virtual void operator()(CEventBase const& ev);
	void OnIOThreadEvent();

	CSocket *m_pSocket;

	// Will be set only while creating active mode connections
	CSocket* m_pSocketServer;

	CFileZillaEnginePrivate *m_pEngine;
	CFtpControlSocket *m_pControlSocket;

	bool m_bActive;
	TransferEndReason m_transferEndReason;

	TransferMode m_transferMode;

	char *m_pTransferBuffer;
	int m_transferBufferLen;

	// Set to true if OnClose got called
	// We now have to read all available data in the socket, ignoring any
	// speed limits
	bool m_onCloseCalled;

	bool m_postponedReceive;
	bool m_postponedSend;
	void TriggerPostponedEvents();

	CBackend* m_pBackend;

	CProxySocket* m_pProxyBackend;

	CTlsSocket* m_pTlsSocket;
	bool m_shutdown;

	// Needed for the madeProgress field in CTransferStatus
	// Initially 0, 2 if made progress
	// On uploads, 1 after first WSAE_WOULDBLOCK
	int m_madeProgress;
};

#endif
