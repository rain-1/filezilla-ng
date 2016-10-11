#ifndef __FILELISTCTRL_H__
#define __FILELISTCTRL_H__

#include "listctrlex.h"
#include "systemimagelist.h"
#include "listingcomparison.h"

#include <cstring>
#include <memory>

class CQueueView;
class CFileListCtrl_SortComparisonObject;
class CFilelistStatusBar;
#if defined(__WXGTK__) && !defined(__WXGTK3__)
class CGtkEventCallbackProxyBase;
#endif

class CGenericFileData
{
public:
	std::wstring fileType;
	int icon{-2};

	// t_fileEntryFlags is defined in listingcomparison.h as it will be used for
	// both local and remote listings
	CComparableListing::t_fileEntryFlags comparison_flags{CComparableListing::normal};
};

class CFileListCtrlSortBase
{
public:
	enum DirSortMode
	{
		dirsort_ontop,
		dirsort_onbottom,
		dirsort_inline
	};
	enum NameSortMode
	{
		namesort_caseinsensitive,
		namesort_casesensitive,
		namesort_natural
	};

	virtual bool operator()(int a, int b) const = 0;
	virtual ~CFileListCtrlSortBase() {} // Without this empty destructor GCC complains

	#define CMP(f, data1, data2) \
		{\
			int res = this->f(data1, data2);\
			if (res < 0)\
				return true;\
			else if (res > 0)\
				return false;\
		}

	#define CMP_LESS(f, data1, data2) \
		{\
			int res = this->f(data1, data2);\
			if (res < 0)\
				return true;\
			else\
				return false;\
		}

	static int CmpCase(std::wstring const& str1, std::wstring const& str2)
	{
		return str1.compare(str2);
	}

	static int CmpCase(wxString const& str1, wxString const& str2)
	{
		return str1.Cmp(str2);
	}

	static int CmpNoCase(std::wstring const& str1, std::wstring const& str2)
	{
		int cmp = fz::stricmp(str1, str2);
		if (cmp)
			return cmp;
		return str1.compare(str2);
	}

	static int CmpNoCase(wxString const& str1, wxString const& str2)
	{
		int cmp = str1.CmpNoCase(str2);
		if (cmp)
			return cmp;
		return str1.Cmp(str2);
	}

	static int CmpNatural(wxString const& str1, wxString const& str2)
	{
		return CmpNatural(static_cast<wxChar const*>(str1.c_str()), static_cast<wxChar const*>(str2.c_str()));
	}

	static int CmpNatural(std::wstring const& str1, std::wstring const& str2)
	{
		return CmpNatural(str1.c_str(), str2.c_str());
	}

	static int CmpNatural(wchar_t const* p1, wchar_t const* p2)
	{
		int res = 0;
		int zeroCount = 0;
		bool isNumber = false;
		for (; *p1 && *p2; ++p1, ++p2) {
			int diff = static_cast<int>(wxTolower(*p1)) - static_cast<int>(wxTolower(*p2));
			if (isNumber) {
				if (res == 0)
					res = diff;
				int nbDigits = (wxIsdigit(*p1) ? 1 : 0) + (wxIsdigit(*p2) ? 1 : 0);
				if (nbDigits == 0 && res == 0) {
					if (zeroCount)
						break;
					isNumber = false;
				} else if (nbDigits != 2)
					break;
			} else if (wxIsdigit(*p1) && wxIsdigit(*p2)) {
				zeroCount = 0;
				for(; *p1 == '0' && *(p1+1) && wxIsdigit(*(p1+1)); ++p1)
					zeroCount++;
				for(; *p2 == '0' && *(p2+1) && wxIsdigit(*(p2+1)); ++p2)
					zeroCount--;
				res = *p1 - *p2;
				isNumber = true;
			} else if (diff)
				return diff;
		}

		if (res == 0 && isNumber)
			res = zeroCount;

		if (!*p1 && !*p2)
			return res;
		if (!isNumber || res == 0)
			 return !*p1 ? -1 : 1;
		if (*p1 && wxIsdigit(*p1))
			return 1;       //more digits
		if (*p2 && wxIsdigit(*p2))
			return -1;      //fewer digits
		return res;         //same length, compare first different digit in the sequence
	}

	typedef int (* CompareFunction)(std::wstring const&, std::wstring const&);
	static CompareFunction GetCmpFunction(NameSortMode mode)
	{
		switch (mode)
		{
		default:
		case CFileListCtrlSortBase::namesort_caseinsensitive:
			return &CFileListCtrlSortBase::CmpNoCase;
		case CFileListCtrlSortBase::namesort_casesensitive:
			return &CFileListCtrlSortBase::CmpCase;
		case CFileListCtrlSortBase::namesort_natural:
			return &CFileListCtrlSortBase::CmpNatural;
		}
	}
};

// Helper classes for fast sorting using std::sort
// -----------------------------------------------

