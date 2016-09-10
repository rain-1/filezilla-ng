#ifndef FILEZILLA_ENGINE_EXTERNALIPRESOLVER_HEADER
#define FILEZILLA_ENGINE_EXTERNALIPRESOLVER_HEADER

#include "socket.h"

struct external_ip_resolve_event_type;
typedef fz::simple_event<external_ip_resolve_event_type> CExternalIPResolveEvent;

class CExternalIPResolver final : public fz::event_handler
{
public:
	CExternalIPResolver(fz::thread_pool & pool, fz::event_handler & handler);
	virtual ~CExternalIPResolver();

	CExternalIPResolver(CExternalIPResolver const&) = delete;
	CExternalIPResolver& operator=(CExternalIPResolver const&) = delete;

	bool Done() const { return m_done; }
	bool Successful() const;
	std::string GetIP() const;

	void GetExternalIP(std::wstring const& resolver, CSocket::address_family protocol, bool force = false);

protected:

	void Close(bool successful);

	std::wstring m_address;
	CSocket::address_family m_protocol{};
	unsigned long m_port{80};
	fz::thread_pool & thread_pool_;
	fz::event_handler * m_handler{};

	bool m_done{};

	std::string m_data;

	CSocket *m_pSocket{};

	virtual void operator()(fz::event_base const& ev);
	void OnSocketEvent(CSocketEventSource* source, SocketEventType t, int error);

	void OnConnect(int error);
	void OnClose();
	void OnReceive();
	void OnHeader();
	void OnData(char* buffer, unsigned int len);
	void OnChunkedData();
	void OnSend();

	std::string m_sendBuffer;

	char* m_pRecvBuffer{};
	unsigned int m_recvBufferPos{};

	static const unsigned int m_recvBufferLen = 4096;

	// HTTP data
	void ResetHttpData(bool resetRedirectCount);
	bool m_gotHeader{};
	int m_responseCode{};
	std::string m_responseString;
	std::wstring m_location;
	int m_redirectCount{};

	enum transferEncodings {
		identity,
		chunked,
		unknown
	};

	transferEncodings m_transferEncoding;

	struct t_chunkData {
		bool getTrailer{};
		bool terminateChunk{};
		int64_t size{};
	} m_chunkData;

	bool m_finished{};
};

#endif
