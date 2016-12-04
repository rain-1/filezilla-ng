#include <filezilla.h>
#include "wrapengine.h"
#include "filezillaapp.h"
#include "ipcmutex.h"
#include "xmlfunctions.h"
#include "buildinfo.h"
#include "Options.h"

#include <libfilezilla/local_filesys.hpp>

#include <wx/statbox.h>
#include <wx/wizard.h>

#ifdef __WXGTK3__
#include <gtk/gtk.h>
#endif

#include <algorithm>

bool CWrapEngine::m_use_cache = true;

#define WRAPDEBUG 0

#if WRAPDEBUG
#define WRAPASSERT(x) wxASSERT(x)
#else
#define WRAPASSERT(x)
#endif

// Chinese equivalents to ".", "," and ":"
static const wxChar noWrapChars_Chinese[] = { '.', ',', ':', 0x3002, 0xFF0C, 0xFF1A, 0};
// Remark: Chinese (Taiwan) uses ascii punctuation marks though, but those
// don't have to be added, as only characters >= 128 will be wrapped.

bool CWrapEngine::CanWrapBefore(const wxChar& c)
{
	// Check if this is a punctuation character, we're not allowed
	// to wrap before such a character
	for (auto p = m_noWrapChars; p && *p; ++p) {
		if (*p == c) {
			return false;
		}
	}
	return true;
}

bool CWrapEngine::WrapTextChinese(wxWindow* parent, wxString &text, unsigned long maxLength)
{
	WRAPASSERT(text.Find(_T("  ")) == -1);
	WRAPASSERT(text.Find(_T(" \n")) == -1);
	WRAPASSERT(text.Find(_T("\n ")) == -1);
	WRAPASSERT(text.empty() || text.Last() != ' ');
	WRAPASSERT(text.empty() || text.Last() != '\n');

	// See comment at start of WrapText function what this function does
	wxString wrappedText;

	int width = 0, height = 0;

	const wxChar* str = text.c_str();
	// Scan entire string
	while (*str)
	{
		unsigned int lineLength = 0;

		const wxChar* p = str;

		// Position of last wrappable character
		const wxChar* wrappable = 0;

		bool lastAmpersand = false;
		while (*p)
		{
			if (*p == '&')
			{
				if (!lastAmpersand)
				{
					lastAmpersand = true;
					p++;
					continue;
				}
				else
					lastAmpersand = false;
			}
			std::map<wxChar, unsigned int>::const_iterator iter = m_charWidths.find(*p);
			if (iter == m_charWidths.end())
			{
				// Get width of all individual characters, record width of the current line
				parent->GetTextExtent(*p, &width, &height, 0, 0, &m_font);
				if ((unsigned int)width > maxLength)
					return false;
				m_charWidths[*p] = width;
				lineLength += width;
			}
			else
				lineLength += iter->second;

			WRAPASSERT(*p != '\r');
			if (*p == '\n')
			{
				// Wrap on newline
				wrappedText += wxString(str, p - str + 1);
				str = p + 1;
				break;
			}
			else if (p != str) // Don't wrap at first character
			{
				if (*p == ' ')
					// Remember position of last space
					wrappable = p;
				else if (*p >= 128)
				{
					if (CanWrapBefore(*p))
						wrappable = p;
				}
				else if (*(p - 1) >= 128 && CanWrapBefore(*p))
				{
					// Beginning of embedded English text, can wrap before
					wrappable = p;
				}
			}

			if (lineLength > maxLength && wrappable)
			{
				wxString tmp = wxString(str, wrappable - str);
				if (!tmp.empty() && tmp.Last() == ' ')
					tmp.RemoveLast();
				wrappedText += tmp + _T("\n");
				if (*wrappable != ' ')
					str = wrappable;
				else
					str = wrappable + 1;
				break;
			}

			p++;
		}
		if (!*p)
		{
			if (lineLength > maxLength)
			{
				if (!wrappable)
					return false;

				const wxString& tmp = wxString(str, wrappable - str);
				wrappedText += tmp + _T("\n");
				if (*wrappable != ' ')
					str = wrappable;
				else
					str = wrappable + 1;
			}
			wrappedText += str;
			break;
		}
	}

#if WRAPDEBUG
	wxString temp = wrappedText;
	wxASSERT(temp.Find(_T("  ")) == -1);
	wxASSERT(temp.Find(_T(" \n")) == -1);
	wxASSERT(temp.Find(_T("\n ")) == -1);
	wxASSERT(temp.empty() || temp.Last() != ' ');
	wxASSERT(temp.empty() || temp.Last() != '\n');
	temp.Replace(_T("&"), _T(""));
	while (!temp.empty())
	{
		wxString piece;
		int pos = temp.Find(_T("\n"));
		if (pos == -1)
		{
			piece = temp;
			temp = _T("");
		}
		else
		{
			piece = temp.Left(pos);
			temp = temp.Mid(pos + 1);
		}
		parent->GetTextExtent(piece, &width, &height, 0, 0, &m_font);
		wxASSERT(width <= (int)maxLength);
	}
#endif

	text = wrappedText;

	return true;
}

