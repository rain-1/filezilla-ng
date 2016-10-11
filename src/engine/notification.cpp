#include <filezilla.h>

CDirectoryListingNotification::CDirectoryListingNotification(const CServerPath& path, const bool modified /*=false*/, const bool failed /*=false*/)
	: m_modified(modified), m_failed(failed), m_path(path)
{
}

RequestId CFileExistsNotification::GetRequestID() const
{
	return reqId_fileexists;
}

CInteractiveLoginNotification::CInteractiveLoginNotification(type t, std::wstring const& challenge, bool repeated)
	: m_challenge(challenge)
	, m_type(t)
	, m_repeated(repeated)
{
}

RequestId CInteractiveLoginNotification::GetRequestID() const
{
	return reqId_interactiveLogin;
}

CActiveNotification::CActiveNotification(int direction)
	: m_direction(direction)
{
}

CTransferStatusNotification::CTransferStatusNotification(CTransferStatus const& status)
	: status_(status)
{
}

CTransferStatus const& CTransferStatusNotification::GetStatus() const
{
	return status_;
}

CHostKeyNotification::CHostKeyNotification(std::wstring const& host, int port, std::wstring const& fingerprint, bool changed)
	: m_host(host), m_port(port), m_fingerprint(fingerprint), m_changed(changed)
{
}

RequestId CHostKeyNotification::GetRequestID() const
{
	return m_changed ? reqId_hostkeyChanged : reqId_hostkey;
}

std::wstring CHostKeyNotification::GetHost() const
{
	return m_host;
}

int CHostKeyNotification::GetPort() const
{
	return m_port;
}

std::wstring CHostKeyNotification::GetFingerprint() const
{
	return m_fingerprint;
}

CDataNotification::CDataNotification(char* pData, int len)
	: m_pData(pData), m_len(len)
{
}

CDataNotification::~CDataNotification()
{
	delete [] m_pData;
}

char* CDataNotification::Detach(int& len)
{
	len = m_len;
	char* pData = m_pData;
	m_pData = 0;
	return pData;
}

CCertificate::CCertificate(
		unsigned char const* rawData, unsigned int len,
		fz::datetime const& activationTime, fz::datetime const& expirationTime,
		std::wstring const& serial,
		std::wstring const& pkalgoname, unsigned int bits,
		std::wstring const& signalgoname,
		std::wstring const& fingerprint_sha256,
		std::wstring const& fingerprint_sha1,
		std::wstring const& issuer,
		std::wstring const& subject,
		std::vector<std::wstring> const& altSubjectNames)
	: m_activationTime(activationTime)
	, m_expirationTime(expirationTime)
	, m_len(len)
	, m_serial(serial)
	, m_pkalgoname(pkalgoname)
	, m_pkalgobits(bits)
	, m_signalgoname(signalgoname)
	, m_fingerprint_sha256(fingerprint_sha256)
	, m_fingerprint_sha1(fingerprint_sha1)
	, m_issuer(issuer)
	, m_subject(subject)
	, m_altSubjectNames(altSubjectNames)
{
	wxASSERT(len);
	if (len) {
		m_rawData = new unsigned char[len];
		memcpy(m_rawData, rawData, len);
	}
}

CCertificate::CCertificate(const CCertificate &op)
{
	if (op.m_rawData) {
		wxASSERT(op.m_len);
		if (op.m_len) {
			m_rawData = new unsigned char[op.m_len];
			memcpy(m_rawData, op.m_rawData, op.m_len);
		}
		else {
			m_rawData = 0;
		}
	}
	else {
		m_rawData = 0;
	}
	m_len = op.m_len;

	m_activationTime = op.m_activationTime;
	m_expirationTime = op.m_expirationTime;

	m_serial = op.m_serial;
	m_pkalgoname = op.m_pkalgoname;
	m_pkalgobits = op.m_pkalgobits;

	m_signalgoname = op.m_signalgoname;

	m_fingerprint_sha256 = op.m_fingerprint_sha256;
	m_fingerprint_sha1 = op.m_fingerprint_sha1;

	m_issuer = op.m_issuer;
	m_subject = op.m_subject;
	m_altSubjectNames = op.m_altSubjectNames;
}

CCertificate::~CCertificate()
{
	delete [] m_rawData;
}

CCertificate& CCertificate::operator=(const CCertificate &op)
{
	if (&op == this)
		return *this;

	delete [] m_rawData;
	if (op.m_rawData) {
		wxASSERT(op.m_len);
		if (op.m_len) {
			m_rawData = new unsigned char[op.m_len];
			memcpy(m_rawData, op.m_rawData, op.m_len);
		}
		else
			m_rawData = 0;
	}
	else
		m_rawData = 0;
	m_len = op.m_len;

	m_activationTime = op.m_activationTime;
	m_expirationTime = op.m_expirationTime;

	m_serial = op.m_serial;
	m_pkalgoname = op.m_pkalgoname;
	m_pkalgobits = op.m_pkalgobits;

	m_signalgoname = op.m_signalgoname;

	m_fingerprint_sha256 = op.m_fingerprint_sha256;
	m_fingerprint_sha1 = op.m_fingerprint_sha1;

	m_issuer = op.m_issuer;
	m_subject = op.m_subject;
	m_altSubjectNames = op.m_altSubjectNames;

	return *this;
}

CCertificateNotification::CCertificateNotification(std::wstring const& host, unsigned int port,
		std::wstring const& protocol,
		std::wstring const& keyExchange,
		std::wstring const& sessionCipher,
		std::wstring const& sessionMac,
		int algorithmWarnings,
		std::vector<CCertificate> && certificates)
	: m_host(host)
	, m_port(port)
	, m_protocol(protocol)
	, m_keyExchange(keyExchange)
	, m_sessionCipher(sessionCipher)
	, m_sessionMac(sessionMac)
	, m_algorithmWarnings(algorithmWarnings)
	, m_certificates(certificates)
{
}
