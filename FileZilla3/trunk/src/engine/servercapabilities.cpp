#include <filezilla.h>
#include "servercapabilities.h"

#include <assert.h>

std::map<CServer, CCapabilities> CServerCapabilities::m_serverMap;

capabilities CCapabilities::GetCapability(capabilityNames name, std::wstring* pOption) const
{
	const std::map<capabilityNames, CCapabilities::t_cap>::const_iterator iter = m_capabilityMap.find(name);
	if (iter == m_capabilityMap.end()) {
		return unknown;
	}

	if (iter->second.cap == yes && pOption) {
		*pOption = iter->second.option;
	}
	return iter->second.cap;
}

capabilities CCapabilities::GetCapability(capabilityNames name, int* pOption) const
{
	const std::map<capabilityNames, CCapabilities::t_cap>::const_iterator iter = m_capabilityMap.find(name);
	if (iter == m_capabilityMap.end()) {
		return unknown;
	}

	if (iter->second.cap == yes && pOption) {
		*pOption = iter->second.number;
	}
	return iter->second.cap;
}

void CCapabilities::SetCapability(capabilityNames name, capabilities cap, std::wstring const& option)
{
	assert(cap == yes || option.empty());
	CCapabilities::t_cap tcap;
	tcap.cap = cap;
	tcap.option = option;
	tcap.number = 0;

	m_capabilityMap[name] = tcap;
}

void CCapabilities::SetCapability(capabilityNames name, capabilities cap, int option)
{
	assert(cap == yes || option == 0);
	CCapabilities::t_cap tcap;
	tcap.cap = cap;
	tcap.number = option;

	m_capabilityMap[name] = tcap;
}

capabilities CServerCapabilities::GetCapability(const CServer& server, capabilityNames name, std::wstring* pOption)
{
	const std::map<CServer, CCapabilities>::const_iterator iter = m_serverMap.find(server);
	if (iter == m_serverMap.end()) {
		return unknown;
	}

	return iter->second.GetCapability(name, pOption);
}

capabilities CServerCapabilities::GetCapability(const CServer& server, capabilityNames name, int* pOption)
{
	const std::map<CServer, CCapabilities>::const_iterator iter = m_serverMap.find(server);
	if (iter == m_serverMap.end()) {
		return unknown;
	}

	return iter->second.GetCapability(name, pOption);
}

void CServerCapabilities::SetCapability(const CServer& server, capabilityNames name, capabilities cap, std::wstring const& option)
{
	const std::map<CServer, CCapabilities>::iterator iter = m_serverMap.find(server);
	if (iter == m_serverMap.end()) {
		CCapabilities capabilities;
		capabilities.SetCapability(name, cap, option);
		m_serverMap[server] = capabilities;
		return;
	}

	iter->second.SetCapability(name, cap, option);
}

void CServerCapabilities::SetCapability(const CServer& server, capabilityNames name, capabilities cap, int option)
{
	const std::map<CServer, CCapabilities>::iterator iter = m_serverMap.find(server);
	if (iter == m_serverMap.end()) {
		CCapabilities capabilities;
		capabilities.SetCapability(name, cap, option);
		m_serverMap[server] = capabilities;
		return;
	}

	iter->second.SetCapability(name, cap, option);
}