bool CWrapEngine::WrapText(wxWindow* parent, wxString& text, unsigned long maxLength)
{
	/*
	This function wraps the given string so that it's width in pixels does
	not exceed maxLength.
	In the general case, wrapping is done on word boundaries. Thus we scan the
	string for spaces, measuer the length of the words and wrap if line becomes
	too long.
	It has to be done wordwise, as with some languages/fonts, the width in
	pixels of a line is smaller than the sum of the widths of every character.

	A special case are some languages, e.g. Chinese, which don't separate words
	with spaces. In such languages it is allowed to break lines after any
	character.

	Though there are a few exceptions:
	- Don't wrap before punctuation marks
	- Wrap embedded English text fragments only on spaces

	For this kind of languages, a different wrapping algorithm is used.
	*/

	if (!m_font.IsOk())
		m_font = parent->GetFont();

#if WRAPDEBUG
	const wxString original = text;
#endif

	if (m_wrapOnEveryChar)
	{
		bool res = WrapTextChinese(parent, text, maxLength);
#if WRAPDEBUG
		wxString unwrapped = UnwrapText(text);
		wxASSERT(original == unwrapped);
#endif
		return res;
	}

	wxString wrappedText;

	int width = 0, height = 0;

	if (m_spaceWidth == -1) {
		parent->GetTextExtent(_T(" "), &m_spaceWidth, &height, 0, 0, &m_font);
	}

	int strLen = text.Length();
	int wrapAfter = -1;
	int start = 0;
	unsigned int lineLength = 0;

	bool url = false;
	bool containsURL = false;
	for (int i = 0; i <= strLen; i++) {
		if ((i < strLen - 2 && text[i] == ':' && text[i + 1] == '/' && text[i + 2] == '/') || // absolute
			(i < strLen && text[i] == '/' && (!i || text[i - 1] == ' '))) // relative
		{
			url = true;
			containsURL = true;
		}
		if (i < strLen && text[i] != ' ') {
			// If url, wrap on slashes and ampersands, but not first slash of something://
			if (!url ||
				 ((i < strLen - 1 && (text[i] != '/' || text[i + 1] == '/')) && (i < strLen - 1 && (text[i] != '&' || text[i + 1] == '&')) && text[i] != '?'))
			continue;
		}

		wxString segment;
		if (wrapAfter == -1) {
			if (i < strLen && (text[i] == '/' || text[i] == '?' || text[i] == '&'))
				segment = text.Mid(start, i - start + 1);
			else
				segment = text.Mid(start, i - start);
			wrapAfter = i;
		}
		else {
			if (i < strLen && (text[i] == '/' || text[i] == '?' || text[i] == '&'))
				segment = text.Mid(wrapAfter + 1, i - wrapAfter);
			else
				segment = text.Mid(wrapAfter + 1, i - wrapAfter - 1);
		}

		segment = wxStripMenuCodes(segment);
		parent->GetTextExtent(segment, &width, &height, 0, 0, &m_font);

		if (lineLength + m_spaceWidth + width > maxLength) {
			// Cannot be appended to current line without overflow, so start a new line
			if (!wrappedText.empty())
				wrappedText += _T("\n");
			wrappedText += text.Mid(start, wrapAfter - start);
			if (wrapAfter < strLen && text[wrapAfter] != ' ' && text[wrapAfter] != '\0')
				wrappedText += text[wrapAfter];

			if (width + m_spaceWidth >= (int)maxLength) {
				// Current segment too big to even fit into a line just by itself

				if( i != wrapAfter ) {
					if (!wrappedText.empty())
						wrappedText += _T("\n");
					wrappedText += text.Mid(wrapAfter + 1, i - wrapAfter - 1);
				}

				start = i + 1;
				wrapAfter = -1;
				lineLength = 0;
			}
			else {
				start = wrapAfter + 1;
				wrapAfter = i;
				lineLength = width;
			}
		}
		else if (lineLength + m_spaceWidth + width + m_spaceWidth >= maxLength) {
			if (!wrappedText.empty())
				wrappedText += _T("\n");
			wrappedText += text.Mid(start, i - start);
			if (i < strLen && text[i] != ' ' && text[i] != '\0')
				wrappedText += text[i];
			start = i + 1;
			wrapAfter = -1;
			lineLength = 0;
		}
		else {
			if (lineLength)
				lineLength += m_spaceWidth;
			lineLength += width;
			wrapAfter = i;
		}

		if (i < strLen && text[i] == ' ')
			url = false;
	}
	if (start < strLen) {
		if (!wrappedText.empty())
			wrappedText += _T("\n");
		wrappedText += text.Mid(start);
	}

	text = wrappedText;

#if WRAPDEBUG
		wxString unwrapped = UnwrapText(text);
		wxASSERT(original == unwrapped || containsURL);
#else
	(void)containsURL;
#endif
	return true;
}

