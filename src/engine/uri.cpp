#include <filezilla.h>

#include "uri.h"

#include <libfilezilla/iputils.hpp>

namespace fz {

namespace uri_chars {
std::string const alpha{ "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" };
std::string const digit{ "01234567890" };
std::string const hexdigit{ digit + "abcdefABCDEF" };
std::string const scheme{ alpha + digit + "+-." };
}

uri::uri(std::string const& in)
{
	if (!parse(in)) {
		clear();
	}
}

void uri::clear()
{
	*this = uri();
}

bool uri::parse(std::string in)
{
	// Look for fragment
	size_t pos = in.find('#');
	if (pos != std::string::npos) {
		fragment_ = in.substr(pos + 1);
		in = in.substr(0, pos);
	}

	// Look for query
	pos = in.find('?');
	if (pos != std::string::npos) {
		query_ = in.substr(pos + 1);
		in = in.substr(0, pos);
	}

	// Do we have a scheme?
	if (uri_chars::alpha.find(in[0]) != std::string::npos) {
		size_t scheme_delim = in.find_first_not_of(uri_chars::scheme, 1);
		if (scheme_delim != std::string::npos && in[scheme_delim] == ':') {
			scheme_ = in.substr(0, scheme_delim);
			in = in.substr(scheme_delim + 1);
		}
	}

	// Do we have authority?
	if (in[0] == '/' && in[1] == '/') {
		size_t auth_delim = in.find('/', 2);
		std::string authority;
		if (auth_delim != std::string::npos) {
			authority = in.substr(2, auth_delim - 2);
			in = in.substr(auth_delim);
		}
		else {
			authority = in;
			in.clear();
		}
		if (!parse_authority(std::move(authority))) {
			return false;
		}
	}

	if (!in.empty()) {
		path_ = percent_decode(in);
		if (path_.empty()) {
			return false;
		}
	}

	return true;
}

bool uri::parse_authority(std::string && authority)
{
	// Do we have userinfo?
	size_t pos = authority.find('@');
	if (pos != std::string::npos) {
		std::string userinfo = authority.substr(0, pos);
		authority = authority.substr(pos + 1);
		pos = userinfo.find(':');
		if (pos != std::string::npos) { // Slight inaccuracy: Empty password isn't handled well
			user_ = percent_decode(userinfo.substr(0, pos));
			if (user_.empty() && pos != 0) {
				return false;
			}
			pass_ = percent_decode(userinfo.substr(pos + 1));
			if (pass_.empty() && pos + 1 != userinfo.size()) {
				return false;
			}
		}
		else {
			user_ = userinfo;
		}
	}

	// Do we have port?
	pos = authority.rfind(':');
	if (pos != std::string::npos) {
		if (authority.find_first_not_of(uri_chars::digit, pos + 1) == std::string::npos) {
			port_ = to_integral<unsigned short>(authority.substr(pos + 1));
			authority = authority.substr(0, pos);
		}
	}

	if (authority[0] == '[') {
		if (authority.back() != ']') {
			return false;
		}
		if (get_address_type(authority) != address_type::ipv6) {
			return false;
		}
	}
	host_ = percent_decode(authority);
	if (host_.empty() && !authority.empty()) {
		return false;
	}
	return true;
}

std::string uri::to_string() const
{
	std::string ret;
	if (!scheme_.empty()) {
		ret += scheme_ + ":";
	}
	if (!host_.empty()) {
		ret += "//";
		ret += get_authority(true);
	}
	ret += percent_encode(path_, true);

	if (!query_.empty()) {
		ret += "?" + query_;
	}
	if (!fragment_.empty()) {
		ret += "#" + fragment_;
	}

	return ret;
}

std::string uri::get_request() const
{
	std::string ret = percent_encode(path_, true);
	if (!ret.empty() && !query_.empty()) {
		ret += "?";
		ret += query_;
	}

	return ret;
}

std::string uri::get_authority(bool with_userinfo) const
{
	std::string ret;
	if (!host_.empty()) {
		if (with_userinfo) {
			ret += percent_encode(user_);
			if (!pass_.empty()) {
				ret += ":";
				ret += percent_encode(pass_);
			}
			if (!user_.empty() || !pass_.empty()) {
				ret += "@";
			}
		}
		ret += percent_encode(host_);
		if (port_ != 0) {
			ret += ":";
			ret += fz::to_string(port_);
		}
	}

	return ret;
}

void uri::resolve(uri const& base)
{
	if (!scheme_.empty() && scheme_ != base.scheme_) {
		return;
	}
	scheme_ = base.scheme_;

	if (!host_.empty()) {
		return;
	}

	host_ = base.host_;
	port_ = base.port_;
	user_ = base.user_;
	pass_ = base.pass_;

	if (path_.empty()) {
		path_ = base.path_;
		if (query_.empty()) {
			query_ = base.query_;
		}
	}
	else {
		if (path_[0] != '/') {
			if (base.path_.empty() && !base.host_.empty()) {
				path_ = "/" + path_;
			}
			else {
				size_t pos = base.path_.rfind('/');
				if (pos != std::string::npos) {
					path_ = base.path_.substr(0, pos) + path_;
				}
			}
		}
	}
}

bool uri::empty() const
{
	return host_.empty() && path_.empty();
}

std::string percent_encode(std::string const& s, bool keep_slashes)
{
	std::string ret;
	ret.reserve(s.size());

	for (auto const& c : s) {
		if (!c) {
			break;
		}
		else if ((c >= '0' && c <= '9') ||
			(c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			c == '-' || c == '.' || c == '_' || c == '~')
		{
			ret += c;
		}
		else if (c == '/' && keep_slashes) {
			ret += c;
		}
		else {
			ret += '%';
			ret += int_to_hex_char<char, false>(static_cast<unsigned char>(c) >> 4);
			ret += int_to_hex_char<char, false>(c & 0xf);
		}
	}

	return ret;
}

std::string percent_encode(std::wstring const& s, bool keep_slashes)
{
	return percent_encode(to_utf8(s), keep_slashes);
}

std::wstring percent_encode_w(std::wstring const& s, bool keep_slashes)
{
	return to_wstring(percent_encode(s, keep_slashes));
}

std::string percent_decode(std::string const& s)
{
	std::string ret;
	ret.reserve(s.size());

	char const* c = s.c_str();
	while (*c) {
		if (*c == '%') {
			int high = hex_char_to_int(*(++c));
			if (high == -1) {
				return std::string();
			}
			int low = hex_char_to_int(*(++c));
			if (low == -1) {
				return std::string();
			}

			// Special feature: Disallow
			if (!high && !low) {
				return std::string();
			}
			ret.push_back(static_cast<char>(static_cast<uint8_t>((high << 4) + low)));
		}
		else {
			ret.push_back(*c);
		}
		++c;
	}

	return ret;
}

}