template<typename value_type>
inline int DoCmpName(value_type const& data1, value_type const& data2, CFileListCtrlSortBase::NameSortMode const nameSortMode)
{
	switch (nameSortMode)
	{
	case CFileListCtrlSortBase::namesort_casesensitive:
		return CFileListCtrlSortBase::CmpCase(data1.name, data2.name);

	default:
	case CFileListCtrlSortBase::namesort_caseinsensitive:
		return CFileListCtrlSortBase::CmpNoCase(data1.name, data2.name);

	case CFileListCtrlSortBase::namesort_natural:
		return CFileListCtrlSortBase::CmpNatural(data1.name, data2.name);
	}
}

template<typename Listing>
class CFileListCtrlSort : public CFileListCtrlSortBase
{
public:
	typedef Listing List;
	typedef typename Listing::value_type value_type;

	CFileListCtrlSort(Listing const& listing, DirSortMode dirSortMode, NameSortMode nameSortMode)
		: m_listing(listing), m_dirSortMode(dirSortMode), m_nameSortMode(nameSortMode)
	{
	}

	inline int CmpDir(value_type const& data1, value_type const& data2) const
	{
		switch (m_dirSortMode)
		{
		default:
		case dirsort_ontop:
			if (data1.is_dir()) {
				if (!data2.is_dir())
					return -1;
				else
					return 0;
			}
			else {
				if (data2.is_dir())
					return 1;
				else
					return 0;
			}
		case dirsort_onbottom:
			if (data1.is_dir()) {
				if (!data2.is_dir())
					return 1;
				else
					return 0;
			}
			else {
				if (data2.is_dir())
					return -1;
				else
					return 0;
			}
		case dirsort_inline:
			return 0;
		}
	}

	template<typename value_type>
	inline int CmpName(value_type const& data1, value_type const& data2) const
	{
		return DoCmpName(data1, data2, m_nameSortMode);
	}

	inline int CmpSize(const value_type &data1, const value_type &data2) const
	{
		int64_t const diff = data1.size - data2.size;
		if (diff < 0)
			return -1;
		else if (diff > 0)
			return 1;
		else
			return 0;
	}

	inline int CmpStringNoCase(const wxString &data1, const wxString &data2) const
	{
		return data1.CmpNoCase(data2);
	}

	inline int CmpTime(const value_type &data1, const value_type &data2) const
	{
		if( data1.time < data2.time ) {
			return -1;
		}
		else if( data1.time > data2.time ) {
			return 1;
		}
		else {
			return 0;
		}
	}

protected:
	Listing const& m_listing;

	DirSortMode const m_dirSortMode;
	NameSortMode const m_nameSortMode;
};

template<class CFileData> class CFileListCtrl;

template<class T, typename DataEntry> class CReverseSort : public T
{
public:
	CReverseSort(typename T::List const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const pListView)
		: T(listing, fileData, dirSortMode, nameSortMode, pListView)
	{
	}

	inline bool operator()(int a, int b) const
	{
		return T::operator()(b, a);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortName : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortName(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortSize : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortSize(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpSize, data1, data2);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortType : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortType(Listing const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const pListView)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode), m_pListView(pListView), m_fileData(fileData)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		DataEntry &type1 = m_fileData[a];
		DataEntry &type2 = m_fileData[b];
		if (type1.fileType.empty())
			type1.fileType = m_pListView->GetType(data1.name, data1.is_dir()).ToStdWstring();
		if (type2.fileType.empty())
			type2.fileType = m_pListView->GetType(data2.name, data2.is_dir()).ToStdWstring();

		CMP(CmpStringNoCase, type1.fileType, type2.fileType);

		CMP_LESS(CmpName, data1, data2);
	}

protected:
	CFileListCtrl<DataEntry>* const m_pListView;
	std::vector<DataEntry>& m_fileData;
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortTime : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortTime(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpTime, data1, data2);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortPermissions : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortPermissions(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpStringNoCase, *data1.permissions, *data2.permissions);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortOwnerGroup : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortOwnerGroup(Listing const& listing, std::vector<DataEntry>&, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);

		CMP(CmpStringNoCase, *data1.ownerGroup, *data2.ownerGroup);

		CMP_LESS(CmpName, data1, data2);
	}
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortPath : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortPath(Listing const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
		, m_fileData(fileData)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		if (data1.path < data2.path)
			return true;
		if (data1.path != data2.path)
			return false;

		CMP_LESS(CmpName, data1, data2);
	}
	std::vector<DataEntry>& m_fileData;
};

template<typename Listing, typename DataEntry>
class CFileListCtrlSortNamePath : public CFileListCtrlSort<Listing>
{
public:
	CFileListCtrlSortNamePath(Listing const& listing, std::vector<DataEntry>& fileData, CFileListCtrlSortBase::DirSortMode dirSortMode, CFileListCtrlSortBase::NameSortMode nameSortMode, CFileListCtrl<DataEntry>* const)
		: CFileListCtrlSort<Listing>(listing, dirSortMode, nameSortMode)
		, m_fileData(fileData)
	{
	}

