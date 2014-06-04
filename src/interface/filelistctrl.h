#ifndef __FILELISTCTRL_H__
#define __FILELISTCTRL_H__

#include "listctrlex.h"
#include "systemimagelist.h"
#include "listingcomparison.h"

class CQueueView;
class CFileListCtrl_SortComparisonObject;
class CState;
class CFilelistStatusBar;
#ifdef __WXGTK__
class CGtkEventCallbackProxyBase;
#endif

class CGenericFileData
{
public:
	int icon;
	wxString fileType;

	// t_fileEntryFlags is defined in listingcomparison.h as it will be used for
	// both local and remote listings
	CComparableListing::t_fileEntryFlags flags;
};

class CListViewSort
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
	virtual ~CListViewSort() {} // Without this empty destructor GCC complains

	static int CmpCase(const wxString& str1, const wxString& str2)
	{
		return str1.Cmp(str2);
	}

	static int CmpNoCase(const wxString& str1, const wxString& str2)
	{
		int cmp = str1.CmpNoCase(str2);
		if (cmp)
			return cmp;
		return str1.Cmp(str2);
	}

	static int CmpNatural(const wxString& str1, const wxString& str2)
	{
		wxString::const_iterator p1 = str1.begin();
		wxString::const_iterator p2 = str2.begin();

		int res = 0;
		int zeroCount = 0;
		bool isNumber = false;
		for (; p1 != str1.end() && p2 != str2.end(); ++p1, ++p2) {
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
				for(; *p1 == '0' && p1+1 != str1.end() && wxIsdigit(*(p1+1)); ++p1)
					zeroCount++;
				for(; *p2 == '0' && p2+1 != str2.end() && wxIsdigit(*(p2+1)); ++p2)
					zeroCount--;
				res = *p1 - *p2;
				isNumber = true;
			} else if (diff)
				return diff;
		}

		if (res == 0 && isNumber)
			res = zeroCount;

		if (p1 == str1.end() && p2 == str2.end())
			return res;
		if (!isNumber || res == 0)
			 return p1 == str1.end() ? -1 : 1;
		if (p1 != str1.end() && wxIsdigit(*p1))
			return 1;       //more digits
		if (p2 != str2.end() && wxIsdigit(*p2))
			return -1;      //fewer digits
		return res;         //same length, compare first different digit in the sequence
	}

	typedef int (* CompareFunction)(const wxString&, const wxString&);
	static CompareFunction GetCmpFunction(NameSortMode mode)
	{
		switch (mode)
		{
		default:
		case CListViewSort::namesort_caseinsensitive:
			return &CListViewSort::CmpNoCase;
		case CListViewSort::namesort_casesensitive:
			return &CListViewSort::CmpCase;
		case CListViewSort::namesort_natural:
			return &CListViewSort::CmpNatural;
		}
	}
};

namespace genericTypes {
	enum type {
		file,
		directory
	};
}

template<class CFileData> class CFileListCtrl : public wxListCtrlEx, public CComparableListing
{
public:
	CFileListCtrl(wxWindow* pParent, CState *pState, CQueueView *pQueue, bool border = false);
	virtual ~CFileListCtrl();

	class CSortComparisonObject : public std::binary_function<int,int,bool>
	{
	public:
		CSortComparisonObject(CListViewSort* pObject)
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
		CListViewSort* m_pObject;
	};

	void SetFilelistStatusBar(CFilelistStatusBar* pFilelistStatusBar) { m_pFilelistStatusBar = pFilelistStatusBar; }
	CFilelistStatusBar* GetFilelistStatusBar() { return m_pFilelistStatusBar; }

	void ClearSelection();

	virtual void OnNavigationEvent(bool) {}

protected:
	CQueueView *m_pQueue;

	std::vector<CFileData> m_fileData;
	std::vector<unsigned int> m_indexMapping;
	std::vector<unsigned int> m_originalIndexMapping; // m_originalIndexMapping will only be set on comparisons

	virtual bool ItemIsDir(int index) const = 0;
	virtual wxLongLong ItemGetSize(int index) const = 0;

	std::map<wxString, wxString> m_fileTypeMap;

	// The .. item
	bool m_hasParent;

	int m_sortColumn;
	int m_sortDirection;

	void InitSort(int optionID); // Has to be called after initializing columns
	void SortList(int column = -1, int direction = -1, bool updateSelections = true);
	enum CListViewSort::DirSortMode GetDirSortMode();
	enum CListViewSort::NameSortMode GetNameSortMode();
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
	std::list<int> m_comparisonSelections;

	CFilelistStatusBar* m_pFilelistStatusBar;

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
	virtual int GetOverlayIndex(int item) { return 0; }
#endif

private:
	void SortList_UpdateSelections(bool* selections, int focus);

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

#ifdef __WXGTK__
	CSharedPointer<CGtkEventCallbackProxyBase> m_gtkEventCallbackProxy;
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
