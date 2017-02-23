#ifndef FILEZILLA_ENGINE_TLSSOCKET_HEADER
#define FILEZILLA_ENGINE_TLSSOCKET_HEADER

#include "backend.h"

class CControlSocket;
class CTlsSocketImpl;
class CTlsSocket final : protected fz::event_handler, public CBackend
{
public:
	enum class TlsState
	{
		noconn,
		handshake,
		verifycert,
		conn,
		closing,
		closed
	};

	CTlsSocket(fz::event_handler* pEvtHandler, CSocket& pSocket, CControlSocket* pOwner);
	virtual ~CTlsSocket();

	bool Init();
	void Uninit();

	int Handshake(const CTlsSocket* pPrimarySocket = 0, bool try_resume = 0);

	virtual int Read(void *buffer, unsigned int size, int& error) override;
	virtual int Peek(void *buffer, unsigned int size, int& error) override;
	virtual int Write(const void *buffer, unsigned int size, int& error) override;

	int Shutdown();

	void TrustCurrentCert(bool trusted);

	TlsState GetState() const;

	std::wstring GetProtocolName();
	std::wstring GetKeyExchange();
	std::wstring GetCipherName();
	std::wstring GetMacName();
	int GetAlgorithmWarnings();

	bool ResumedSession() const;

	static std::string ListTlsCiphers(std::string const& priority);

	bool SetClientCertificate(fz::native_string const& keyfile, fz::native_string const& certs, fz::native_string const& password);

	static std::wstring GetGnutlsVersion();
private:
	virtual void operator()(fz::event_base const& ev) override;
	virtual void OnRateAvailable(CRateLimiter::rate_direction direction) override;

	friend class CTlsSocketImpl;
	std::unique_ptr<CTlsSocketImpl> impl_;
};

#endif
