#ifndef FILEZILLA_ENGINE_EVENT_HEADER
#define FILEZILLA_ENGINE_EVENT_HEADER

class CEventBase
{
public:
	CEventBase() {}
	virtual ~CEventBase() {}

	CEventBase(CEventBase const&) = delete;
	CEventBase& operator=(CEventBase const&) = delete;
};

template<typename UniqueType, typename...Values>
class CEvent final : public CEventBase
{
public:
	typedef UniqueType unique_type;
	typedef std::tuple<Values...> tuple_type;

	CEvent()
	{
	}

	template<typename First_Value, typename...Remaining_Values>
	explicit CEvent(First_Value&& value, Remaining_Values&& ...values)
		: v_(std::forward<First_Value>(value), std::forward<Remaining_Values>(values)...)
	{
	}

	CEvent(CEvent const& op)
		: v_(op.v_)
	{
	}

	CEvent& operator=(CEvent const& op) {
		if (this != &op) {
			v_ = op.v_;
		}
		return *this;
	}

	std::tuple<Values...> v_;
};

typedef unsigned long long timer_id;
struct timer_event_type{};
typedef CEvent<timer_event_type, timer_id> CTimerEvent;

#endif
