#ifndef FILEZILLA_ENGINE_TLSSOCKET_IMPL_HEADER
#define FILEZILLA_ENGINE_TLSSOCKET_IMPL_HEADER

#if defined(_MSC_VER)
typedef std::make_signed_t<size_t> ssize_t;
#endif

#include <gnutls/gnutls.h>
#include "backend.h"
#include "socket.h"

class CControlSocket;
class CTlsSocket;
class CTlsSocketImpl final
{
public:
	CTlsSocketImpl(CTlsSocket& tlsSocket, CSocket& pSocket, CControlSocket* pOwner);
	~CTlsSocketImpl();

	bool Init();
	void Uninit();

	int Handshake(const CTlsSocketImpl* pPrimarySocket = 0, bool try_resume = 0);

	int Read(void *buffer, unsigned int size, int& error);
	int Peek(void *buffer, unsigned int size, int& error);
	int Write(const void *buffer, unsigned int size, int& error);

	int Shutdown();

	void TrustCurrentCert(bool trusted);

	CTlsSocket::TlsState GetState() const { return m_tlsState; }

	std::wstring GetProtocolName();
	std::wstring GetKeyExchange();
	std::wstring GetCipherName();
	std::wstring GetMacName();
	int GetAlgorithmWarnings();

	bool ResumedSession() const;

	static std::string ListTlsCiphers(std::string priority);

	bool SetClientCertificate(fz::native_string const& keyfile, fz::native_string const& certs, fz::native_string const& password);

	static std::wstring GetGnutlsVersion();

protected:

	bool InitSession();
	void UninitSession();
	bool CopySessionData(CTlsSocketImpl const* pPrimarySocket);

	void OnRateAvailable(CRateLimiter::rate_direction direction);

	int ContinueHandshake();
	void ContinueShutdown();

	int VerifyCertificate();
	bool CertificateIsBlacklisted(std::vector<CCertificate> const& certificates);

	void LogError(int code, std::wstring const& function, MessageType logLegel = MessageType::Error);
	void PrintAlert(MessageType logLevel);

	// Failure logs the error, uninits the session and sends a close event
	void Failure(int code, bool send_close, std::wstring const& function = std::wstring());

	static ssize_t PushFunction(gnutls_transport_ptr_t ptr, const void* data, size_t len);
	static ssize_t PullFunction(gnutls_transport_ptr_t ptr, void* data, size_t len);
	ssize_t PushFunction(const void* data, size_t len);
	ssize_t PullFunction(void* data, size_t len);

	int DoCallGnutlsRecordRecv(void* data, size_t len);

	void TriggerEvents();

	void operator()(fz::event_base const& ev);
	void OnSocketEvent(CSocketEventSource* source, SocketEventType t, int error);

	void OnRead();
	void OnSend();

	bool GetSortedPeerCertificates(gnutls_x509_crt_t *& certs, unsigned int & certs_size);

	bool ExtractCert(gnutls_x509_crt_t const& cert, CCertificate& out);
	std::vector<std::wstring> GetCertSubjectAltNames(gnutls_x509_crt_t cert);

	CTlsSocket& tlsSocket_;

	CTlsSocket::TlsState m_tlsState{ CTlsSocket::TlsState::noconn };

	CControlSocket* m_pOwner{};

	bool m_initialized{};
	gnutls_session_t m_session{};

	gnutls_certificate_credentials_t m_certCredentials{};

	bool m_canReadFromSocket{true};
	bool m_canWriteToSocket{true};
	bool m_canCheckCloseSocket{false};

	bool m_canTriggerRead{false};
	bool m_canTriggerWrite{true};

	bool m_socketClosed{};

	CSocket& m_socket;
	std::unique_ptr<CSocketBackend> socketBackend_;

	bool m_shutdown_requested{};

	// Due to the strange gnutls_record_send semantics, call it again
	// with 0 data and 0 length after GNUTLS_E_AGAIN and store the number
	// of bytes written. These bytes get skipped on next write from the
	// application.
	// This avoids the rule to call it again with the -same- data after
	// GNUTLS_E_AGAIN.
	void CheckResumeFailedReadWrite();
	bool m_lastReadFailed{true};
	bool m_lastWriteFailed{false};
	unsigned int m_writeSkip{};

	// Peek data
	char* m_peekData{};
	unsigned int m_peekDataLen{};

	gnutls_datum_t m_implicitTrustedCert;

	bool m_socket_eof{};
	int m_socket_error{ECONNABORTED}; // Set in the push and pull functions if reading/writing fails fatally

	friend class CTlsSocket;
	friend class CTlsSocketCallbacks;
};

#endif
