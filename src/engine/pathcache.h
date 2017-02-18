#ifndef FILEZILLA_ENGINE_PATHCACHE_HEADER
#define FILEZILLA_ENGINE_PATHCACHE_HEADER

#include <libfilezilla/mutex.hpp>

class CPathCache final
{
public:
	CPathCache();
	~CPathCache();

	CPathCache(CPathCache const&) = delete;
	CPathCache& operator=(CPathCache const&) = delete;

	// The source argument should be a canonicalized path already if subdir is non-empty
	void Store(CServer const& server, CServerPath const& target, CServerPath const& source, std::wstring const& subdir = std::wstring());

	// The source argument should be a canonicalized path already if subdir is non-empty happen
	CServerPath Lookup(CServer const& server, CServerPath const& source, std::wstring const& subdir = std::wstring());

	void InvalidateServer(CServer const& server);

	// Invalidate path
	void InvalidatePath(CServer const& server, CServerPath const& path, std::wstring const& subdir = std::wstring());

	void Clear();

protected:
	class CSourcePath
	{
	public:
		CServerPath source;
		std::wstring subdir;

		bool operator<(CSourcePath const& op) const
		{
			return std::tie(subdir, source) < std::tie(op.subdir, op.source);
		}
	};

	fz::mutex mutex_;

	typedef std::map<CSourcePath, CServerPath> tServerCache;
	typedef tServerCache::iterator tServerCacheIterator;
	typedef tServerCache::const_iterator tServerCacheConstIterator;
	typedef std::map<CServer, tServerCache> tCache;
	tCache m_cache;
	typedef tCache::iterator tCacheIterator;
	typedef tCache::const_iterator tCacheConstIterator;

	CServerPath Lookup(tServerCache const& serverCache, CServerPath const& source, std::wstring const& subdir);
	void InvalidatePath(tServerCache & serverCache, CServerPath const& path, std::wstring const& subdir = std::wstring());

	int m_hits{};
	int m_misses{};

};

#endif
