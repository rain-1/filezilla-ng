#ifndef FILEZILLA_INTERFACE_LOGINMANAGER_HEADER
#define FILEZILLA_INTERFACE_LOGINMANAGER_HEADER

#include <vector>

// The purpose of this class is to manage some aspects of the login
// behaviour. These are:
// - Password dialog for servers with ASK or INTERACTIVE logontype
// - Storage of passwords for ASK servers for duration of current session

class CLoginManager
{
public:
	static CLoginManager& Get() { return m_theLoginManager; }

	bool GetPassword(ServerWithCredentials& server, bool silent, std::wstring const& name = std::wstring(), std::wstring const& challenge = std::wstring(), bool canRemember = true);

	void CachedPasswordFailed(CServer const& server, std::wstring const& challenge = std::wstring());

	void RememberPassword(ServerWithCredentials & server, std::wstring const& challenge = std::wstring());

protected:
	bool DisplayDialog(ServerWithCredentials& server, std::wstring const& name, std::wstring const& challenge, bool canRemember);

	static CLoginManager m_theLoginManager;

	// Session password cache for Ask-type servers
	struct t_passwordcache
	{
		std::wstring host;
		unsigned int port;
		std::wstring user;
		std::wstring password;
		std::wstring challenge;
	};

	std::list<t_passwordcache>::iterator FindItem(CServer const& server, std::wstring const& challenge);

	std::list<t_passwordcache> m_passwordCache;
};

class public_key
{
public:
	// Very simple structure to represent a X25519 public key with associated salt.

	enum {
		key_size = 32,
		salt_size = 32
	};

	explicit operator bool() const {
		return key_.size() == key_size && salt_.size() == salt_size;
	}

	std::vector<uint8_t> key_;
	std::vector<uint8_t> salt_;
};

class private_key
{
public:
	// Very simple structure to represent a X25519 private key with associated salt.
	// See RFC 7748 for the X22519 specs.
	enum {
		key_size = 32,
		salt_size = 32
	};

	private_key() = default;
	private_key(private_key const&) = default;

	// Generates a random private key
	static private_key generate();

	// Generates a private key using PKBDF2-SHA256 from the given password
	static private_key from_password(std::vector<uint8_t> const& password, std::vector<uint8_t> const& salt);
	static private_key from_password(std::string const& password, std::vector<uint8_t> const& salt)
	{
		return from_password(std::vector<uint8_t>(password.begin(), password.end()), salt);
	}

	explicit operator bool() const {
		return key_.size() == key_size && salt_.size() == salt_size;
	}

	std::vector<uint8_t> const& salt() const {
		return salt_;
	}

	// Gets the public key corresponding to the private key
	public_key pubkey() const;

	// Generates shared secret using Elliptic Curve Diffie-Hellman
	std::vector<uint8_t> shared_secret(public_key const& pub) const;

private:
	std::vector<uint8_t> key_;
	std::vector<uint8_t> salt_;
};

std::vector<uint8_t> get_random(size_t size);

// Encrypt the plaintext to the given public key.
std::vector<uint8_t> encrypt(std::vector<uint8_t> const& plain, public_key const& pub);
inline std::vector<uint8_t> encrypt(std::string const& plain, public_key const& pub)
{
	return encrypt(std::vector<uint8_t>(plain.begin(), plain.end()), pub);
}

// Decrypt the ciphertext. The private key matching the public key the data was encrypted to must be used.
std::vector<uint8_t> decrypt(std::vector<uint8_t> const& chiper, private_key const& priv);
inline std::vector<uint8_t> decrypt(std::string const& chiper, private_key const& priv)
{
	return decrypt(std::vector<uint8_t>(chiper.begin(), chiper.end()), priv);
}

#endif
