#include <filezilla.h>
#include "loginmanager.h"

#include "dialogex.h"
#include "filezillaapp.h"

#include <algorithm>
#include <random>

#include <nettle/aes.h>
#include <nettle/ctr.h>
#include <nettle/curve25519.h>
#include <nettle/pbkdf2.h>
#include <nettle/sha2.h>

CLoginManager CLoginManager::m_theLoginManager;

std::list<CLoginManager::t_passwordcache>::iterator CLoginManager::FindItem(CServer const& server, std::wstring const& challenge)
{
	return std::find_if(m_passwordCache.begin(), m_passwordCache.end(), [&](t_passwordcache const& item)
		{
			return item.host == server.GetHost() && item.port == server.GetPort() && item.user == server.GetUser() && item.challenge == challenge;
		}
	);
}

bool CLoginManager::GetPassword(ServerWithCredentials &server, bool silent, std::wstring const& name, std::wstring const& challenge, bool canRemember)
{
	wxASSERT(server.credentials.logonType_ != LogonType::anonymous);

	if (canRemember) {
		auto it = FindItem(server.server, challenge);
		if (it != m_passwordCache.end()) {
			server.credentials.SetPass(it->password);
			return true;
		}
	}
	if (silent) {
		return false;
	}

	return DisplayDialog(server, name, challenge, canRemember);
}

bool CLoginManager::DisplayDialog(ServerWithCredentials &server, std::wstring const& name, std::wstring const& challenge, bool canRemember)
{
	wxDialogEx pwdDlg;
	if (!pwdDlg.Load(wxGetApp().GetTopWindow(), _T("ID_ENTERPASSWORD"))) {
		return false;
	}

	if (name.empty()) {
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_NAMELABEL", wxStaticText), false, true);
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_NAME", wxStaticText), false, true);
	}
	else {
		XRCCTRL(pwdDlg, "ID_NAME", wxStaticText)->SetLabel(name);
	}
	if (challenge.empty()) {
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_CHALLENGELABEL", wxStaticText), false, true);
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_CHALLENGE", wxTextCtrl), false, true);
	}
	else {
		wxString displayChallenge = challenge;
		displayChallenge.Trim(true);
		displayChallenge.Trim(false);
#ifdef FZ_WINDOWS
		displayChallenge.Replace(_T("\n"), _T("\r\n"));
#endif
		XRCCTRL(pwdDlg, "ID_CHALLENGE", wxTextCtrl)->ChangeValue(displayChallenge);
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_REMEMBER", wxCheckBox), canRemember, true);
		XRCCTRL(pwdDlg, "ID_CHALLENGE", wxTextCtrl)->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
	}
	XRCCTRL(pwdDlg, "ID_HOST", wxStaticText)->SetLabel(server.Format(ServerFormat::with_optional_port));

	if (server.server.GetUser().empty()) {
		XRCCTRL(pwdDlg, "ID_OLD_USER_LABEL", wxStaticText)->Hide();
		XRCCTRL(pwdDlg, "ID_OLD_USER", wxStaticText)->Hide();

		XRCCTRL(pwdDlg, "ID_HEADER_PASS", wxStaticText)->Hide();
		if (server.credentials .logonType_ == LogonType::interactive) {
			pwdDlg.SetTitle(_("Enter username"));
			XRCCTRL(pwdDlg, "ID_PASSWORD_LABEL", wxStaticText)->Hide();
			XRCCTRL(pwdDlg, "ID_PASSWORD", wxTextCtrl)->Hide();
			XRCCTRL(pwdDlg, "ID_REMEMBER", wxCheckBox)->Hide();
			XRCCTRL(pwdDlg, "ID_HEADER_BOTH", wxStaticText)->Hide();
		}
		else {
			pwdDlg.SetTitle(_("Enter username and password"));
			XRCCTRL(pwdDlg, "ID_HEADER_USER", wxStaticText)->Hide();
		}

		XRCCTRL(pwdDlg, "ID_NEW_USER", wxTextCtrl)->SetFocus();
	}
	else {
		XRCCTRL(pwdDlg, "ID_OLD_USER", wxStaticText)->SetLabel(server.server.GetUser());
		XRCCTRL(pwdDlg, "ID_NEW_USER_LABEL", wxStaticText)->Hide();
		XRCCTRL(pwdDlg, "ID_NEW_USER", wxTextCtrl)->Hide();
		XRCCTRL(pwdDlg, "ID_HEADER_USER", wxStaticText)->Hide();
		XRCCTRL(pwdDlg, "ID_HEADER_BOTH", wxStaticText)->Hide();
	}
	XRCCTRL(pwdDlg, "wxID_OK", wxButton)->SetId(wxID_OK);
	XRCCTRL(pwdDlg, "wxID_CANCEL", wxButton)->SetId(wxID_CANCEL);
	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	std::wstring user;
	while (user.empty()) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return false;
		}

		if (server.server.GetUser().empty()) {
			user = XRCCTRL(pwdDlg, "ID_NEW_USER", wxTextCtrl)->GetValue().ToStdWstring();
			if (user.empty()) {
				wxMessageBoxEx(_("No username given."), _("Invalid input"), wxICON_EXCLAMATION);
				continue;
			}
		}
		else {
			user = server.server.GetUser();
		}
	}
	
	server.SetUser(user);
	server.credentials.SetPass(XRCCTRL(pwdDlg, "ID_PASSWORD", wxTextCtrl)->GetValue().ToStdWstring());

	if (canRemember) {
		RememberPassword(server, challenge);
	}

	return true;
}

void CLoginManager::CachedPasswordFailed(CServer const& server, std::wstring const& challenge)
{
	auto it = FindItem(server, challenge);
	if (it != m_passwordCache.end()) {
		m_passwordCache.erase(it);
	}
}

void CLoginManager::RememberPassword(ServerWithCredentials & server, std::wstring const& challenge)
{
	if (server.credentials.logonType_ == LogonType::anonymous) {
		return;
	}

	auto it = FindItem(server.server, challenge);
	if (it != m_passwordCache.end()) {
		it->password = server.credentials.GetPass();
	}
	else {
		t_passwordcache entry;
		entry.host = server.server.GetHost();
		entry.port = server.server.GetPort();
		entry.user = server.server.GetUser();
		entry.password = server.credentials.GetPass();
		entry.challenge = challenge;
		m_passwordCache.push_back(entry);
	}
}

std::vector<uint8_t> get_random(size_t size)
{
	std::vector<uint8_t> ret;
	ret.resize(size);

	std::random_device rd;

	ret.resize(size);
	size_t i;
	for (i = 0; i + sizeof(std::random_device::result_type) <= ret.size(); i += sizeof(std::random_device::result_type)) {
		*reinterpret_cast<std::random_device::result_type*>(&ret[i]) = rd();
	}

	if (i < size) {
		auto v = rd();
		memcpy(&ret[i], &v, size - i);
	}

	return ret;
}


private_key private_key::generate()
{
	private_key ret;

	ret.key_ = get_random(key_size);
	ret.key_[0] &= 248;
	ret.key_[31] &= 127;
	ret.key_[31] |= 64;

	ret.salt_ = get_random(salt_size);

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

	if (cipher.size() > public_key::key_size + public_key::salt_size) {
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
