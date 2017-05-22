#include <filezilla.h>

#include "password_crypto.h"

#include <libfilezilla/util.hpp>

#include <random>

#include <nettle/aes.h>
#include <nettle/ctr.h>
#include <nettle/curve25519.h>
#include <nettle/pbkdf2.h>
#include <nettle/sha2.h>

std::string public_key::to_base64() const
{
	auto raw = std::string(key_.cbegin(), key_.cend());
	raw += std::string(salt_.cbegin(), salt_.cend());
	return fz::base64_encode(raw);
}

public_key public_key::from_base64(std::string const& base64)
{
	public_key ret;
	
	auto raw = fz::base64_decode(base64);
	if (raw.size() == key_size + salt_size) {
		auto p = reinterpret_cast<uint8_t const*>(&raw[0]);
		ret.key_.assign(p, p + key_size);
		ret.salt_.assign(p + key_size, p + key_size + salt_size);
	}

	return ret;
}

private_key private_key::generate()
{
	private_key ret;

	ret.key_ = fz::random_bytes(key_size);
	ret.key_[0] &= 248;
	ret.key_[31] &= 127;
	ret.key_[31] |= 64;

	ret.salt_ = fz::random_bytes(salt_size);

	return ret;
}

public_key private_key::pubkey() const
{
	public_key ret;

	if (*this) {
		static const uint8_t nine[32]{
			9, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0 };

		ret.key_.resize(32);
		nettle_curve25519_mul(&ret.key_[0], &key_[0], nine);

		ret.salt_ = salt_;
	}

	return ret;
}

private_key private_key::from_password(std::vector<uint8_t> const& password, std::vector<uint8_t> const& salt)
{
	private_key ret;

	if (!password.empty() && salt.size() == salt_size) {

		std::vector<uint8_t> key;
		key.resize(key_size);
		nettle_pbkdf2_hmac_sha256(password.size(), &password[0], 100000, salt_size, &salt[0], 32, &key[0]);
		key[0] &= 248;
		key[31] &= 127;
		key[31] |= 64;

		ret.key_ = std::move(key);
		ret.salt_ = salt;
	}

	return ret;
}

std::vector<uint8_t> private_key::shared_secret(public_key const& pub) const
{
	std::vector<uint8_t> ret;

	if (*this && pub) {
		ret.resize(32);

		nettle_curve25519_mul(&ret[0], &key_[0], &pub.key_[0]);
	}

	return ret;
}

namespace {
class sha256 final
{
public:
	sha256()
	{
		nettle_sha256_init(&ctx_);
	}

	sha256(sha256 const&) = delete;
	sha256& operator=(sha256 const&) = delete;

	operator std::vector<uint8_t>()
	{
		std::vector<uint8_t> ret;
		ret.resize(32);

		nettle_sha256_digest(&ctx_, 32, &ret[0]);

		return ret;
	}

	sha256& operator<<(uint8_t in)
	{
		nettle_sha256_update(&ctx_, 1, &in);
		return *this;
	}

	sha256& operator<<(std::vector<uint8_t> in)
	{
		if (!in.empty()) {
			nettle_sha256_update(&ctx_, in.size(), &in[0]);
		}
		return *this;
	}

private:
	sha256_ctx ctx_;
};

}

std::vector<uint8_t> encrypt(std::vector<uint8_t> const& plain, public_key const& pub)
{
	std::vector<uint8_t> ret;

	private_key ephemeral = private_key::generate();
	public_key ephemeral_pub = ephemeral.pubkey();

	if (pub && ephemeral && ephemeral_pub) {
		// Generate shared secret from pub and ephemeral
		std::vector<uint8_t> secret = ephemeral.shared_secret(pub);

		// Derive AES2556 key and CTR nonce from shared secret
		std::vector<uint8_t> aes_key = sha256() << ephemeral_pub.salt_ << 0 << secret << ephemeral_pub.key_ << pub.key_ << pub.salt_;
		std::vector<uint8_t> ctr = sha256() << ephemeral_pub.salt_ << 1 << secret << ephemeral_pub.key_ << pub.key_ << pub.salt_;

		aes256_ctx ctx;
		aes256_set_encrypt_key(&ctx, &aes_key[0]);

		// Encrypt plaintext with AES256-CTR
		ret.resize(public_key::key_size + public_key::salt_size + plain.size());
		nettle_ctr_crypt(&ctx, reinterpret_cast<nettle_cipher_func*>(nettle_aes256_encrypt), 16, &ctr[0], plain.size(), &ret[public_key::key_size + public_key::salt_size], &plain[0]);

		// Return ephemeral_pub.key_||ephemeral_pub.salt_||ciphertext
		memcpy(&ret[0], &ephemeral_pub.key_[0], public_key::key_size);
		memcpy(&ret[public_key::key_size], &ephemeral_pub.salt_[0], public_key::salt_size);
	}

	return ret;
}

std::vector<uint8_t> decrypt(std::vector<uint8_t> const& cipher, private_key const& priv)
{
	std::vector<uint8_t> ret;

	if (priv && cipher.size() > public_key::key_size + public_key::salt_size) {
		// Extract ephemeral_pub from cipher
		public_key ephemeral_pub;
		ephemeral_pub.key_.resize(public_key::key_size);
		ephemeral_pub.salt_.resize(public_key::salt_size);
		memcpy(&ephemeral_pub.key_[0], &cipher[0], public_key::key_size);
		memcpy(&ephemeral_pub.salt_[0], &cipher[public_key::key_size], public_key::salt_size);

		// Generate shared secret from ephemeral_pub and priv
		std::vector<uint8_t> secret = priv.shared_secret(ephemeral_pub);

		// Derive AES2556 key and CTR nonce from shared secret
		public_key pub = priv.pubkey();
		std::vector<uint8_t> aes_key = sha256() << ephemeral_pub.salt_ << 0 << secret << ephemeral_pub.key_ << pub.key_ << pub.salt_;
		std::vector<uint8_t> ctr = sha256() << ephemeral_pub.salt_ << 1 << secret << ephemeral_pub.key_ << pub.key_ << pub.salt_;

		aes256_ctx ctx;
		aes256_set_encrypt_key(&ctx, &aes_key[0]);

		// Decrypt ciphertext with AES256-CTR
		ret.resize(cipher.size() - (public_key::key_size + public_key::salt_size));
		nettle_ctr_crypt(&ctx, reinterpret_cast<nettle_cipher_func*>(nettle_aes256_encrypt), 16, &ctr[0], ret.size(), &ret[0], &cipher[public_key::key_size + public_key::salt_size]);

		// Return the plaintext
	}

	return ret;
}
