#include <filezilla.h>
#include "engine_context.h"

#include "directorycache.h"
#include "event_loop.h"
#include "logging_private.h"
#include "pathcache.h"
#include "ratelimiter.h"
#include "socket.h"

namespace {
struct logging_options_changed_event_type;
typedef fz::simple_event<logging_options_changed_event_type> CLoggingOptionsChangedEvent;

class CLoggingOptionsChanged final : public fz::event_handler, COptionChangeEventHandler
{
public:
	CLoggingOptionsChanged(COptionsBase& options, fz::event_loop & loop)
		: fz::event_handler(loop)
		, options_(options)
	{
		RegisterOption(OPTION_LOGGING_DEBUGLEVEL);
		RegisterOption(OPTION_LOGGING_RAWLISTING);
		send_event<CLoggingOptionsChangedEvent>();
	}

	virtual void OnOptionsChanged(changed_options_t const& options)
	{
		if (options.test(OPTION_LOGGING_DEBUGLEVEL) || options.test(OPTION_LOGGING_RAWLISTING)) {
			CLogging::UpdateLogLevel(options_); // In main thread
			send_event<CLoggingOptionsChangedEvent>();
		}
	}

	virtual void operator()(const fz::event_base&)
	{
		CLogging::UpdateLogLevel(options_); // In worker thread
	}

	COptionsBase& options_;
};
}

class CFileZillaEngineContext::Impl final
{
public:
	Impl(COptionsBase& options)
		: limiter_(loop_, options)
		, optionChangeHandler_(options, loop_)
	{
		CLogging::UpdateLogLevel(options);
	}

	~Impl()
	{
		optionChangeHandler_.remove_handler();
	}

	fz::event_loop loop_;
	CRateLimiter limiter_;
	CDirectoryCache directory_cache_;
	CPathCache path_cache_;
	CLoggingOptionsChanged optionChangeHandler_;
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

fz::event_loop& CFileZillaEngineContext::GetEventLoop()
{
	return impl_->loop_;
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