bool CWrapEngine::WrapText(wxWindow* parent, int id, unsigned long maxLength)
{
	wxStaticText* pText = dynamic_cast<wxStaticText*>(parent->FindWindow(id));
	if (!pText)
		return false;

	wxString text = pText->GetLabel();
	if (!WrapText(parent, text, maxLength))
		return false;

	pText->SetLabel(text);

	return true;
}

#if WRAPDEBUG >= 3
	#define plvl { for (int i = 0; i < level; i++) printf(" "); }
#endif

int CWrapEngine::WrapRecursive(wxWindow* wnd, wxSizer* sizer, int max)
{
	// This function auto-wraps static texts.

#if WRAPDEBUG >= 3
	static int level = 1;
	plvl printf("Enter with max = %d, current = %d, sizer is %s\n", max, wnd ? wnd->GetRect().GetWidth() : -1, static_cast<char const*>(wxString(sizer->GetClassInfo()->GetClassName())));
#endif

	if (max <= 0)
	{
#if WRAPDEBUG >= 3
		plvl printf("Leave: max <= 0\n");
#endif
		return wrap_failed;
	}

	int result = 0;

	for (unsigned int i = 0; i < sizer->GetChildren().GetCount(); ++i) {
		wxSizerItem* item = sizer->GetItem(i);
		if (!item || !item->IsShown())
			continue;

		int rborder = 0;
		if (item->GetFlag() & wxRIGHT)
			rborder = item->GetBorder();
		int lborder = 0;
		if (item->GetFlag() & wxLEFT)
			lborder = item->GetBorder();

		wxRect rect = item->GetRect();

		wxSize min = item->GetMinSize();
		if (!min.IsFullySpecified())
			min = item->CalcMin();
		wxASSERT(min.GetWidth() + rborder + lborder <= sizer->GetMinSize().GetWidth());

		if (min.GetWidth() + item->GetPosition().x + lborder + rborder <= max)
			continue;

		wxWindow* window;
		wxSizer* subSizer = 0;
		if ((window = item->GetWindow())) {
			wxStaticText* text = dynamic_cast<wxStaticText*>(window);
			if (text) {
#ifdef __WXMAC__
				const int offset = 3;
#else
				const int offset = 2;
#endif
				if (max - rect.GetLeft() - rborder - offset <= 0)
					continue;

				wxString str = text->GetLabel();
				if (!WrapText(text, str, max - wxMax(0, rect.GetLeft()) - rborder - offset))
				{
#if WRAPDEBUG >= 3
					plvl printf("Leave: WrapText failed\n");
#endif
					return result | wrap_failed;
				}
				text->SetLabel(str);
				result |= wrap_didwrap;

#ifdef __WXGTK3__
				gtk_widget_set_size_request(text->GetHandle(), -1, -1);
				GtkRequisition req;
				gtk_widget_get_preferred_size(text->GetHandle(), 0, &req);
				text->CacheBestSize(wxSize(req.width, req.height));
#endif
				continue;
			}

			wxNotebook* book = dynamic_cast<wxNotebook*>(window);
			if (book) {
				int maxPageWidth = 0;
				for (unsigned int j = 0; j < book->GetPageCount(); ++j) {
					wxNotebookPage* page = book->GetPage(j);
					maxPageWidth = wxMax(maxPageWidth, page->GetRect().GetWidth());
				}

				for (unsigned int j = 0; j < book->GetPageCount(); ++j) {
					wxNotebookPage* page = book->GetPage(j);
					wxRect pageRect = page->GetRect();
					int pageMax = max - rect.GetLeft() - pageRect.GetLeft() - rborder - rect.GetWidth() + maxPageWidth;

					result |= WrapRecursive(wnd, page->GetSizer(), pageMax);
					if (result & wrap_failed) {
#if WRAPDEBUG >= 3
						plvl printf("Leave: WrapRecursive on notebook page failed\n");
#endif
						return result;
					}
				}
				continue;
			}

			if (dynamic_cast<wxCheckBox*>(window) || dynamic_cast<wxRadioButton*>(window) || dynamic_cast<wxChoice*>(window)) {
#if WRAPDEBUG >= 3
				plvl printf("Leave: WrapRecursive on unshrinkable control failed: %s\n",
					static_cast<char const*>(wxString(window->GetClassInfo()->GetClassName())));
#endif
				result |= wrap_failed;
				return result;
			}

			// We assume here that all other oversized controls can scale
		}
		else if ((subSizer = item->GetSizer()))
		{
			int subBorder = 0;
			wxWindow* subWnd = wnd;

			// Add border of static box sizer
			wxStaticBoxSizer* sboxSizer;
			if ((sboxSizer = dynamic_cast<wxStaticBoxSizer*>(subSizer))) {
				int top, other;
				sboxSizer->GetStaticBox()->GetBordersForSizer(&top, &other);
				subBorder += other * 2;
				subWnd = sboxSizer->GetStaticBox();
			}

#if WRAPDEBUG >= 3
			level++;
#endif
			result |= WrapRecursive(subWnd, subSizer, max - lborder - rborder - subBorder);
#if WRAPDEBUG >= 3
			level--;
#endif
			if (result & wrap_failed)
			{
#if WRAPDEBUG >= 3
				plvl printf("Leave: WrapRecursive on sizer failed\n");
#endif
				return result;
			}
		}
	}

	wxStaticBoxSizer* sboxSizer = dynamic_cast<wxStaticBoxSizer*>(sizer);
	if (sboxSizer) {
#ifdef __WXGTK3__
		gtk_widget_set_size_request(sboxSizer->GetStaticBox()->GetHandle(), -1, -1);
		GtkRequisition req;
		gtk_widget_get_preferred_size(sboxSizer->GetStaticBox()->GetHandle(), 0, &req);
		sboxSizer->GetStaticBox()->CacheBestSize(wxSize(req.width, req.height));
#elif defined(__WXMAC__) || defined(__WXGTK__)
		sboxSizer->GetStaticBox()->CacheBestSize(wxSize(0, 0));
#else
		sboxSizer->GetStaticBox()->CacheBestSize(wxDefaultSize);
#endif
	}

#if WRAPDEBUG >= 3
	plvl printf("Leave: Success, new min: %d\n", sizer->CalcMin().x);
#endif

	return result;
}

