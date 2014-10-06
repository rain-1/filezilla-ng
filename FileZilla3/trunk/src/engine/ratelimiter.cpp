#include <filezilla.h>
#include "ratelimiter.h"

#include "event_loop.h"

static const int tickDelay = 250;

CRateLimiter::CRateLimiter(CEventLoop& loop, COptionsBase& options)
	: CEventHandler(loop)
	, options_(options)
{
	m_tokenDebt[0] = 0;
	m_tokenDebt[1] = 0;
}

wxLongLong CRateLimiter::GetLimit(enum rate_direction direction) const
{
	wxLongLong ret;
	if (options_.GetOptionVal(OPTION_SPEEDLIMIT_ENABLE) != 0) {
		ret = options_.GetOptionVal(OPTION_SPEEDLIMIT_INBOUND + direction) * 1024;
	}

	return ret;
}

void CRateLimiter::AddObject(CRateLimiterObject* pObject)
{
	m_objectList.push_back(pObject);

	for (int i = 0; i < 2; ++i) {
		wxLongLong limit = GetLimit((enum rate_direction)i);
		if (limit > 0) {
			wxLongLong tokens = limit / (1000 / tickDelay);

			tokens /= m_objectList.size();
			if (m_tokenDebt[i] > 0) {
				if (tokens >= m_tokenDebt[i]) {
					tokens -= m_tokenDebt[i];
					m_tokenDebt[i] = 0;
				}
				else {
					tokens = 0;
					m_tokenDebt[i] -= tokens;
				}
			}

			pObject->m_bytesAvailable[i] = tokens;
		}
		else
			pObject->m_bytesAvailable[i] = -1;


		if (!m_timer)
			m_timer = AddTimer(tickDelay, false);
	}
}

void CRateLimiter::RemoveObject(CRateLimiterObject* pObject)
{
	for (auto iter = m_objectList.begin(); iter != m_objectList.end(); ++iter) {
		if (*iter == pObject) {
			for (int i = 0; i < 2; ++i) {
				// If an object already used up some of its assigned tokens, add them to m_tokenDebt,
				// so that newly created objects get less initial tokens.
				// That ensures that rapidly adding and removing objects does not exceed the rate
				wxLongLong limit = GetLimit((enum rate_direction)i);
				wxLongLong tokens = limit / (1000 / tickDelay);
				tokens /= m_objectList.size();
				if ((*iter)->m_bytesAvailable[i] < tokens)
					m_tokenDebt[i] += tokens - (*iter)->m_bytesAvailable[i];
			}
			m_objectList.erase(iter);
			break;
		}
	}

	for (int i = 0; i < 2; ++i) {
		for (auto iter = m_wakeupList[i].begin(); iter != m_wakeupList[i].end(); ++iter) {
			if (*iter == pObject) {
				m_wakeupList[i].erase(iter);
				break;
			}
		}
	}
}

