#include <filezilla.h>
#include <gnutls/gnutls.h>
#include <sqlite3.h>
#include <random>
#include <cstdint>

#include <libfilezilla/time.hpp>

#include "tlssocket.h"

wxString GetDependencyVersion(dependency::type d)
{
	switch (d) {
	case dependency::wxwidgets:
		return wxVERSION_NUM_DOT_STRING_T;
	case dependency::gnutls:
		{
			const char* v = gnutls_check_version(0);
			if (!v || !*v)
				return _T("unknown");

			return wxString(v, wxConvLibc);
		}
	case dependency::sqlite:
		return wxString::FromUTF8(sqlite3_libversion());
	default:
		return wxString();
	}
}

wxString GetDependencyName(dependency::type d)
{
	switch (d) {
	case dependency::wxwidgets:
		return _T("wxWidgets");
	case dependency::gnutls:
		return _T("GnuTLS");
	case dependency::sqlite:
		return _T("SQLite");
	default:
		return wxString();
	}
}

wxString ListTlsCiphers(const wxString& priority)
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