bool CWrapEngine::WrapRecursive(wxWindow* wnd, double ratio, const char* name /*=""*/, wxSize canvas /*=wxSize()*/, wxSize minRequestedSize /*wxSize()*/)
{
	std::vector<wxWindow*> windows;
	windows.push_back(wnd);
	return (WrapRecursive(windows, ratio, name, canvas, minRequestedSize) & wrap_failed) == 0;
}

void CWrapEngine::UnwrapRecursive_Wrapped(std::vector<int> const& wrapped, std::vector<wxWindow*> &windows, bool remove_fitting /*=false*/)
{
	unsigned int i = 0;
	for (auto const& wrap : wrapped) {
		UnwrapRecursive(windows[i], windows[i]->GetSizer());
		windows[i]->GetSizer()->Layout();

		if (!(wrap & wrap_didwrap) && !(wrap & wrap_failed)) {
			if (!wrap && remove_fitting) {
				// Page didn't need to be wrapped with current wrap offset,
				// remove it since desired width will only be larger in further wrappings.
				windows.erase(windows.begin() + i);
				continue;
			}
		}

		i++;
	}
}

bool CWrapEngine::WrapRecursive(std::vector<wxWindow*>& windows, double ratio, const char* name, wxSize canvas, wxSize const& minRequestedSize)
{
#ifdef __WXMAC__
	const int offset = 6;
#elif defined(__WXGTK__)
	const int offset = 0;
#else
	const int offset = 0;
#endif

	int maxWidth = GetWidthFromCache(name);
	if (maxWidth) {
		for (auto iter = windows.begin(); iter != windows.end(); ++iter) {
			wxSizer* pSizer = (*iter)->GetSizer();
			if (!pSizer)
				continue;

			pSizer->Layout();

#if WRAPDEBUG
			int res =
#endif
			WrapRecursive(*iter, pSizer, maxWidth - offset);
			WRAPASSERT(!(res & wrap_failed));
			pSizer->Layout();
			pSizer->Fit(*iter);
#if WRAPDEBUG
			wxSize size = pSizer->GetMinSize();
#endif
			WRAPASSERT(size.x <= maxWidth);
		}
		return true;
	}

	std::vector<wxWindow*> all_windows = windows;

	wxSize size = minRequestedSize;

	for (auto const& window : windows) {
		wxSizer* pSizer = window->GetSizer();
		if (!pSizer)
			return false;

		pSizer->Layout();
		size.IncTo(pSizer->GetMinSize());
	}

	double currentRatio = ((double)(size.GetWidth() + canvas.x) / (size.GetHeight() + canvas.y));
	if (ratio >= currentRatio) {
		// Nothing to do, can't wrap anything
		return true;
	}

	int max = size.GetWidth();
	int min = wxMin(size.GetWidth(), size.GetHeight());
	if (ratio < 0)
		min = (int)(min * ratio);
	if (min > canvas.x)
		min -= canvas.x;
	int desiredWidth = (min + max) / 2;
	int actualWidth = size.GetWidth();

	double bestRatioDiff = currentRatio - ratio;
	int bestWidth = max;

#if WRAPDEBUG > 0
	printf("Target ratio: %f\n", (float)ratio);
	printf("Canvas: % 4d % 4d\n", canvas.x, canvas.y);
	printf("Initial min and max: %d %d\n", min, max);
#endif

	for (;;) {
		std::vector<int> didwrap;

		size = minRequestedSize;
		int res = 0;
		for (auto const& window : windows) {
			wxSizer* pSizer = window->GetSizer();
			res = WrapRecursive(window, pSizer, desiredWidth - offset);
			if (res & wrap_didwrap)
				pSizer->Layout();
			didwrap.push_back(res);
			wxSize minSize = pSizer->GetMinSize();
			if (minSize.x > desiredWidth)
				res |= wrap_failed;
			size.IncTo(minSize);
			if (res & wrap_failed)
				break;
		}

#if WRAPDEBUG > 0
		printf("After current wrapping: size=%dx%d  desiredWidth=%d  min=%d  max=%d  res=%d\n", size.GetWidth(), size.GetHeight(), desiredWidth, min, max, res);
#endif
		if (size.GetWidth() > desiredWidth) {
			// Wrapping failed

			UnwrapRecursive_Wrapped(didwrap, windows, true);

			min = desiredWidth;
			if (max - min < 5)
				break;

			desiredWidth = (min + max) / 2;

#if WRAPDEBUG > 0
			printf("Wrapping failed, new min: %d\n", min);
#endif
			continue;
		}
		actualWidth = size.GetWidth();

		double newRatio = ((double)(size.GetWidth() + canvas.x) / (size.GetHeight() + canvas.y));
#if WRAPDEBUG > 0
		printf("New ratio: %f\n", (float)newRatio);
#endif

		if (newRatio < ratio) {
			UnwrapRecursive_Wrapped(didwrap, windows, true);

			if (ratio - newRatio < bestRatioDiff) {
				bestRatioDiff = ratio - newRatio;
				bestWidth = std::max(desiredWidth, actualWidth);
			}

			if (min >= actualWidth)
				min = desiredWidth;
			else
				min = actualWidth;
		}
		else if (newRatio > ratio) {
			UnwrapRecursive_Wrapped(didwrap, windows);
			if (newRatio - ratio < bestRatioDiff) {
				bestRatioDiff = newRatio - ratio;
				bestWidth = std::max(desiredWidth, actualWidth);
			}

			if (max == actualWidth)
				break;
			max = std::max(desiredWidth, actualWidth);
		}
		else {
			UnwrapRecursive_Wrapped(didwrap, windows);

			bestRatioDiff = ratio - newRatio;
			max = std::max(desiredWidth, actualWidth);
			break;
		}

		if (max - min < 2)
			break;
		desiredWidth = (min + max) / 2;
	}
#if WRAPDEBUG > 0
		printf("Performing final wrap with bestwidth %d\n", bestWidth);
#endif

	for (auto const& window : all_windows) {
		wxSizer *pSizer = window->GetSizer();

		int res = WrapRecursive(window, pSizer, bestWidth - offset);

		if (res & wrap_didwrap) {
			pSizer->Layout();
			pSizer->Fit(window);
		}
#if WRAPDEBUG
		size = pSizer->GetMinSize();
		WRAPASSERT(size.x <= bestWidth);
#endif
	}

	SetWidthToCache(name, bestWidth);

	return true;
}

