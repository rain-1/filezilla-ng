#ifndef FILEZILLA_LOCALE_INITIALIZER_HEADER
#define FILEZILLA_LOCALE_INITIALIZER_HEADER

#include <libfilezilla/libfilezilla.hpp>

wxString GetFallbackLocale(wxString const& locale);

#ifdef __WXGTK__

#include <string>

class CInitializer
{
public:
	static bool SetLocale(const std::string& arg);

	static std::string GetLocaleOption();

	static bool error;

protected:
	static std::string GetAdjustedSettingsDir(); // Returns settings from fzdefaults.xml if it exists, otherwise calls GetSettingsDir
	static std::string GetUnadjustedSettingsDir(); // Returns standard settings dir as if there were no fzdefaults.xml
	static std::string ReadSettingsFromDefaults(std::string file);
	static std::string GetSettingFromFile(std::string file, const std::string& name);
	static std::string GetDefaultsXmlFile();
	static std::string CheckPathForDefaults(std::string path, int strip, std::string suffix);

	static bool SetLocaleReal(const std::string& locale);
	static std::string LocaleAddEncoding(const std::string& locale, const std::string& encoding);
};

#endif //__WXGTK__

template<typename String>
String ExpandPath(String dir)
{
	if (dir.empty()) {
		return dir;
	}

	String result;
	while (!dir.empty()) {
		String token;
#ifdef FZ_WINDOWS
		size_t pos = dir.find_first_of(fzS(typename String::value_type, "\\/"));
#else
		size_t pos = dir.find('/');
#endif
		if (pos == String::npos) {
			token.swap(dir);
		}
		else {
			token = dir.substr(0, pos);
			dir = dir.substr(pos + 1);
		}

		if (token[0] == '$') {
			if (token[1] == '$') {
				result += token.substr(1);
			}
			else if (token.size() > 1) {
				char* env = getenv(fz::to_string(token.substr(1)).c_str());
				if (env) {
					result += fz::toString<String>(env);
				}
			}
		}
		else {
			result += token;
		}

#ifdef FZ_WINDOWS
		result += '\\';
#else
		result += '/';
#endif
	}

	return result;
}

#endif
