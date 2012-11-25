#include <filezilla.h>
#include "rtt.h"

CLatencyMeasurement::CLatencyMeasurement()
	: m_measurements()
{
}

int CLatencyMeasurement::GetLatency() const
{
	wxCriticalSectionLocker lock(m_sync);
	if (!m_measurements)
		return -1;

	return static_cast<int>(m_summed_latency.GetValue() / m_measurements);
}

bool CLatencyMeasurement::Start()
{
	wxCriticalSectionLocker lock(m_sync);
	if (m_start.IsValid())
		return false;

	m_start = wxDateTime::UNow();

	return true;
}

bool CLatencyMeasurement::Stop()
{
	wxCriticalSectionLocker lock(m_sync);
	if (!m_start.IsValid())
		return false;

	wxTimeSpan diff = wxDateTime::UNow() - m_start;
	m_start = wxDateTime();

	if (diff.GetMilliseconds() < 0)
		return false;

	m_summed_latency += diff.GetMilliseconds();
	++m_measurements;
	
	return true;
}

void CLatencyMeasurement::Reset()
{
	wxCriticalSectionLocker lock(m_sync);
	m_summed_latency = 0;
	m_measurements = 0;
	m_start = wxDateTime();
}

void CLatencyMeasurement::cb()
{
	Stop();
}