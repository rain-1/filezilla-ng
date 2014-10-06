#ifndef __RATELIMITER_H__
#define __RATELIMITER_H__

class COptionsBase;

class CRateLimiterObject;

// This class implements a simple rate limiter based on the Token Bucket algorithm.
class CRateLimiter final : protected CEventHandler
{
public:
	CRateLimiter(CEventLoop& loop, COptionsBase& options);

	enum rate_direction
	{
		inbound,
		outbound
	};

	void AddObject(CRateLimiterObject* pObject);
	void RemoveObject(CRateLimiterObject* pObject);

protected:
	wxLongLong GetLimit(enum rate_direction direction) const;

	int GetBucketSize() const;

	std::list<CRateLimiterObject*> m_objectList;
	std::list<CRateLimiterObject*> m_wakeupList[2];

	timer_id m_timer{};

	wxLongLong m_tokenDebt[2];

	COptionsBase& options_;

	void WakeupWaitingObjects();

	void operator()(CEventBase const& ev);
	void OnTimer(timer_id id);
};

class CRateLimiterObject
{
	friend class CRateLimiter;

public:
	CRateLimiterObject();
	virtual ~CRateLimiterObject() {}
	wxLongLong GetAvailableBytes(enum CRateLimiter::rate_direction direction) const { return m_bytesAvailable[direction]; }

	bool IsWaiting(enum CRateLimiter::rate_direction direction) const;

protected:
	void UpdateUsage(enum CRateLimiter::rate_direction direction, int usedBytes);
	void Wait(enum CRateLimiter::rate_direction direction);

	virtual void OnRateAvailable(enum CRateLimiter::rate_direction) {}

private:
	bool m_waiting[2];
	wxLongLong m_bytesAvailable[2];
};

#endif //__RATELIMITER_H__