void CRateLimiter::OnTimer(timer_id)
{
	for (int i = 0; i < 2; ++i) {
		m_tokenDebt[i] = 0;

		if (m_objectList.empty())
			continue;

		wxLongLong limit = GetLimit((enum rate_direction)i);
		if (limit == 0) {
			for (auto iter = m_objectList.begin(); iter != m_objectList.end(); ++iter) {
				(*iter)->m_bytesAvailable[i] = -1;
				if ((*iter)->m_waiting[i])
					m_wakeupList[i].push_back(*iter);
			}
			continue;
		}

		wxLongLong tokens = (limit * tickDelay) / 1000;
		wxLongLong maxTokens = tokens * GetBucketSize();

		// Get amount of tokens for each object
		wxLongLong tokensPerObject = tokens / m_objectList.size();

		if (tokensPerObject == 0)
			tokensPerObject = 1;
		tokens = 0;

		// This list will hold all objects which didn't reach maxTokens
		std::list<CRateLimiterObject*> unsaturatedObjects;

		for (auto iter = m_objectList.begin(); iter != m_objectList.end(); ++iter) {
			if ((*iter)->m_bytesAvailable[i] == -1) {
				wxASSERT(!(*iter)->m_waiting[i]);
				(*iter)->m_bytesAvailable[i] = tokensPerObject;
				unsaturatedObjects.push_back(*iter);
			}
			else {
				(*iter)->m_bytesAvailable[i] += tokensPerObject;
				if ((*iter)->m_bytesAvailable[i] > maxTokens)
				{
					tokens += (*iter)->m_bytesAvailable[i] - maxTokens;
					(*iter)->m_bytesAvailable[i] = maxTokens;
				}
				else
					unsaturatedObjects.push_back(*iter);

				if ((*iter)->m_waiting[i])
					m_wakeupList[i].push_back(*iter);
			}
		}

		// If there are any left-over tokens (in case of objects with a rate below the limit)
		// assign to the unsaturated sources
		while (tokens != 0 && !unsaturatedObjects.empty()) {
			tokensPerObject = tokens / unsaturatedObjects.size();
			if (tokensPerObject == 0)
				break;
			tokens = 0;

			std::list<CRateLimiterObject*> objects;
			objects.swap(unsaturatedObjects);

			for (auto iter = objects.begin(); iter != objects.end(); ++iter) {
				(*iter)->m_bytesAvailable[i] += tokensPerObject;
				if ((*iter)->m_bytesAvailable[i] > maxTokens) {
					tokens += (*iter)->m_bytesAvailable[i] - maxTokens;
					(*iter)->m_bytesAvailable[i] = maxTokens;
				}
				else
					unsaturatedObjects.push_back(*iter);
			}
		}
	}
	WakeupWaitingObjects();

	if (m_objectList.empty()) {
		StopTimer(m_timer);
		m_timer = 0;
	}
}

void CRateLimiter::WakeupWaitingObjects()
{
	for (int i = 0; i < 2; ++i) {
		while (!m_wakeupList[i].empty()) {
			CRateLimiterObject* pObject = m_wakeupList[i].front();
			m_wakeupList[i].pop_front();
			if (!pObject->m_waiting[i])
				continue;

			wxASSERT(pObject->m_bytesAvailable != 0);
			pObject->m_waiting[i] = false;

			pObject->OnRateAvailable((rate_direction)i);
		}
	}
}

int CRateLimiter::GetBucketSize() const
{
	const int burst_tolerance = options_.GetOptionVal(OPTION_SPEEDLIMIT_BURSTTOLERANCE);

	int bucket_size = 1000 / tickDelay;
	switch (burst_tolerance)
	{
	case 1:
		bucket_size *= 2;
		break;
	case 2:
		bucket_size *= 5;
		break;
	default:
		break;
	}

	return bucket_size;
}

void CRateLimiter::operator()(CEventBase const& ev)
{
	Dispatch<CTimerEvent>(ev, this, &CRateLimiter::OnTimer);
}

CRateLimiterObject::CRateLimiterObject()
{
	for (int i = 0; i < 2; ++i) {
		m_waiting[i] = false;
		m_bytesAvailable[i] = -1;
	}
}

void CRateLimiterObject::UpdateUsage(enum CRateLimiter::rate_direction direction, int usedBytes)
{
	wxASSERT(usedBytes <= m_bytesAvailable[direction]);
	if (usedBytes > m_bytesAvailable[direction])
		m_bytesAvailable[direction] = 0;
	else
		m_bytesAvailable[direction] -= usedBytes;
}

void CRateLimiterObject::Wait(enum CRateLimiter::rate_direction direction)
{
	wxASSERT(m_bytesAvailable[direction] == 0);
	m_waiting[direction] = true;
}

bool CRateLimiterObject::IsWaiting(enum CRateLimiter::rate_direction direction) const
{
	return m_waiting[direction];
}
