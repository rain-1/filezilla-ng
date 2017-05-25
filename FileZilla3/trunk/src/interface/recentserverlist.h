#ifndef FILEZILLA_INTERFACE_RECENTSERVERLIST_HEADER
#define FILEZILLA_INTERFACE_RECENTSERVERLIST_HEADER

#include "xmlfunctions.h"

#include <deque>

class CRecentServerList
{
public:
	static void SetMostRecentServer(ServerWithCredentials const& server);
	static void SetMostRecentServers(std::deque<ServerWithCredentials> const& servers, bool lockMutex = true);
	static const std::deque<ServerWithCredentials> GetMostRecentServers(bool lockMutex = true);
	static void Clear();
};

#endif