	bool operator()(int a, int b) const
	{
		typename Listing::value_type const& data1 = this->m_listing[a];
		typename Listing::value_type const& data2 = this->m_listing[b];

		CMP(CmpDir, data1, data2);
		CMP(CmpName, data1, data2);

		if (data1.path < data2.path)
			return true;
		if (data1.path != data2.path)
			return false;

		CMP_LESS(CmpName, data1, data2);
	}
	std::vector<DataEntry>& m_fileData;
};

namespace genericTypes {
	enum type {
		file,
		directory
	};
}

template<class CFileData> class CFileListCtrl : public wxListCtrlEx, public CComparableListing
{
	template<typename Listing, typename DataEntry> friend class CFileListCtrlSortType;
public:
	CFileListCtrl(wxWindow* pParent, CQueueView *pQueue, bool border = false);
	virtual ~CFileListCtrl();

	class CSortComparisonObject : public std::binary_function<int,int,bool>
	{
	public:
		CSortComparisonObject(CFileListCtrlSortBase* pObject)
			: m_pObject(pObject)
		{
		}

		void Destroy()
		{
			delete m_pObject;
		}

		inline bool operator()(int a, int b)
		{
			return m_pObject->operator ()(a, b);
		}
	protected:
		CFileListCtrlSortBase* m_pObject;
	};

	void SetFilelistStatusBar(CFilelistStatusBar* pFilelistStatusBar) { m_pFilelistStatusBar = pFilelistStatusBar; }
	CFilelistStatusBar* GetFilelistStatusBar() { return m_pFilelistStatusBar; }

	void ClearSelection();

	virtual void OnNavigationEvent(bool) {}

	std::vector<unsigned int> const& indexMapping() const { return m_indexMapping; }

protected:
	CQueueView *m_pQueue;

	std::vector<CFileData> m_fileData;
	std::vector<unsigned int> m_indexMapping;
	std::vector<unsigned int> m_originalIndexMapping; // m_originalIndexMapping will only be set on comparisons

	virtual bool ItemIsDir(int index) const = 0;
	virtual int64_t ItemGetSize(int index) const = 0;

	std::map<wxString, wxString> m_fileTypeMap;

	// The .. item
	bool m_hasParent;

	int m_sortColumn{-1};
	int m_sortDirection{};

	void InitSort(int optionID); // Has to be called after initializing columns
	void SortList(int column = -1, int direction = -1, bool updateSelections = true);
	CFileListCtrlSortBase::DirSortMode GetDirSortMode();
	CFileListCtrlSortBase::NameSortMode GetNameSortMode();
	virtual CSortComparisonObject GetSortComparisonObject() = 0;

	// An empty path denotes a virtual file
	wxString GetType(wxString name, bool dir, const wxString& path = _T(""));

	// Comparison related
	virtual void ScrollTopItem(int item);
	virtual void OnPostScroll();
	virtual void OnExitComparisonMode();
	virtual void CompareAddFile(t_fileEntryFlags flags);

	int m_comparisonIndex;

	// Remembers which non-fill items are selected if enabling/disabling comparison.
	// Exploit fact that sort order doesn't change -> O(n)
	void ComparisonRememberSelections();
	void ComparisonRestoreSelections();
	std::deque<int> m_comparisonSelections;

	CFilelistStatusBar* m_pFilelistStatusBar;

	// Indexes of the items added, sorted ascending.
	void UpdateSelections_ItemsAdded(std::vector<int> const& added_indexes);

#ifndef __WXMSW__
	// Generic wxListCtrl does not support wxLIST_STATE_DROPHILITED, emulate it
	wxListItemAttr m_dropHighlightAttribute;
#endif

	void SetSelection(int item, bool select);
#ifndef __WXMSW__
	// Used by selection tracking
	void SetItemCount(int count);
#endif

#ifdef __WXMSW__
	virtual int GetOverlayIndex(int) { return 0; }
#endif

private:
	void UpdateSelections(int min, int max);

	void SortList_UpdateSelections(bool* selections, int focused_item, unsigned int focused_index);

	// If this is set to true, don't process selection changed events
	bool m_insideSetSelection;

#ifdef __WXMSW__
	virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam);
	virtual bool MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM *result);
#else
	int m_focusItem;
	std::vector<bool> m_selections;
	int m_pending_focus_processing;
#endif

#if defined(__WXGTK__) && !defined(__WXGTK3__)
	std::unique_ptr<CGtkEventCallbackProxyBase> m_gtkEventCallbackProxy;
#endif

	wxString m_genericTypes[2];

	DECLARE_EVENT_TABLE()
	void OnColumnClicked(wxListEvent &event);
	void OnColumnRightClicked(wxListEvent& event);
	void OnItemSelected(wxListEvent& event);
	void OnItemDeselected(wxListEvent& event);
#ifndef __WXMSW__
	void OnFocusChanged(wxListEvent& event);
	void OnProcessFocusChange(wxCommandEvent& event);
	void OnLeftDown(wxMouseEvent& event);
	void OnProcessMouseEvent(wxCommandEvent& event);
#endif
	void OnKeyDown(wxKeyEvent& event);
};

#ifdef FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION
#include "filelistctrl.cpp"
#endif

#endif //__FILELISTCTRL_H__
