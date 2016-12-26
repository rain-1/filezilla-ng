#include <filezilla.h>
#include "directorycache.h"

#include <assert.h>

CDirectoryCache::CDirectoryCache()
{
}

CDirectoryCache::~CDirectoryCache()
{
	for (auto & serverEntry : m_serverList) {
		for (auto & cacheEntry : serverEntry.cacheList) {
#ifndef NDEBUG
			m_totalFileCount -= cacheEntry.listing.GetCount();
#endif
			tLruList::iterator* lruIt = (tLruList::iterator*)cacheEntry.lruIt;
			if (lruIt) {
				m_leastRecentlyUsedList.erase(*lruIt);
				delete lruIt;
			}
		}
	}
#ifndef NDEBUG
	assert(m_totalFileCount == 0);
#endif
}

void CDirectoryCache::Store(CDirectoryListing const& listing, CServer const& server)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = CreateServerEntry(server);
	assert(sit != m_serverList.end());

	m_totalFileCount += listing.GetCount();

	tCacheIter cit;
	bool unused;
	if (Lookup(cit, sit, listing.path, true, unused)) {
		auto & entry = const_cast<CCacheEntry&>(*cit);
		entry.modificationTime = fz::monotonic_clock::now();

		m_totalFileCount -= cit->listing.GetCount();
		entry.listing = listing;

		return;
	}

	cit = sit->cacheList.emplace_hint(cit, listing);

	UpdateLru(sit, cit);

	Prune();
}

bool CDirectoryCache::Lookup(CDirectoryListing &listing, CServer const& server, const CServerPath &path, bool allowUnsureEntries, bool& is_outdated)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return false;
	}

	tCacheIter iter;
	if (Lookup(iter, sit, path, allowUnsureEntries, is_outdated)) {
		listing = iter->listing;
		return true;
	}

	return false;
}

bool CDirectoryCache::Lookup(tCacheIter &cacheIter, tServerIter &sit, CServerPath const& path, bool allowUnsureEntries, bool& is_outdated)
{
	static CCacheEntry dummy;
	dummy.listing.path = path;
	cacheIter = sit->cacheList.lower_bound(dummy);

	if (cacheIter != sit->cacheList.end()) {
		CCacheEntry const& entry = *cacheIter;

		if (entry.listing.path == path) {
			UpdateLru(sit, cacheIter);

			if (!allowUnsureEntries && entry.listing.get_unsure_flags()) {
				return false;
			}

			is_outdated = (fz::monotonic_clock::now() - entry.listing.m_firstListTime) > ttl_;
			return true;
		}
	}

	return false;
}

bool CDirectoryCache::DoesExist(CServer const& server, CServerPath const& path, int &hasUnsureEntries, bool &is_outdated)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return false;
	}

	tCacheIter iter;
	if (Lookup(iter, sit, path, true, is_outdated)) {
		hasUnsureEntries = iter->listing.get_unsure_flags();
		return true;
	}

	return false;
}

bool CDirectoryCache::LookupFile(CDirentry &entry, CServer const& server, CServerPath const& path, std::wstring const& filename, bool &dirDidExist, bool &matchedCase)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		dirDidExist = false;
		return false;
	}

	tCacheIter iter;
	bool unused;
	if (!Lookup(iter, sit, path, true, unused)) {
		dirDidExist = false;
		return false;
	}
	dirDidExist = true;

	const CCacheEntry &cacheEntry = *iter;
	const CDirectoryListing &listing = cacheEntry.listing;

	int i = listing.FindFile_CmpCase(filename);
	if (i >= 0) {
		entry = listing[i];
		matchedCase = true;
		return true;
	}
	i = listing.FindFile_CmpNoCase(filename);
	if (i >= 0) {
		entry = listing[i];
		matchedCase = false;
		return true;
	}

	return false;
}

bool CDirectoryCache::InvalidateFile(CServer const& server, CServerPath const& path, std::wstring const& filename, bool *wasDir)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return false;
	}

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ++iter) {
		auto & entry = const_cast<CCacheEntry&>(*iter);
		if (path.CmpNoCase(entry.listing.path)) {
			continue;
		}

		UpdateLru(sit, iter);

		for (unsigned int i = 0; i < entry.listing.GetCount(); i++) {
			if (!fz::stricmp(filename, entry.listing[i].name)) {
				if (wasDir) {
					*wasDir = entry.listing[i].is_dir();
				}
				entry.listing.get(i).flags |= CDirentry::flag_unsure;
			}
		}
		entry.listing.m_flags |= CDirectoryListing::unsure_unknown;
		entry.modificationTime = fz::monotonic_clock::now();
	}

	return true;
}

