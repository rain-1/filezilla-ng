#ifndef FILEZILLA_INSTALLER_PASSWORD_CRYPTO_HEADER
#define FILEZILLA_INSTALLER_PASSWORD_CRYPTO_HEADER

#include <vector>
#include <string>

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

	bool operator==(public_key const& rhs) const {
		return key_ == rhs.key_ && salt_ == rhs.salt_;
	}

	bool operator!=(public_key const& rhs) const {
		return !(*this == rhs);
	}

	bool operator<(public_key const& rhs) const {
		return key_ < rhs.key_ || (key_ == rhs.key_ && salt_ < rhs.salt_);
	}

	std::string to_base64() const;
	static public_key from_base64(std::string const& base64);

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
