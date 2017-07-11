#ifndef FILEZILLA_ENGINE_STORJ_EVENT_HEADER
#define FILEZILLA_ENGINE_STORJ_EVENT_HEADER

#include "../storj/events.hpp"

struct storj_message
{
	storjEvent type;
	mutable std::wstring text[4];
};

struct storj_event_type;
typedef fz::simple_event<storj_event_type, storj_message> CStorjEvent;

struct storj_terminate_event_type;
typedef fz::simple_event<storj_terminate_event_type, std::wstring> StorjTerminateEvent;

#endif
