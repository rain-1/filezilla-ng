#ifndef FILEZILLA_ENGINE_DIRECTORYCACHE_HEADER
#define FILEZILLA_ENGINE_DIRECTORYCACHE_HEADER

/*
This class is the directory cache used to store retrieved directory listings
for further use.
Directory get either purged from the cache if the maximum cache time exceeds,
or on possible data inconsistencies.
For example since some servers are case sensitive and others aren't, a
directory is removed from cache once an operation effects a file wich matches
multiple entries in a cache directory using a case insensitive search
On other operations, the directory is marked as unsure. It may still be valid,
but for some operations the engine/interface prefers to retrieve a clean
version.
*/

#include <libfilezilla/mutex.hpp>

#include <set>

class CDirectoryCache final
{
public:
	enum Filetype
	{
		unknown,
		file,
		dir
	};

	CDirectoryCache();
	~CDirectoryCache();

	CDirectoryCache(CDirectoryCache const&) = delete;
	CDirectoryCache& operator=(CDirectoryCache const&) = delete;

	void Store(CDirectoryListing const& listing, CServer const& server);
	bool GetChangeTime(fz::monotonic_clock& time, CServer const& server, CServerPath const& path);
	bool Lookup(CDirectoryListing &listing, CServer const&server, CServerPath const& path, bool allowUnsureEntries, bool& is_outdated);
	bool DoesExist(CServer const& server, CServerPath const& path, int &hasUnsureEntries, bool &is_outdated);
	bool LookupFile(CDirentry &entry, CServer const& server, CServerPath const& path, std::wstring const& filename, bool &dirDidExist, bool &matchedCase);
	bool InvalidateFile(CServer const& server, CServerPath const& path, std::wstring const& filename, bool *wasDir = 0);
	bool UpdateFile(CServer const& server, CServerPath const& path, std::wstring const& filename, bool mayCreate, Filetype type = file, int64_t size = -1);
	bool RemoveFile(CServer const& server, CServerPath const& path, std::wstring const& filename);
	void InvalidateServer(CServer const& server);
	void RemoveDir(CServer const& server, CServerPath const& path, std::wstring const& filename, CServerPath const& target);
	void Rename(CServer const& server, CServerPath const& pathFrom, std::wstring const& fileFrom, CServerPath const& pathTo, std::wstring const& fileTo);

	void SetTtl(fz::duration const& ttl);

protected:

	class CCacheEntry final
	{
	public:
		CCacheEntry() = default;
		CCacheEntry(CCacheEntry const& entry) = default;
		CCacheEntry(CCacheEntry && entry) noexcept = default;

		explicit CCacheEntry(CDirectoryListing const& l)
			: listing(l)
			, modificationTime(fz::monotonic_clock::now())
		{}

		CDirectoryListing listing;
		fz::monotonic_clock modificationTime;

		CCacheEntry& operator=(CCacheEntry const& a) = default;
		CCacheEntry& operator=(CCacheEntry && a) noexcept = default;

		void* lruIt{}; // void* to break cyclic declaration dependency

		bool operator<(CCacheEntry const& op) const noexcept {
			return listing.path < op.listing.path;
		}
	};

	class CServerEntry final
	{
	public:
		CServerEntry() {}
		explicit CServerEntry(CServer const& s)
			: server(s)
		{}

		CServer server;
		std::set<CCacheEntry> cacheList;
	};

	typedef std::list<CServerEntry>::iterator tServerIter;

	tServerIter CreateServerEntry(const CServer& server);
	tServerIter GetServerEntry(const CServer& server);

	typedef std::set<CCacheEntry>::iterator tCacheIter;
	typedef std::set<CCacheEntry>::const_iterator tCacheConstIter;

	bool Lookup(tCacheIter &cacheIter, tServerIter &sit, CServerPath const& path, bool allowUnsureEntries, bool& is_outdated);

	fz::mutex mutex_;

	std::list<CServerEntry> m_serverList;

	void UpdateLru(tServerIter const& sit, tCacheIter const& cit);

	void Prune();

	typedef std::pair<tServerIter, tCacheIter> tFullEntryPosition;
	typedef std::list<tFullEntryPosition> tLruList;
	tLruList m_leastRecentlyUsedList;

	int64_t m_totalFileCount{};

	fz::duration ttl_{fz::duration::from_seconds(600)};
};

#endif
