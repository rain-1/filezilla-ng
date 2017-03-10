#ifndef FILEZILLA_ENGINE_DIRECTORYLISTING_HEADER
#define FILEZILLA_ENGINE_DIRECTORYLISTING_HEADER

#include <libfilezilla/optional.hpp>
#include <libfilezilla/shared.hpp>
#include <libfilezilla/time.hpp>

#include <unordered_map>

class CDirentry
{
public:
	std::wstring name;
	int64_t size;
	fz::shared_value<std::wstring> permissions;
	fz::shared_value<std::wstring> ownerGroup;

	enum _flags
	{
		flag_dir = 1,
		flag_link = 2,
		flag_unsure = 4 // May be set on cached items if any changes were made to the file
	};
	int flags;

	inline bool is_dir() const
	{
		return (flags & flag_dir) != 0;
	}

	inline bool is_link() const
	{
		return (flags & flag_link) != 0;
	}

	inline bool is_unsure() const
	{
		return (flags & flag_unsure) != 0;
	}

	inline bool has_date() const
	{
		return !time.empty();
	}

	inline bool has_time() const
	{
		return !time.empty() && time.get_accuracy() >= fz::datetime::hours;
	}

	inline bool has_seconds() const
	{
		return !time.empty() && time.get_accuracy() >= fz::datetime::seconds;
	}

	fz::sparse_optional<std::wstring> target; // Set to linktarget it link is true

	fz::datetime time;

	std::wstring dump() const;
	bool operator==(const CDirentry &op) const;
};

class CDirectoryListing final
{
public:
	typedef CDirentry value_type;

	CDirectoryListing() = default;
	CDirectoryListing(CDirectoryListing const& listing) = default;
	CDirectoryListing(CDirectoryListing && listing) noexcept = default;

	CDirectoryListing& operator=(CDirectoryListing const&) = default;
	CDirectoryListing& operator=(CDirectoryListing &&) noexcept = default;

	CDirentry const& operator[](unsigned int index) const;

	// Word of caution: You MUST NOT change the name of the returned
	// entry if you do not call ClearFindMap afterwards
	CDirentry& get(unsigned int index);

	unsigned int GetCount() const { return m_entries ? m_entries->size() : 0; }

	void Append(CDirentry&& entry);

	int FindFile_CmpCase(std::wstring const& name) const;
	int FindFile_CmpNoCase(std::wstring const& name) const;

	void ClearFindMap();

	CServerPath path;
	fz::monotonic_clock m_firstListTime;

	enum
	{
		unsure_file_added = 0x01,
		unsure_file_removed = 0x02,
		unsure_file_changed = 0x04,
		unsure_file_mask = 0x07,
		unsure_dir_added = 0x08,
		unsure_dir_removed = 0x10,
		unsure_dir_changed = 0x20,
		unsure_dir_mask = 0x38,
		unsure_unknown = 0x40,
		unsure_invalid = 0x80, // Recommended action: Do a full refresh
		unsure_mask = 0xff,

		listing_failed = 0x100,
		listing_has_dirs = 0x200,
		listing_has_perms = 0x400,
		listing_has_usergroup = 0x800
	};
	// Lowest bit indicates a file got added
	// Next bit indicates a file got removed
	// 3rd bit indicates a file got changed.
	// 4th bit is set if an update cannot be applied to
	// one of the other categories.
	//
	// These bits should help the user interface to choose an appropriate sorting
	// algorithm for modified listings
	int m_flags{};

	int get_unsure_flags() const { return m_flags & unsure_mask; }
	bool failed() const { return (m_flags & listing_failed) != 0; }
	bool has_dirs() const { return (m_flags & listing_has_dirs) != 0; }
	bool has_perms() const { return (m_flags & listing_has_perms) != 0; }
	bool has_usergroup() const { return (m_flags & listing_has_usergroup) != 0; }

	void Assign(std::deque<fz::shared_value<CDirentry>> & entries);

	bool RemoveEntry(unsigned int index);

	void GetFilenames(std::vector<std::wstring> &names) const;

protected:

	fz::shared_optional<std::vector<fz::shared_value<CDirentry> > > m_entries;

	mutable fz::shared_optional<std::unordered_multimap<std::wstring, unsigned int> > m_searchmap_case;
	mutable fz::shared_optional<std::unordered_multimap<std::wstring, unsigned int> > m_searchmap_nocase;
};

// Checks if listing2 is a subset of listing1. Compares only filenames.
bool CheckInclusion(CDirectoryListing const& listing1, CDirectoryListing const& listing2);

#endif