wxString CWrapEngine::UnwrapText(const wxString& text)
{
	wxString unwrapped;
	int lang = wxGetApp().GetCurrentLanguage();
	if (lang == wxLANGUAGE_CHINESE || lang == wxLANGUAGE_CHINESE_SIMPLIFIED ||
		lang == wxLANGUAGE_CHINESE_TRADITIONAL || lang == wxLANGUAGE_CHINESE_HONGKONG ||
		lang == wxLANGUAGE_CHINESE_MACAU || lang == wxLANGUAGE_CHINESE_SINGAPORE ||
		lang == wxLANGUAGE_CHINESE_TAIWAN)
	{
		wxChar const* p = text.c_str();
		bool wasAscii = false;
		while (*p) {
			if (*p == '\n') {
				if (wasAscii)
					unwrapped += ' ';
				else if (*(p + 1) < 127)
				{
					if ((*(p + 1) != '(' || *(p + 2) != '&') && CanWrapBefore(*(p - 1)))
						unwrapped += ' ';
				}
			}
			else if (*p != '\r')
				unwrapped += *p;

			if (*p < 127)
				wasAscii = true;
			else
				wasAscii = false;

			p++;
		}
	}
	else
	{
		unwrapped = text;

		// Special handling for unwrapping of URLs
		int pos;
		while ( (pos = unwrapped.Find(_T("&&\n"))) > 0 )
		{
			if (unwrapped[pos - 1] == ' ')
				unwrapped = unwrapped.Left(pos + 2) + _T(" ") + unwrapped.Mid(pos + 3);
			else
				unwrapped = unwrapped.Left(pos + 2) + unwrapped.Mid(pos + 3);
		}

		unwrapped.Replace(_T("\n"), _T(" "));
		unwrapped.Replace(_T("\r"), _T(""));
	}
	return unwrapped;
}

