#ifndef FILEZILLA_ENGINE_MISC_HEADER
#define FILEZILLA_ENGINE_MISC_HEADER

#include <libfilezilla/event_handler.hpp>

enum class lib_dependency
{
	gnutls,
	count
};

std::wstring GetDependencyName(lib_dependency d);
std::wstring GetDependencyVersion(lib_dependency d);

std::string ListTlsCiphers(std::string const& priority);

template<typename Derived, typename Base>
std::unique_ptr<Derived>
unique_static_cast(std::unique_ptr<Base>&& p)
{
	auto d = static_cast<Derived *>(p.release());
	return std::unique_ptr<Derived>(d);
}

#if FZ_WINDOWS
DWORD GetSystemErrorCode();
fz::native_string GetSystemErrorDescription(DWORD err);
#else
int GetSystemErrorCode();
fz::native_string GetSystemErrorDescription(int err);
#endif

namespace fz {

void set_translators(
	std::wstring(*s)(char const* const t),
	std::wstring(*pf)(char const* const singular, char const* const plural, int64_t n)
);

std::wstring translate(char const* const source);
std::wstring translate(char const * const singular, char const * const plural, int64_t n);

// Poor-man's tolower. Consider to eventually use libicu or similar
std::wstring str_tolower(std::wstring const& source);
}

// Sadly xgettext cannot be used with namespaces
#define fztranslate fz::translate
#define fztranslate_mark

#endif
