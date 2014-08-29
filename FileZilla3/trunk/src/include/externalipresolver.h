#ifndef __EXTERNALIPRESOLVER_H__
#define __EXTERNALIPRESOLVER_H__

#include "socket.h"

struct external_ip_resolve_event_type;
typedef CEvent<external_ip_resolve_event_type> CExternalIPResolveEvent;

class CExternalIPResolver final : public CSocketEventHandler
{
public:
	CExternalIPResolver(CSocketEventDispatcher& dispatcher, CEventHandler & handler);
	virtual ~CExternalIPResolver();

	bool Done() const { return m_done; }
	bool Successful() const { return !m_ip.empty(); }
	wxString GetIP() const { return m_ip; }

	void GetExternalIP(const wxString& address, CSocket::address_family protocol, bool force = false);

protected:

	void Close(bool successful);

	wxString m_address;
	CSocket::address_family m_protocol{};
	unsigned long m_port{80};
	CEventHandler * m_handler;
	int m_id;

	bool m_done{};

	static wxString m_ip;
	static bool m_checked;

	wxString m_data;

	CSocket *m_pSocket{};

	void OnSocketEvent(CSocketEvent& event);

	void OnConnect(int error);
	void OnClose();
	void OnReceive();
	void OnHeader();
	void OnData(char* buffer, unsigned int len);
	void OnChunkedData();
	void OnSend();

	char* m_pSendBuffer{};
	unsigned int m_sendBufferPos{};

	char* m_pRecvBuffer{};
	unsigned int m_recvBufferPos{};

	static const unsigned int m_recvBufferLen = 4096;

	// HTTP data
	void ResetHttpData(bool resetRedirectCount);
	bool m_gotHeader;
	int m_responseCode;
	wxString m_responseString;
	wxString m_location;
	int m_redirectCount;

	enum transferEncodings
	{
		identity,
		chunked,
		unknown
	};

	transferEncodings m_transferEncoding;

	struct t_chunkData
	{
		bool getTrailer;
		bool terminateChunk;
		wxLongLong size;
	} m_chunkData;

	bool m_finished;
};

#endif //__EXTERNALIPRESOLVER_H__