bool CWrapEngine::UnwrapRecursive(wxWindow* wnd, wxSizer* sizer)
{
	for (unsigned int i = 0; i < sizer->GetChildren().GetCount(); i++)
	{
		wxSizerItem* item = sizer->GetItem(i);
		if (!item)
			continue;

		wxWindow* window;
		wxSizer* subSizer;
		if ((window = item->GetWindow())) {
			wxStaticText* text = dynamic_cast<wxStaticText*>(window);
			if (text) {
				wxString unwrapped = UnwrapText(text->GetLabel());
				text->SetLabel(unwrapped);

				continue;
			}

			wxNotebook* book = dynamic_cast<wxNotebook*>(window);
			if (book) {
				for (unsigned int j = 0; j < book->GetPageCount(); ++j) {
					wxNotebookPage* page = book->GetPage(j);
					UnwrapRecursive(wnd, page->GetSizer());
				}
				continue;
			}
		}
		else if ((subSizer = item->GetSizer()))
		{
			UnwrapRecursive(wnd, subSizer);
		}
	}

	return true;
}

int CWrapEngine::GetWidthFromCache(const char* name)
{
	if (!m_use_cache)
		return 0;

	if (!name || !*name)
		return 0;

	// We have to synchronize access to layout.xml so that multiple processes don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_LAYOUT);

	CXmlFile xml(wxGetApp().GetSettingsFile(_T("layout")));
	auto root = xml.Load(true);
	auto  element = root.child("Layout");
	if (!element) {
		return 0;
	}

	wxString language = wxGetApp().GetCurrentLanguageCode();
	if (language.empty())
		language = _T("default");

	auto xLanguage = FindElementWithAttribute(element, "Language", "id", language.mb_str());
	if (!xLanguage) {
		return 0;
	}

	auto dialog = FindElementWithAttribute(xLanguage, "Dialog", "name", name);
	if (!dialog) {
		return 0;
	}

	int value = GetAttributeInt(dialog, "width");

	return value;
}

