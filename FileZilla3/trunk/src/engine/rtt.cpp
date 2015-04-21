#include <filezilla.h>
#include "rtt.h"

CLatencyMeasurement::CLatencyMeasurement()
{
}

int CLatencyMeasurement::GetLatency() const
{
	scoped_lock lock(m_sync);
	if (!m_measurements)
		return -1;

	return static_cast<int>(m_summed_latency / m_measurements);
}

bool CLatencyMeasurement::Start()
{
	scoped_lock lock(m_sync);
	if (m_start)
		return false;

	m_start = CMonotonicClock::now();

	return true;
}

bool CLatencyMeasurement::Stop()
{
	scoped_lock lock(m_sync);
	if (!m_start)
		return false;

	duration const diff = CMonotonicClock::now() - m_start;
	m_start = CMonotonicClock();

	if (diff.get_milliseconds() < 0)
		return false;

	m_summed_latency += diff.get_milliseconds();
	++m_measurements;

	return true;
}

void CLatencyMeasurement::Reset()
{
	scoped_lock lock(m_sync);
	m_summed_latency = 0;
	m_measurements = 0;
	m_start = CMonotonicClock();
}

void CLatencyMeasurement::cb()
{
	Stop();
}
