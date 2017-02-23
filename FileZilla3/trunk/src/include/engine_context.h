#ifndef FILEZILLA_ENGINE_CONTEXT_HEADER
#define FILEZILLA_ENGINE_CONTEXT_HEADER

#include <memory>

class CDirectoryCache;
class COptionsBase;
class CPathCache;
class CRateLimiter;

namespace fz {
class event_loop;
class thread_pool;
}


class CustomEncodingConverterBase
{
public:
	virtual ~CustomEncodingConverterBase() = default;

	virtual std::wstring toLocal(std::wstring const& encoding, char const* buffer, size_t len) const = 0;
	virtual std::string toServer(std::wstring const& encoding, wchar_t const* buffer, size_t len) const = 0;
};

// There can be multiple engines, but there can be at most one context
class CFileZillaEngineContext final
{
public:
	CFileZillaEngineContext(COptionsBase & options, CustomEncodingConverterBase const& customEncodingConverter);
	~CFileZillaEngineContext();

	COptionsBase& GetOptions() { return options_; }
	fz::thread_pool& GetThreadPool();
	fz::event_loop& GetEventLoop();
	CRateLimiter& GetRateLimiter();
	CDirectoryCache& GetDirectoryCache();
	CPathCache& GetPathCache();
	CustomEncodingConverterBase const& GetCustomEncodingConverter() { return customEncodingConverter_; }

protected:
	COptionsBase& options_;
	CustomEncodingConverterBase const& customEncodingConverter_;

	class Impl;
	std::unique_ptr<Impl> impl_;
};

#endif