void CWrapEngine::SetWidthToCache(const char* name, int width)
{
	if (!m_use_cache)
		return;

	if (!name || !*name)
		return;

	// We have to synchronize access to layout.xml so that multiple processes don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_LAYOUT);

	CXmlFile xml(wxGetApp().GetSettingsFile(_T("layout")));
	auto root = xml.Load(true);
	auto element = root.child("Layout");
	if (!element) {
		return;
	}

	wxString language = wxGetApp().GetCurrentLanguageCode();
	if (language.empty())
		language = _T("default");

	auto xLanguage = FindElementWithAttribute(element, "Language", "id", language.mb_str());
	if (!xLanguage) {
		return;
	}

	auto dialog = FindElementWithAttribute(xLanguage, "Dialog", "name", name);
	if (!dialog) {
		dialog = xLanguage.append_child("Dialog");
		SetTextAttribute(dialog, "name", name);
	}

	SetAttributeInt(dialog, "width", width);
	xml.Save(false);
}

CWrapEngine::CWrapEngine()
{
	CheckLanguage();
}

static wxString GetLocaleFile(const wxString& localesDir, wxString name)
{
	if (wxFileName::FileExists(localesDir + name + _T("/filezilla.mo")))
		return name;
	if (wxFileName::FileExists(localesDir + name + _T("/LC_MESSAGES/filezilla.mo")))
		return name + _T("/LC_MESSAGES");

	size_t pos = name.Find('@');
	if (pos > 0)
	{
		name = name.Left(pos);
		if (wxFileName::FileExists(localesDir + name + _T("/filezilla.mo")))
			return name;
		if (wxFileName::FileExists(localesDir + name + _T("/LC_MESSAGES/filezilla.mo")))
			return name + _T("/LC_MESSAGES");
	}

	pos = name.Find('_');
	if (pos > 0)
	{
		name = name.Left(pos);
		if (wxFileName::FileExists(localesDir + name + _T("/filezilla.mo")))
			return name;
		if (wxFileName::FileExists(localesDir + name + _T("/LC_MESSAGES/filezilla.mo")))
			return name + _T("/LC_MESSAGES");
	}

	return wxString();
}

