#include <filezilla.h>
#include "locale_initializer.h"
#ifdef HAVE_LIBPUGIXML
#include <pugixml.hpp>
#else
#include "../pugixml/pugixml.hpp"
#endif
#include <string>
#include <locale.h>
#include <sys/stat.h>

#ifdef ENABLE_BINRELOC
	#define BR_PTHREADS 0
	#include "prefix.h"
#endif

struct t_fallbacks
{
	const char* locale;
	const char* fallback;
};

struct t_fallbacks fallbacks[] = {

	// The following entries are needed due to missing language codes wxWidgets
	{ "ka", "ka_GE" },
	{ "ku", "ku_TR" },
	{ "ne", "ne_NP" },

	// Fallback chain for English
	{ "en", "en_US" },
	{ "en_US", "en_GB" },
	{ "en_GB", "C" },

	// Base names
	{ "ar", "ar_EG" },
	{ "ca", "ca_ES" },
	{ "cy", "cy_GB" },
	{ "de", "de_DE" },
	{ "el", "el_GR" },
	{ "es", "es_ES" },
	{ "et", "et_EE" },
	{ "fr", "fr_FR" },
	{ "it", "it_IT" },
	{ "nl", "nl_NL" },
	{ "ru", "ru_RU" },
	{ "sv", "sv_SE" },
	{ "tr", "tr_TR" },

	{ 0, 0 }
};

wxString GetFallbackLocale( wxString const& locale )
{
	int i = 0;
	while (fallbacks[i].locale) {
		if (fallbacks[i].locale == locale)
			return fallbacks[i].fallback;
		i++;
	}

	return wxString();
}

// Custom main method to initialize proper locale
#ifdef __WXGTK__

bool CInitializer::error = false;

static std::string mkstr(const char* str)
{
	if (!str)
		return "";
	else
		return str;
}

int main(int argc, char** argv)
{
	std::string locale = CInitializer::GetLocaleOption();
	if (locale != "")
	{
		if (!CInitializer::SetLocale(locale))
		{
#ifdef __WXDEBUG__
			printf("failed to set locale\n");
#endif
			CInitializer::error = true;
		}
		else
		{
#ifdef __WXDEBUG__
			printf("locale set to %s\n", setlocale(LC_ALL, 0));
#endif
		}
	}

	return wxEntry(argc, argv);
}

bool CInitializer::SetLocaleReal(const std::string& locale)
{
	if (!setlocale(LC_ALL, locale.c_str()))
		return false;

#ifdef __WXDEBUG__
	printf("setlocale %s successful\n", locale.c_str());
#endif
#ifdef HAVE_SETENV
	setenv("LC_ALL", locale.c_str(), 1);
#else
	std::string str("LC_ALL=");
	str += locale;
	putenv(str.c_str());
#endif
	return true;
}

bool CInitializer::SetLocale(const std::string& arg)
{
	const char *encodings[] = {
		"UTF-8",
		"UTF8",
		"utf-8",
		"utf8",
		0
	};

	for (int i = 0; encodings[i]; i++)
	{
		std::string locale = CInitializer::LocaleAddEncoding(arg, encodings[i]);
		if (SetLocaleReal(locale))
			return true;
	}

	if (CInitializer::SetLocaleReal(arg))
		return true;

	int i = 0;
	while (fallbacks[i].locale)
	{
		if (fallbacks[i].locale == arg)
			return SetLocale(fallbacks[i].fallback);
		i++;
	}

	return false;
}

std::string CInitializer::CheckPathForDefaults(std::string path, int strip, std::string suffix)
{
	if (path.empty())
		return "";

	if (path[path.size() - 1] == '/')
		path = path.substr(0, path.size() - 1);
	while (strip--)
	{
		int p = path.rfind('/');
		if (p == -1)
			return "";
		path = path.substr(0, p);
	}

	path += '/' + suffix;
	struct stat buf;
	if (!stat(path.c_str(), &buf))
		return path;

	return "";
}

