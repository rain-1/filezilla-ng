#ifndef LIBFILEZILLA_EVENT_HEADER
#define LIBFILEZILLA_EVENT_HEADER

#include "libfilezilla.hpp"

namespace fz {

/*
Common base class for all events.

If possible, use CEvent<> below instead of deriving from CEventBase directly.

Keep events as simple as possible. Avoid mutexes in your events.
*/
class CEventBase
{
public:
	CEventBase() = default;
	virtual ~CEventBase() {}

	CEventBase(CEventBase const&) = delete;
	CEventBase& operator=(CEventBase const&) = delete;

	/*
	The returned pointer must be unique for the derived type such that:
		CEventBase& a = ...
		CEventBase& b = ...
		assert((a.derived_type() == b.derived_type()) == (typeid(a) == typeid(b)));

	Beware: Using &typeid is tempting, but unspecifined (sic)

	Best solution is to have your derived type return the address of a static data member of it
	*/
	virtual void const* derived_type() const = 0;
};

/*
This is the recommended event class.

Instanciate the template with a unique type to identify the type of the event and a number of types for the values.

Keep the values simple, in particular avoid mutexes in your values.

See event_handler.h for usage example.
*/
template<typename UniqueType, typename...Values>
class CEvent final : public CEventBase
{
public:
	typedef UniqueType unique_type;
	typedef std::tuple<Values...> tuple_type;

	CEvent() = default;

	template<typename First_Value, typename...Remaining_Values>
	explicit CEvent(First_Value&& value, Remaining_Values&& ...values)
		: v_(std::forward<First_Value>(value), std::forward<Remaining_Values>(values)...)
	{
	}

	CEvent(CEvent const& op) = default;
	CEvent& operator=(CEvent const& op) = default;

	// Returns a unique pointer for the type such that can be used directly in derived_type. 
	static void const* type() {
		static const char* f = 0;
		return &f;
	}

	virtual void const* derived_type() const {
		return type();
	}

	tuple_type v_;
};

// Returns true iff T& t = ...; t.derived_type() == ev.derived_type()
template<typename T>
bool same_type(CEventBase const& ev)
{
	return ev.derived_type() == T::type();
}

typedef unsigned long long timer_id;
struct timer_event_type{};
typedef CEvent<timer_event_type, timer_id> CTimerEvent;

}

#endif
