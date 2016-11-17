#include <filezilla.h>
#include "pathcache.h"

#include <assert.h>

CPathCache::CPathCache()
{
}

CPathCache::~CPathCache()
{
}

void CPathCache::Store(CServer const& server, CServerPath const& target, CServerPath const& source, std::wstring const& subdir)
{
	fz::scoped_lock lock(mutex_);

	assert(!target.empty() && !source.empty());

	tCacheIterator iter = m_cache.find(server);
	if (iter == m_cache.cend()) {
		iter = m_cache.emplace(std::make_pair(server, tServerCache())).first;
	}
	tServerCache &serverCache = iter->second;

	CSourcePath sourcePath;

	sourcePath.source = source;
	sourcePath.subdir = subdir;

	serverCache[sourcePath] = target;
}

CServerPath CPathCache::Lookup(CServer const& server, CServerPath const& source, std::wstring const& subdir)
{
	fz::scoped_lock lock(mutex_);

	const tCacheConstIterator iter = m_cache.find(server);
	if (iter == m_cache.end()) {
		return CServerPath();
	}

	CServerPath result = Lookup(iter->second, source, subdir);

	if (result.empty()) {
		m_misses++;
	}
	else {
		m_hits++;
	}

	return result;
}

CServerPath CPathCache::Lookup(tServerCache const& serverCache, CServerPath const& source, std::wstring const& subdir)
{
	CSourcePath sourcePath;
	sourcePath.source = source;
	sourcePath.subdir = subdir;

	tServerCacheConstIterator serverIter = serverCache.find(sourcePath);
	if (serverIter == serverCache.end())
		return CServerPath();

	return serverIter->second;
}

void CPathCache::InvalidateServer(CServer const& server)
{
	fz::scoped_lock lock(mutex_);

	tCacheIterator iter = m_cache.find(server);
	if (iter == m_cache.end())
		return;

	m_cache.erase(iter);
}

void CPathCache::InvalidatePath(CServer const& server, CServerPath const& path, std::wstring const& subdir)
{
	fz::scoped_lock lock(mutex_);

	tCacheIterator iter = m_cache.find(server);
	if (iter != m_cache.end()) {
		InvalidatePath(iter->second, path, subdir);
	}
}

void CPathCache::InvalidatePath(tServerCache & serverCache, CServerPath const& path, std::wstring const& subdir)
{
	CSourcePath sourcePath;

	sourcePath.source = path;
	sourcePath.subdir = subdir;

	CServerPath target;
	tServerCacheIterator serverIter = serverCache.find(sourcePath);
	if (serverIter != serverCache.end()) {
		target = serverIter->second;
		serverCache.erase(serverIter);
	}

	if (target.empty() && !subdir.empty()) {
		target = path;
		if (!target.AddSegment(subdir)) {
			return;
		}
	}

	if (!target.empty()) {
		// Unfortunately O(n), don't know of a faster way.
		for (serverIter = serverCache.begin(); serverIter != serverCache.end(); ) {
			if (serverIter->second == target || target.IsParentOf(serverIter->second, false)) {
				serverCache.erase(serverIter++);
			}
			else if (serverIter->first.source == target || target.IsParentOf(serverIter->first.source, false)) {
				serverCache.erase(serverIter++);
			}
			else {
				++serverIter;
			}
		}
	}
}

void CPathCache::Clear()
{
	fz::scoped_lock lock(mutex_);
	m_cache.clear();
}