bool CDirectoryCache::UpdateFile(CServer const& server, CServerPath const& path, std::wstring const& filename, bool mayCreate, Filetype type, int64_t size)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return false;
	}

	bool updated = false;

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ++iter) {
		auto & entry = const_cast<CCacheEntry&>(*iter);
		if (path.CmpNoCase(entry.listing.path)) {
			continue;
		}

		UpdateLru(sit, iter);

		bool matchCase = false;
		unsigned int i;
		for (i = 0; i < entry.listing.GetCount(); ++i) {
			if (!fz::stricmp(filename, entry.listing[i].name)) {
				entry.listing.get(i).flags |= CDirentry::flag_unsure;
				if (entry.listing[i].name == filename) {
					matchCase = true;
					break;
				}
			}
		}

		if (matchCase) {
			Filetype old_type = entry.listing[i].is_dir() ? dir : file;
			if (type != old_type) {
				entry.listing.m_flags |= CDirectoryListing::unsure_invalid;
			}
			else if (type == dir) {
				entry.listing.m_flags |= CDirectoryListing::unsure_dir_changed;
			}
			else {
				entry.listing.m_flags |= CDirectoryListing::unsure_file_changed;
			}
		}
		else if (type != unknown && mayCreate) {
			CDirentry direntry;
			direntry.name = filename;
			if (type == dir) {
				direntry.flags = CDirentry::flag_dir | CDirentry::flag_unsure;
			}
			else {
				direntry.flags = CDirentry::flag_unsure;
			}
			direntry.size = size;
			switch (type) {
			case dir:
				entry.listing.m_flags |= CDirectoryListing::unsure_dir_added | CDirectoryListing::listing_has_dirs;
				break;
			case file:
				entry.listing.m_flags |= CDirectoryListing::unsure_file_added;
				break;
			default:
				entry.listing.m_flags |= CDirectoryListing::unsure_invalid;
				break;
			}
			entry.listing.Append(std::move(direntry));

			++m_totalFileCount;
		}
		else {
			entry.listing.m_flags |= CDirectoryListing::unsure_unknown;
		}
		entry.modificationTime = fz::monotonic_clock::now();

		updated = true;
	}

	return updated;
}

bool CDirectoryCache::RemoveFile(CServer const& server, CServerPath const& path, std::wstring const& filename)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return false;
	}

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ++iter) {
		auto & entry = const_cast<CCacheEntry&>(*iter);
		if (path.CmpNoCase(entry.listing.path)) {
			continue;
		}

		UpdateLru(sit, iter);

		bool matchCase = false;
		for (unsigned int i = 0; i < entry.listing.GetCount(); ++i) {
			if (entry.listing[i].name == filename) {
				matchCase = true;
			}
		}

		if (matchCase) {
			unsigned int i;
			for (i = 0; i < entry.listing.GetCount(); ++i) {
				if (entry.listing[i].name == filename) {
					break;
				}
			}
			assert(i != entry.listing.GetCount());

			entry.listing.RemoveEntry(i); // This does set m_hasUnsureEntries
			--m_totalFileCount;
		}
		else {
			for (unsigned int i = 0; i < entry.listing.GetCount(); ++i) {
				if (!fz::stricmp(filename, entry.listing[i].name)) {
					entry.listing.get(i).flags |= CDirentry::flag_unsure;
				}
			}
			entry.listing.m_flags |= CDirectoryListing::unsure_invalid;
		}
		entry.modificationTime = fz::monotonic_clock::now();
	}

	return true;
}

void CDirectoryCache::InvalidateServer(CServer const& server)
{
	fz::scoped_lock lock(mutex_);

	for (auto iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
		if (iter->server != server) {
			continue;
		}

		for (tCacheIter cit = iter->cacheList.begin(); cit != iter->cacheList.end(); ++cit) {
			tLruList::iterator* lruIt = (tLruList::iterator*)cit->lruIt;
			if (lruIt) {
				m_leastRecentlyUsedList.erase(*lruIt);
				delete lruIt;
			}

			m_totalFileCount -= cit->listing.GetCount();
		}

		m_serverList.erase(iter);
		break;
	}
}

bool CDirectoryCache::GetChangeTime(fz::monotonic_clock& time, CServer const& server, CServerPath const& path)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return false;
	}

	tCacheIter iter;
	bool unused;
	if (Lookup(iter, sit, path, true, unused)) {
		time = iter->modificationTime;
		return true;
	}

	return false;
}