bool CWrapEngine::LoadCache()
{
	// We have to synchronize access to layout.xml so that multiple processes don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_LAYOUT);

	CXmlFile xml(wxGetApp().GetSettingsFile(_T("layout")));
	auto document = xml.Load(true);

	if (!document) {
		m_use_cache = false;
		wxMessageBoxEx(xml.GetError(), _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	bool cacheValid = true;

	auto layoutElement = document.child("Layout");
	if (!layoutElement) {
		layoutElement = document.append_child("Layout");
	}

	std::wstring const buildDate = CBuildInfo::GetBuildDateString();
	if (GetTextAttribute(layoutElement, "Builddate") != buildDate) {
		cacheValid = false;
		SetTextAttribute(layoutElement, "Builddate", buildDate);
	}

	std::wstring const buildTime = CBuildInfo::GetBuildTimeString();
	if (GetTextAttribute(layoutElement, "Buildtime") != buildTime) {
		cacheValid = false;
		SetTextAttribute(layoutElement, "Buildtime", buildTime);
	}

	// Enumerate resource file names
	// -----------------------------

	auto resources = layoutElement.child("Resources");
	if (!resources) {
		resources = layoutElement.append_child("Resources");
	}

	CLocalPath resourceDir = wxGetApp().GetResourceDir();
	resourceDir.AddSegment(_T("xrc"));
	wxDir dir(resourceDir.GetPath());

	wxLogNull log;

	wxString xrc;
	for (bool found = dir.GetFirst(&xrc, _T("*.xrc")); found; found = dir.GetNext(&xrc)) {
		if (!wxFileName::FileExists(resourceDir.GetPath() + xrc)) {
			continue;
		}

		fz::datetime const date = fz::local_filesys::get_modification_time(fz::to_native(resourceDir.GetPath() + xrc));
		std::wstring const ticks = std::to_wstring(date.get_time_t());

		auto resourceElement = FindElementWithAttribute(resources, "xrc", "file", xrc.mb_str());
		if (!resourceElement) {
			resourceElement = resources.append_child("xrc");
			SetTextAttribute(resourceElement, "file", xrc.ToStdWstring());
			SetTextAttribute(resourceElement, "date", ticks);
			cacheValid = false;
		}
		else {
			std::wstring xrcNodeDate = GetTextAttribute(resourceElement, "date");
			if (xrcNodeDate.empty() || xrcNodeDate != ticks) {
				cacheValid = false;
				SetTextAttribute(resourceElement, "date", ticks);
			}
		}
	}

	if (!cacheValid) {
		// Clear all languages
		auto language = layoutElement.child("Language");
		while (language) {
			layoutElement.remove_child(language);
			language = layoutElement.child("Language");
		}
	}

	// Get current language
	std::wstring language = wxGetApp().GetCurrentLanguageCode().ToStdWstring();
	if (language.empty()) {
		language = L"default";
	}

	auto languageElement = FindElementWithAttribute(layoutElement, "Language", "id", fz::to_utf8(language).c_str());
	if (!languageElement) {
		languageElement = layoutElement.append_child("Language");
		SetTextAttribute(languageElement, "id", language);
	}

	// Get static text font and measure sample text
	wxFrame* pFrame = new wxFrame;
	pFrame->Create(0, -1, _T("Title"), wxDefaultPosition, wxDefaultSize, wxFRAME_TOOL_WINDOW);
	wxStaticText* pText = new wxStaticText(pFrame, -1, _T("foo"));

	wxFont font = pText->GetFont();
	std::wstring fontDesc = font.GetNativeFontInfoDesc().ToStdWstring();

	auto fontElement = languageElement.child("Font");
	if (!fontElement)
		fontElement = languageElement.append_child("Font");

	if (GetTextAttribute(fontElement, "font") != fontDesc) {
		SetTextAttribute(fontElement, "font", fontDesc);
		cacheValid = false;
	}

	int width, height;
	pText->GetTextExtent(_T("Just some test string we are measuring. If width or heigh differ from the recorded values, invalidate cache. 1234567890MMWWII"), &width, &height);

	if (GetAttributeInt(fontElement, "width") != width ||
		GetAttributeInt(fontElement, "height") != height)
	{
		cacheValid = false;
		SetAttributeInt(fontElement, "width", width);
		SetAttributeInt(fontElement, "height", height);
	}

	pFrame->Destroy();

	// Get language file
	CLocalPath const localesDir = wxGetApp().GetLocalesDir();
	wxString name = GetLocaleFile(localesDir.GetPath(), language);

	if (!name.empty()) {
		fz::datetime const date = fz::local_filesys::get_modification_time(fz::to_native(localesDir.GetPath() + name + _T("/filezilla.mo")));
		std::wstring const ticks = std::to_wstring(date.get_time_t());

		std::wstring languageNodeDate = GetTextAttribute(languageElement, "date");
		if (languageNodeDate.empty() || languageNodeDate != ticks) {
			SetTextAttribute(languageElement, "date", ticks);
			cacheValid = false;
		}
	}
	else
		SetTextAttribute(languageElement, "date", "");
	if (!cacheValid) {
		pugi::xml_node dialog;
		while ((dialog = languageElement.child("Dialog")))
			languageElement.remove_child(dialog);
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
		m_use_cache = cacheValid;
		return true;
	}

	if (!xml.Save(true)) {
		m_use_cache = false;
	}
	return true;
}

void CWrapEngine::ClearCache()
{
	// We have to synchronize access to layout.xml so that multiple processes don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_LAYOUT);
	wxFileName file(COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR), _T("layout.xml"));
	if (file.FileExists())
		wxRemoveFile(file.GetFullPath());
}

void CWrapEngine::CheckLanguage()
{
	int lang = wxGetApp().GetCurrentLanguage();
	if (lang == wxLANGUAGE_CHINESE || lang == wxLANGUAGE_CHINESE_SIMPLIFIED ||
		lang == wxLANGUAGE_CHINESE_TRADITIONAL || lang == wxLANGUAGE_CHINESE_HONGKONG ||
		lang == wxLANGUAGE_CHINESE_MACAU || lang == wxLANGUAGE_CHINESE_SINGAPORE ||
		lang == wxLANGUAGE_CHINESE_TAIWAN ||
		lang == wxLANGUAGE_JAPANESE)
	{
		m_wrapOnEveryChar = true;
		m_noWrapChars = noWrapChars_Chinese;
	}
	else {
		m_wrapOnEveryChar = false;
		m_noWrapChars = 0;
	}
}