std::string CInitializer::GetDefaultsXmlFile()
{
	std::string fzdatadir = mkstr(getenv("FZ_DATADIR"));
	std::string file = CheckPathForDefaults(fzdatadir, 0, "fzdefaults.xml");
	if (!file.empty())
		return file;
	file = CheckPathForDefaults(fzdatadir, 1, "fzdefaults.xml");
	if (!file.empty())
		return file;

	file = GetUnadjustedSettingsDir() + "fzdefaults.xml";

	{
		struct stat buf{};
		if (!stat(file.c_str(), &buf)) {
			return file;
		}
	}

	file = "/etc/filezilla/fzdefaults.xml";

	{
		struct stat buf{};
		if (!stat(file.c_str(), &buf))
			return file;
	}


	file = CheckPathForDefaults(mkstr(SELFPATH), 2, "share/filezilla/fzdefaults.xml");
	if (!file.empty())
		return file;
	file = CheckPathForDefaults(mkstr(DATADIR), 0, "filezilla/fzdefaults.xml");
	if (!file.empty())
		return file;

	std::string path = mkstr(getenv("PATH"));
	while (!path.empty())
	{
		std::string segment;
		int pos = path.find(':');
		if (pos == -1)
			segment.swap(path);
		else
		{
			segment = path.substr(0, pos);
			path = path.substr(pos + 1);
		}

		file = CheckPathForDefaults(segment, 1, "share/filezilla/fzdefaults.xml");
		if (!file.empty())
			return file;
	}

	return "";
}

std::string CInitializer::ReadSettingsFromDefaults(std::string file)
{
	std::string dir = CInitializer::GetSettingFromFile(file, "Config Location");
	auto result = ExpandPath(dir);

	struct stat buf;
	if (stat(result.c_str(), &buf)) {
		return "";
	}

	if (result[result.size() - 1] != '/') {
		result += '/';
	}

	return result;
}

std::string CInitializer::GetSettingFromFile(std::string file, const std::string& name)
{
	pugi::xml_document xmldoc;
	if (!xmldoc.load_file(file.c_str()))
		return "";

	auto main = xmldoc.child("FileZilla3");
	if (!main)
		return "";

	auto settings = main.child("Settings");
	if (!settings)
		return "";

	for (auto setting = settings.child("Setting"); setting; setting = setting.next_sibling("Setting")) {
		const char* nodeVal = setting.attribute("name").value();
		if (!nodeVal || strcmp(nodeVal, name.c_str()))
			continue;

		const char* text = setting.child_value();
		return mkstr(text);
	}

	return "";
}

namespace {
std::string TryDirectory( std::string const& env, std::string const& suffix, bool check_exists )
{
	std::string path = mkstr(getenv(env.c_str()));
	if( !path.empty() && path[0] == '/' ) {
		if( path[path.size()-1] != '/' ) {
			path += '/';
		}

		path += suffix;

		if( check_exists ) {
			struct stat buf{};
			int res = stat(path.c_str(), &buf);
			if( res || !S_ISDIR(buf.st_mode) ) {
				path.clear();
			}
		}
	}
	else {
		path.clear();
	}
	return path;
}
}

std::string CInitializer::GetUnadjustedSettingsDir()
{
	std::string cfg = TryDirectory("XDG_CONFIG_HOME", "filezilla/", true);
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".config/filezilla/", true);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".filezilla/", true);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("XDG_CONFIG_HOME", "filezilla/", false);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".config/filezilla/", false);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".filezilla/", false);
	}

	return cfg;
}

std::string CInitializer::GetAdjustedSettingsDir()
{
	std::string defaults = GetDefaultsXmlFile();
	if (!defaults.empty())
	{
		std::string dir = CInitializer::ReadSettingsFromDefaults(defaults);
		if (!dir.empty())
			return dir;
	}

	return GetUnadjustedSettingsDir();
}

std::string CInitializer::GetLocaleOption()
{
	const std::string dir = GetAdjustedSettingsDir();
	if (dir.empty())
		return "";

#ifdef __WXDEBUG__
	printf("Reading locale option from %sfilezilla.xml\n", dir.c_str());
#endif
	std::string locale = GetSettingFromFile(dir + "filezilla.xml", "Language Code");

	return locale;
}

std::string CInitializer::LocaleAddEncoding(const std::string& locale, const std::string& encoding)
{
	int pos = locale.find('@');
	if (pos == -1)
		return locale + '.' + encoding;

	return locale.substr(0, pos) + '.' + encoding + locale.substr(pos);
}

#endif //__WXGTK__
