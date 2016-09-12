#include <filezilla.h>
#include "tlssocket.h"

#include <libfilezilla/time.hpp>

#include <wx/utils.h>

#include <gnutls/gnutls.h>
#include <sqlite3.h>
#include <random>
#include <cstdint>

wxString GetDependencyVersion(lib_dependency d)
{
	switch (d) {
	case lib_dependency::wxwidgets:
		return wxVERSION_NUM_DOT_STRING_T;
	case lib_dependency::gnutls:
		{
			const char* v = gnutls_check_version(0);
			if (!v || !*v)
				return _T("unknown");

			return wxString(v, wxConvLibc);
		}
	case lib_dependency::sqlite:
		return wxString::FromUTF8(sqlite3_libversion());
	default:
		return wxString();
	}
}

wxString GetDependencyName(lib_dependency d)
{
	switch (d) {
	case lib_dependency::wxwidgets:
		return _T("wxWidgets");
	case lib_dependency::gnutls:
		return _T("GnuTLS");
	case lib_dependency::sqlite:
		return _T("SQLite");
	default:
		return wxString();
	}
}

std::string ListTlsCiphers(std::string const& priority)
{
	return CTlsSocket::ListTlsCiphers(priority);
}

#ifdef __WXMSW__
bool IsAtLeast(int major, int minor = 0)
{
	OSVERSIONINFOEX vi = { 0 };
	vi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	vi.dwMajorVersion = major;
	vi.dwMinorVersion = minor;
	vi.dwPlatformId = VER_PLATFORM_WIN32_NT;

	DWORDLONG mask = 0;
	VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(mask, VER_MINORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(mask, VER_PLATFORMID, VER_EQUAL);
	return VerifyVersionInfo(&vi, VER_MAJORVERSION | VER_MINORVERSION | VER_PLATFORMID, mask) != 0;
}
#endif

bool GetRealOsVersion(int& major, int& minor)
{
#ifndef __WXMSW__
	return wxGetOsVersion(&major, &minor) != wxOS_UNKNOWN;
#else
	major = 4;
	minor = 0;
	while (IsAtLeast(++major, minor))
	{
	}
	--major;
	while (IsAtLeast(major, ++minor))
	{
	}
	--minor;

	return true;
#endif
}

std::wstring url_encode(std::wstring const& s, bool keep_slashes)
{
	std::wstring ret;

	std::string utf8 = fz::to_utf8(s);
	ret.reserve(utf8.size());

	for (auto const& c : utf8) {
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
			ret += fz::int_to_hex_char(static_cast<unsigned char>(c) >> 4);
			ret += fz::int_to_hex_char(c & 0xf);
		}
	}

	return ret;
}
