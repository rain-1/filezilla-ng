#include <filezilla.h>
#include "tlssocket.h"

#include <libfilezilla/format.hpp>
#include <libfilezilla/time.hpp>

#include <random>
#include <cstdint>
#include <cwctype>

#include <string.h>

std::wstring GetDependencyVersion(lib_dependency d)
{
	switch (d) {
	case lib_dependency::gnutls:
		return CTlsSocket::GetGnutlsVersion();
	default:
		return std::wstring();
	}
}

std::wstring GetDependencyName(lib_dependency d)
{
	switch (d) {
	case lib_dependency::gnutls:
		return L"GnuTLS";
	default:
		return std::wstring();
	}
}

std::string ListTlsCiphers(std::string const& priority)
{
	return CTlsSocket::ListTlsCiphers(priority);
}

#if FZ_WINDOWS
DWORD GetSystemErrorCode()
{
	return GetLastError();
}

std::wstring GetSystemErrorDescription(DWORD err)
{
	wchar_t* buf{};
	if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, 0, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buf), 0, 0) || !buf) {
		return fz::sprintf(_("Unknown error %u"), err);
	}
	std::wstring ret = buf;
	LocalFree(buf);

	return ret;
}
#else
int GetSystemErrorCode()
{
	return errno;
}

namespace {
inline std::string ProcessStrerrorResult(int ret, char* buf, int err)
{
	// For XSI strerror_r
	std::string s;
	if (!ret) {
		buf[999] = 0;
		s = buf;
	}
	else {
		s = fz::to_string(fz::sprintf(_("Unknown error %d"), err));
	}
	return s;
}

inline std::string ProcessStrerrorResult(char* ret, char*, int err)
{
	// For GNU strerror_r
	std::string s;
	if (ret) {
		s = ret;
	}
	else {
		s = fz::to_string(fz::sprintf(_("Unknown error %d"), err));
	}
	return s;
}
}

std::string GetSystemErrorDescription(int err)
{
	char buf[1000];
	auto ret = strerror_r(err, buf, 1000);
	return ProcessStrerrorResult(ret, buf, err);
}
#endif

namespace fz {

namespace {
std::wstring default_translator(char const* const t)
{
	return fz::to_wstring(t);
}

std::wstring default_translator_pf(char const* const singular, char const* const plural, int64_t n)
{
	return fz::to_wstring((n == 1) ? singular : plural);
}

std::wstring(*translator)(char const* const) = default_translator;
std::wstring(*translator_pf)(char const* const singular, char const* const plural, int64_t n) = default_translator_pf;
}

void set_translators(
	std::wstring(*s)(char const* const t),
	std::wstring(*pf)(char const* const singular, char const* const plural, int64_t n)
)
{
	translator = s ? s : default_translator;
	translator_pf = pf ? pf : default_translator_pf;
}

std::wstring translate(char const * const t)
{
	return translator(t);
}

std::wstring translate(char const * const singular, char const * const plural, int64_t n)
{
	return translator_pf(singular, plural, n);
}

std::wstring str_tolower(std::wstring const& source)
{
	std::wstring ret;
	for (auto const& c : source) {
		ret.push_back(std::towlower(c));
	}
	return ret;
}

}
