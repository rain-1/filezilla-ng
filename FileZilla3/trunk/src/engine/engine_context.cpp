#include <filezilla.h>
#include "engine_context.h"

#include "directorycache.h"
#include "pathcache.h"
#include "ratelimiter.h"
#include "socket.h"

class CFileZillaEngineContext::Impl
{
public:
	Impl(COptionsBase& options)
		: dispatcher_(loop_)
		, limiter_(loop_, options)
	{
	}

	~Impl()
	{
	}

	CEventLoop loop_;
	CSocketEventDispatcher dispatcher_;
	CRateLimiter limiter_;
	CDirectoryCache directory_cache_;
	CPathCache path_cache_;
};

CFileZillaEngineContext::CFileZillaEngineContext(COptionsBase & options)
: options_(options)
, impl_(new Impl(options))
{
}

CFileZillaEngineContext::~CFileZillaEngineContext()
{
}

COptionsBase& CFileZillaEngineContext::GetOptions()
{
	return options_;
}

CEventLoop& CFileZillaEngineContext::GetEventLoop()
{
	return impl_->loop_;
}

CSocketEventDispatcher& CFileZillaEngineContext::GetSocketEventDispatcher()
{
	return impl_->dispatcher_;
}

CRateLimiter& CFileZillaEngineContext::GetRateLimiter()
{
	return impl_->limiter_;
}

CDirectoryCache& CFileZillaEngineContext::GetDirectoryCache()
{
	return impl_->directory_cache_;
}

CPathCache& CFileZillaEngineContext::GetPathCache()
{
	return impl_->path_cache_;
}
