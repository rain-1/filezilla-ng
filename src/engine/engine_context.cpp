#include <filezilla.h>
#include "engine_context.h"

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