void CDirectoryCache::RemoveDir(CServer const& server, CServerPath const& path, std::wstring const& filename, CServerPath const&)
{
	fz::scoped_lock lock(mutex_);

	// TODO: This is not 100% foolproof and may not work properly
	// Perhaps just throw away the complete cache?

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return;
	}

	CServerPath absolutePath = path;
	if (!absolutePath.AddSegment(filename)) {
		absolutePath.clear();
	}

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ) {
		auto & entry = const_cast<CCacheEntry&>(*iter);
		// Delete exact matches and subdirs
		if (!absolutePath.empty() && (entry.listing.path == absolutePath || absolutePath.IsParentOf(entry.listing.path, true))) {
			m_totalFileCount -= entry.listing.GetCount();
			tLruList::iterator* lruIt = (tLruList::iterator*)iter->lruIt;
			if (lruIt) {
				m_leastRecentlyUsedList.erase(*lruIt);
				delete lruIt;
			}
			sit->cacheList.erase(iter++);
		}
		else {
			++iter;
		}
	}

	RemoveFile(server, path, filename);
}

void CDirectoryCache::Rename(CServer const& server, CServerPath const& pathFrom, std::wstring const& fileFrom, CServerPath const& pathTo, std::wstring const& fileTo)
{
	fz::scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		return;
	}

	tCacheIter iter;
	bool is_outdated = false;
	bool found = Lookup(iter, sit, pathFrom, true, is_outdated);
	if (found) {
		auto & listing = const_cast<CDirectoryListing&>(iter->listing);
		if (pathFrom == pathTo) {
			RemoveFile(server, pathFrom, fileTo);
			unsigned int i;
			for (i = 0; i < listing.GetCount(); ++i) {
				if (listing[i].name == fileFrom) {
					break;
				}
			}
			if (i != listing.GetCount()) {
				if (listing[i].is_dir()) {
					RemoveDir(server, pathFrom, fileFrom, CServerPath());
					RemoveDir(server, pathFrom, fileTo, CServerPath());
					UpdateFile(server, pathFrom, fileTo, true, dir);
				}
				else {
					listing.get(i).name = fileTo;
					listing.get(i).flags |= CDirentry::flag_unsure;
					listing.m_flags |= CDirectoryListing::unsure_unknown;
					listing.ClearFindMap();
				}
			}
			return;
		}
		else {
			unsigned int i;
			for (i = 0; i < listing.GetCount(); ++i) {
				if (listing[i].name == fileFrom) {
					break;
				}
			}
			if (i != listing.GetCount()) {
				if (listing[i].is_dir()) {
					RemoveDir(server, pathFrom, fileFrom, CServerPath());
					UpdateFile(server, pathTo, fileTo, true, dir);
				}
				else {
					RemoveFile(server, pathFrom, fileFrom);
					UpdateFile(server, pathTo, fileTo, true, file);
				}
			}
			return;
		}
	}

	// We know nothing, be on the safe side and invalidate everything.
	InvalidateServer(server);
}

CDirectoryCache::tServerIter CDirectoryCache::CreateServerEntry(CServer const& server)
{
	for (tServerIter iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
		if (iter->server == server) {
			return iter;
		}
	}
	m_serverList.emplace_back(server);

	return --m_serverList.end();
}

CDirectoryCache::tServerIter CDirectoryCache::GetServerEntry(CServer const& server)
{
	tServerIter iter;
	for (iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
		if (iter->server == server) {
			break;
		}
	}

	return iter;
}

void CDirectoryCache::UpdateLru(tServerIter const& sit, tCacheIter const& cit)
{
	tLruList::iterator* lruIt = (tLruList::iterator*)cit->lruIt;
	if (lruIt) {
		m_leastRecentlyUsedList.splice(m_leastRecentlyUsedList.end(), m_leastRecentlyUsedList, *lruIt);
		**lruIt = std::make_pair(sit, cit);
	}
	else {
		auto & entry = const_cast<CCacheEntry&>(*cit);
		entry.lruIt = (void*)new tLruList::iterator(m_leastRecentlyUsedList.emplace(m_leastRecentlyUsedList.end(), sit, cit));
	}
}

void CDirectoryCache::Prune()
{
	while ((m_leastRecentlyUsedList.size() > 50000) ||
		(m_totalFileCount > 1000000 && m_leastRecentlyUsedList.size() > 1000) ||
		(m_totalFileCount > 5000000 && m_leastRecentlyUsedList.size() > 100))
	{
		tFullEntryPosition pos = m_leastRecentlyUsedList.front();
		tLruList::iterator* lruIt = (tLruList::iterator*)pos.second->lruIt;
		delete lruIt;

		m_totalFileCount -= pos.second->listing.GetCount();

		pos.first->cacheList.erase(pos.second);
		if (pos.first->cacheList.empty()) {
			m_serverList.erase(pos.first);
		}

		m_leastRecentlyUsedList.pop_front();
	}
}

void CDirectoryCache::SetTtl(fz::duration const& ttl)
{
	if (ttl < fz::duration::from_seconds(30)) {
		ttl_ = fz::duration::from_seconds(30);
	}
	else if (ttl > fz::duration::from_days(1)) {
		ttl_ = fz::duration::from_days(1);
	}
	else {
		ttl_ = ttl;
	}
}
