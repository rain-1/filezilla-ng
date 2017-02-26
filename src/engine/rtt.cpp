#include <filezilla.h>
#include "rtt.h"

int CLatencyMeasurement::GetLatency() const
{
	fz::scoped_lock lock(m_sync);
	if (!m_measurements) {
		return -1;
	}

	return static_cast<int>(m_summed_latency / m_measurements);
}

bool CLatencyMeasurement::Start()
{
	fz::scoped_lock lock(m_sync);
	if (m_start) {
		return false;
	}

	m_start = fz::monotonic_clock::now();

	return true;
}

bool CLatencyMeasurement::Stop()
{
	fz::scoped_lock lock(m_sync);
	if (!m_start) {
		return false;
	}

	fz::duration const diff = fz::monotonic_clock::now() - m_start;
	m_start = fz::monotonic_clock();

	if (diff.get_milliseconds() < 0) {
		return false;
	}

	m_summed_latency += diff.get_milliseconds();
	++m_measurements;

	return true;
}

void CLatencyMeasurement::Reset()
{
	fz::scoped_lock lock(m_sync);
	m_summed_latency = 0;
	m_measurements = 0;
	m_start = fz::monotonic_clock();
}
