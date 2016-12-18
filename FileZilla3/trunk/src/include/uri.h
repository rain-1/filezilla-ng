#ifndef FILEZILLA_ENGINE_URI_HEADER
#define FILEZILLA_ENGINE_URI_HEADER

#include <string>

namespace fz {

class uri
{
public:
	uri() = default;
	explicit uri(std::string const& in);

	void clear();

	// Splits uri into components.
	// Percent-decodes username, pass, host and path
	// Does not decode query and fragment.
	bool parse(std::string in);

	// Assembles components into string
	// Percent-encodes username, pass, host and path
	// Does not encode query and fragment.
	std::string to_string() const;

	// Returns path and query
	std::string get_request() const;

	std::string get_authority(bool with_userinfo) const;

	bool empty() const;

	std::string scheme_;
	std::string user_;
	std::string pass_;
	std::string host_;
	unsigned short port_{};
	std::string path_;
	std::string query_;
	std::string fragment_;

	bool is_absolute() const { return path_[0] == '/'; }

	// Does not remove dot segments in the path
	void resolve(uri const& base);
private:
	bool parse_authority(std::string && authority);
};

std::string percent_encode(std::string const& s, bool keep_slashes = false);
std::string percent_encode(std::wstring const& s, bool keep_slashes = false);
std::wstring percent_encode_w(std::wstring const& s, bool keep_slashes = false);

std::string percent_decode(std::string const& s);

}

#endif
