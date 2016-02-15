#include <filezilla.h>

wxString CDirentry::dump() const
{
	wxString str = wxString::Format(_T("name=%s\nsize=%lld\npermissions=%s\nownerGroup=%s\ndir=%d\nlink=%d\ntarget=%s\nunsure=%d\n"),
				name, static_cast<long long>(size), *permissions, *ownerGroup, flags & flag_dir, flags & flag_link,
				target ? *target : std::wstring(), flags & flag_unsure);

	if( has_date() ) {
		str += _T("date=") + time.format(_T("%Y-%m-%d"), fz::datetime::local) + _T("\n");
	}
	if( has_time() ) {
		str += _T("time=") + time.format(_T("%H-%M-%S"), fz::datetime::local) + _T("\n");
	}
	return str;
}

bool CDirentry::operator==(const CDirentry &op) const
{
	if (name != op.name)
		return false;

	if (size != op.size)
		return false;

	if (permissions != op.permissions)
		return false;

	if (ownerGroup != op.ownerGroup)
		return false;

	if (flags != op.flags)
		return false;

	if( has_date() ) {
		if (time != op.time)
			return false;
	}

	return true;
}

void CDirectoryListing::SetCount(unsigned int count)
{
	if (count == m_entryCount)
		return;

	const unsigned int old_count = m_entryCount;

	if (!count)
	{
		m_entryCount = 0;
		return;
	}

	if (count < old_count)
	{
		m_searchmap_case.clear();
		m_searchmap_nocase.clear();
	}

	m_entries.get().resize(count);

	m_entryCount = count;
}

const CDirentry& CDirectoryListing::operator[](unsigned int index) const
{
	// Commented out, too heavy speed penalty
	// wxASSERT(index < m_entryCount);
	return *(*m_entries)[index];
}

CDirentry& CDirectoryListing::operator[](unsigned int index)
{
	// Commented out, too heavy speed penalty
	// wxASSERT(index < m_entryCount);
	return m_entries.get()[index].get();
}

void CDirectoryListing::Assign(std::deque<fz::shared_value<CDirentry>> &entries)
{
	m_entryCount = entries.size();

	std::vector<fz::shared_value<CDirentry> >& own_entries = m_entries.get();
	own_entries.clear();
	own_entries.reserve(m_entryCount);

	m_flags &= ~(listing_has_dirs | listing_has_perms | listing_has_usergroup);

	for (auto & entry : entries) {
		if (entry->is_dir())
			m_flags |= listing_has_dirs;
		if (!entry->permissions->empty())
			m_flags |= listing_has_perms;
		if (!entry->ownerGroup->empty())
			m_flags |= listing_has_usergroup;
		own_entries.emplace_back(std::move(entry));
	}

	m_searchmap_case.clear();
	m_searchmap_nocase.clear();
}

bool CDirectoryListing::RemoveEntry(unsigned int index)
{
	if (index >= GetCount())
		return false;

	m_searchmap_case.clear();
	m_searchmap_nocase.clear();

	std::vector<fz::shared_value<CDirentry> >& entries = m_entries.get();
	std::vector<fz::shared_value<CDirentry> >::iterator iter = entries.begin() + index;
	if ((*iter)->is_dir())
		m_flags |= CDirectoryListing::unsure_dir_removed;
	else
		m_flags |= CDirectoryListing::unsure_file_removed;
	entries.erase(iter);

	--m_entryCount;

	return true;
}

void CDirectoryListing::GetFilenames(std::vector<std::wstring> &names) const
{
	names.reserve(GetCount());
	for (unsigned int i = 0; i < GetCount(); ++i)
		names.push_back((*m_entries)[i]->name);
}

int CDirectoryListing::FindFile_CmpCase(const wxString& name) const
{
	if (!m_entryCount)
		return -1;

	if (!m_searchmap_case)
		m_searchmap_case.get();

	// Search map
	auto iter = m_searchmap_case->find(to_wstring(name));
	if (iter != m_searchmap_case->end())
		return iter->second;

	unsigned int i = m_searchmap_case->size();
	if (i == m_entryCount)
		return -1;

	auto & searchmap_case = m_searchmap_case.get();

	// Build map if not yet complete
	std::vector<fz::shared_value<CDirentry> >::const_iterator entry_iter = m_entries->begin() + i;
	for (; entry_iter != m_entries->end(); ++entry_iter, ++i) {
		std::wstring const& entry_name = (*entry_iter)->name;
		searchmap_case.insert(std::pair<std::wstring const, unsigned int>(entry_name, i));

		if (entry_name == name)
			return i;
	}

	// Map is complete, item not in it
	return -1;
}

int CDirectoryListing::FindFile_CmpNoCase(wxString name) const
{
	if (!m_entryCount)
		return -1;

	if (!m_searchmap_nocase)
		m_searchmap_nocase.get();

	name.MakeLower();

	// Search map
	auto iter = m_searchmap_nocase->find(to_wstring(name));
	if (iter != m_searchmap_nocase->end())
		return iter->second;

	unsigned int i = m_searchmap_nocase->size();
	if (i == m_entryCount)
		return -1;

	auto& searchmap_nocase = m_searchmap_nocase.get();

	// Build map if not yet complete
	std::vector<fz::shared_value<CDirentry> >::const_iterator entry_iter = m_entries->begin() + i;
	for (; entry_iter != m_entries->end(); ++entry_iter, ++i) {
		wxString entry_name = (*entry_iter)->name;
		entry_name.MakeLower();
		searchmap_nocase.insert(std::pair<std::wstring const, unsigned int>(to_wstring(entry_name), i));

		if (entry_name == name)
			return i;
	}

	// Map is complete, item not in it
	return -1;
}

void CDirectoryListing::ClearFindMap()
{
	if (!m_searchmap_case)
		return;

	m_searchmap_case.clear();
	m_searchmap_nocase.clear();
}
