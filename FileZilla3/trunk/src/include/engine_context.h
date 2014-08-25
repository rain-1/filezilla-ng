#ifndef FILEZILLA_ENGINE_CONTEXT
#define FILEZILLA_ENGINE_CONTEXT

#include <memory>

class CDirectoryCache;
class CEventLoop;
class COptionsBase;
class CRateLimiter;
class CSocketEventDispatcher;

// There can be multiple engines, but there can be at most one context
class CFileZillaEngineContext
{
public:
	CFileZillaEngineContext(COptionsBase & options);
	~CFileZillaEngineContext();

	COptionsBase& GetOptions();
	CEventLoop& GetEventLoop();
	CSocketEventDispatcher& GetSocketEventDispatcher();
	CRateLimiter& GetRateLimiter();
	CDirectoryCache& GetDirectoryCache();

protected:
	COptionsBase& options_;

	class Impl;
	std::unique_ptr<Impl> impl_;
};

#endif