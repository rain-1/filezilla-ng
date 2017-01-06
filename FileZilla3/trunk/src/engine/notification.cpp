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

CHostKeyNotification::CHostKeyNotification(std::wstring const& host, int port, CSftpEncryptionDetails const& details, bool changed)
	: CSftpEncryptionDetails(details)
	, m_host(host)
	, m_port(port)
	, m_changed(changed)
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
		std::vector<uint8_t> const& rawData,
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
	, m_rawData(rawData)
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
}

CCertificate::CCertificate(
	std::vector<uint8_t> && rawData,
	fz::datetime const& activationTime, fz::datetime const& expirationTime,
	std::wstring const& serial,
	std::wstring const& pkalgoname, unsigned int bits,
	std::wstring const& signalgoname,
	std::wstring const& fingerprint_sha256,
	std::wstring const& fingerprint_sha1,
	std::wstring const& issuer,
	std::wstring const& subject,
	std::vector<std::wstring> && altSubjectNames)
	: m_activationTime(activationTime)
	, m_expirationTime(expirationTime)
	, m_rawData(rawData)
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
