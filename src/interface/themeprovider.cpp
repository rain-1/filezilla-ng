#include <filezilla.h>
#include "themeprovider.h"
#include "filezillaapp.h"
#include "Options.h"
#include "xmlfunctions.h"

#include <wx/animate.h>

#include <libfilezilla/format.hpp>
#include <libfilezilla/local_filesys.hpp>

#include <utility>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

static CThemeProvider* instance = 0;

wxSize CTheme::StringToSize(std::wstring const& str)
{
	wxSize ret;

	size_t start = str.find_last_of(L"/\\");
	if (start == std::wstring::npos) {
		start = 0;
	}
	else {
		++start;
	}

	size_t pos = str.find('x', start);
	if (pos != std::wstring::npos && pos != str.size() - 1) {
		ret.SetWidth(fz::to_integral<int>(str.substr(start, pos - start), -1));
		ret.SetHeight(fz::to_integral<int>(str.substr(pos + 1), -1));
	}

	return ret;
}

bool CTheme::Load(std::wstring const& theme)
{
	theme_ = theme;
	path_ = wxGetApp().GetResourceDir().GetPath() + theme;
	if (!theme.empty()) {
		path_ += L"/";
	}

	CXmlFile xml(path_ + L"theme.xml");
	auto xtheme = xml.Load().child("Theme");
	if (!xtheme) {
		return false;
	}

	name_ = GetTextElement(xtheme, "Name");
	author_ = GetTextElement(xtheme, "Author");
	mail_ = GetTextElement(xtheme, "Mail");

	for (auto xSize = xtheme.child("size"); xSize; xSize = xSize.next_sibling("size")) {
		wxSize size = StringToSize(GetTextElement(xSize));
		if (size.x > 0 && size.y > 0) {
			bool primary = std::string(xSize.attribute("primary").value()) == "1";
			sizes_[size] = primary;
		}
	}

	timestamp_ = fz::local_filesys::get_modification_time(fz::to_native(path_) + fzT("theme.xml"));

	return !sizes_.empty();
}

bool CTheme::Load(std::wstring const& theme, std::vector<wxSize> sizes)
{
	path_ = wxGetApp().GetResourceDir().GetPath() + theme;
	if (!theme.empty()) {
		path_ += L"/";
	}

	for (auto const& size : sizes) {
		sizes_[size] = false;
	}
	return !sizes_.empty();
}

wxBitmap const& CTheme::LoadBitmap(std::wstring const& name, wxSize const& size)
{
	// First, check for name in cache
	auto it = cache_.find(name);
	if (it == cache_.end()) {
		it = cache_.insert(std::make_pair(name, cacheEntry())).first;
	}
	else {
		if (it->second.bitmaps_.empty()) {
			// The name is known but the icon does not exist
			static wxBitmap empty;
			return empty;
		}
	}

	// Look for correct size
	auto & sizeCache = it->second.bitmaps_;
	auto sit = sizeCache.find(size);
	if (sit != sizeCache.end()) {
		return sit->second;
	}

	return DoLoadBitmap(name, size, it->second);
}

wxBitmap const& CTheme::DoLoadBitmap(std::wstring const& name, wxSize const& size, cacheEntry & cache)
{
	// Go through all the theme sizes and look for the file we need

	wxSize pivotSize = size;
#ifdef __WXMAC__
	if (wxGetApp().GetTopWindow()) {
		double scale = wxGetApp().GetTopWindow()->GetContentScaleFactor();
		pivotSize.Scale(scale, scale);
	}
#endif
	// First look equal or larger icon
	auto const pivot = sizes_.lower_bound(pivotSize);
	for (auto pit = pivot; pit != sizes_.end(); ++pit) {
		wxBitmap const& bmp = LoadBitmapWithSpecificSizeAndScale(name, pit->first, size, cache);
		if (bmp.IsOk()) {
			return bmp;
		}
	}

	// Now look smaller icons
	for (auto pit = decltype(sizes_)::reverse_iterator(pivot); pit != sizes_.rend(); ++pit) {
		wxBitmap const& bmp = LoadBitmapWithSpecificSizeAndScale(name, pit->first, size, cache);
		if (bmp.IsOk()) {
			return bmp;
		}
	}

	// Out of luck.
	static wxBitmap empty;
	return empty;
}

wxBitmap const& CTheme::LoadBitmapWithSpecificSizeAndScale(std::wstring const& name, wxSize const& size, wxSize const& scale, cacheEntry & cache)
{
	std::wstring const file = path_ + fz::sprintf(L"%dx%d/%s.png", size.x, size.y, name);
	std::wstring cacheFile;
	CLocalPath cacheDir;
#ifndef __WXMAC__
	if (size != scale && !timestamp_.empty()) {
		// Scaling is expensive. Look into resource cache.
		cacheDir = COptions::Get()->GetCacheDirectory();
		if (!cacheDir.empty()) {
			cacheFile = cacheDir.GetPath() + fz::sprintf(L"%s_%s%dx%d.png", theme_, name, scale.x, scale.y);
			auto cacheTime = fz::local_filesys::get_modification_time(fz::to_native(cacheFile));
			if (!cacheTime.empty() && timestamp_ <= cacheTime) {
				wxBitmap bmp(cacheFile, wxBITMAP_TYPE_PNG);
				if (bmp.IsOk() && bmp.GetScaledSize() == scale) {
					auto inserted = cache.bitmaps_.insert(std::make_pair(scale, bmp));
					return inserted.first->second;
				}
			}
		}
	}
#endif
	wxImage image = LoadImageWithSpecificSize(file, size, cache);
	if (!image.IsOk()) {
		static wxBitmap empty;
		return empty;
	}

	// need to scale
#ifdef __WXMAC__
	double factor = static_cast<double>(image.GetSize().x) / scale.x;
	auto inserted = cache.bitmaps_.insert(std::make_pair(scale, wxBitmap(image, -1, factor)));
#else
	if (image.GetSize() != scale) {
		image.Rescale(scale.x, scale.y, wxIMAGE_QUALITY_HIGH);

		// Save in cache
		if (!cacheFile.empty()) {
			if (cacheDir.Create()) {
				image.SaveFile(cacheFile, wxBITMAP_TYPE_PNG);
			}
		}
	}

	auto inserted = cache.bitmaps_.insert(std::make_pair(scale, wxBitmap(image)));
#endif
	return inserted.first->second;
}

wxImage const& CTheme::LoadImageWithSpecificSize(std::wstring const& file, wxSize const& size, cacheEntry & cache)
{
	auto it = cache.images_.find(size);
	if (it != cache.images_.end()) {
		return it->second;
	}

	wxImage img(file, wxBITMAP_TYPE_PNG);

	if (img.Ok()) {
		if (img.HasMask() && !img.HasAlpha()) {
			img.InitAlpha();
		}
		if (img.GetSize() != size) {
			img.Rescale(size.x, size.y, wxIMAGE_QUALITY_HIGH);
		}
	}
	auto inserted = cache.images_.insert(std::make_pair(size, img));
	return inserted.first->second;
}

std::vector<wxBitmap> CTheme::GetAllImages(wxSize const& size)
{
	wxLogNull null;

	std::vector<wxBitmap> ret;

	for (auto const& psize : sizes_) {
		if (psize.second) {
			fz::local_filesys fs;
			if (fs.begin_find_files(fz::to_native(path_) + fz::sprintf(fzT("%dx%d/"), psize.first.x, psize.first.y))) {
				fz::native_string name;
				while (fs.get_next_file(name)) {
					size_t pos = name.find(fzT(".png"));
					if (pos != fz::native_string::npos && pos != 0) {
						wxBitmap const& bmp = LoadBitmap(fz::to_wstring(name.substr(0, pos)), size);
						if (bmp.IsOk()) {
							ret.emplace_back(bmp);
						}
					}
				}
			}
		}
		if (!ret.empty()) {
			break;
		}
	}

	return ret;
}

wxAnimation CTheme::LoadAnimation(std::wstring const& name, wxSize const& size)
{
	std::wstring path = path_ + fz::sprintf(L"%dx%d/%s.gif", size.x, size.y, name);

	return wxAnimation(path);
}

CThemeProvider::CThemeProvider()
{
	wxArtProvider::Push(this);

	CTheme unthemed;
	if (unthemed.Load(std::wstring(), { wxSize(16,16), wxSize(20,20), wxSize(22,22), wxSize(24,24), wxSize(32,32), wxSize(48,48), wxSize(480,480) })) {
		themes_[L""] = unthemed;
	}

	CTheme defaultTheme;
	if (defaultTheme.Load(L"default")) {
		themes_[L"default"] = defaultTheme;
	}

	std::wstring name = COptions::Get()->GetOption(OPTION_ICONS_THEME);
	if (name != L"default") {
		CTheme theme;
		if (theme.Load(name)) {
			themes_[name] = theme;
		}
	}

	RegisterOption(OPTION_ICONS_THEME);
	RegisterOption(OPTION_ICONS_SCALE);

	if (!instance) {
		instance = this;
	}
}

CThemeProvider::~CThemeProvider()
{
	if (instance == this) {
		instance = 0;
	}
}

CThemeProvider* CThemeProvider::Get()
{
	return instance;
}

wxBitmap CThemeProvider::CreateBitmap(wxArtID const& id, wxArtClient const& client, wxSize const& size)
{
	if (id.Left(4) != _T("ART_")) {
		return wxNullBitmap;
	}
	wxASSERT(size.GetWidth() == size.GetHeight());

	wxSize newSize;
	if (size.x <= 0 || size.y <= 0) {
		newSize = GetNativeSizeHint(client);
		if (newSize.x <= 0 || newSize.y <= 0) {
			newSize = GetIconSize(iconSizeSmall);
		}
	}
	else {
		newSize = size;
	}

	// The ART_* IDs are always given in uppercase ASCII,
	// all filenames used by FileZilla for the resources
	// are lowercase ASCII. Locale-independent transformation
	// needed e.g. if using Turkish locale.
	std::wstring name = fz::str_tolower_ascii(id.substr(4).ToStdWstring());

	wxBitmap const* bmp{&wxNullBitmap};
	auto tryTheme = [&](std::wstring const& theme) {
		if (!bmp->IsOk()) {
			auto it = themes_.find(theme);
			if (it != themes_.end()) {
				bmp = &it->second.LoadBitmap(name, newSize);
			}
		}
	};

	wxLogNull logNull;

	std::wstring const theme = COptions::Get()->GetOption(OPTION_ICONS_THEME);
	if (!theme.empty() && theme != L"default") {
		tryTheme(theme);
	}
	tryTheme(L"default");
	tryTheme(L"");

	return *bmp;
}

wxAnimation CThemeProvider::CreateAnimation(wxArtID const& id, wxSize const& size)
{
	if (id.Left(4) != _T("ART_")) {
		return wxAnimation();
	}
	wxASSERT(size.GetWidth() == size.GetHeight());

	// The ART_* IDs are always given in uppercase ASCII,
	// all filenames used by FileZilla for the resources
	// are lowercase ASCII. Locale-independent transformation
	// needed e.g. if using Turkish locale.
	std::wstring name = fz::str_tolower_ascii(id.Mid(4).ToStdWstring());

	wxAnimation anim;
	auto tryTheme = [&](std::wstring const& theme) {
		if (!anim.IsOk()) {
			auto it = themes_.find(theme);
			if (it != themes_.end()) {
				anim = it->second.LoadAnimation(name, size);
			}
		}
	};

	wxLogNull logNull;

	std::wstring const theme = COptions::Get()->GetOption(OPTION_ICONS_THEME);
	if (!theme.empty() && theme != L"default") {
		tryTheme(theme);
	}
	tryTheme(L"default");
	tryTheme(L"");

	return anim;
}

std::vector<std::wstring> CThemeProvider::GetThemes()
{
	std::vector<std::wstring> themes;

	CLocalPath const resourceDir = wxGetApp().GetResourceDir();

	fz::local_filesys fs;

	fz::native_string path = fz::to_native(resourceDir.GetPath());
	if (fs.begin_find_files(path, true)) {
		fz::native_string name;
		while (fs.get_next_file(name)) {
			if (fz::local_filesys::get_file_type(path + name + fzT("/theme.xml")) == fz::local_filesys::file) {
				themes.push_back(fz::to_wstring(name));
			}
		}
	}

	return themes;
}

std::vector<wxBitmap> CThemeProvider::GetAllImages(std::wstring const& theme, wxSize const& size)
{
	auto it = themes_.find(theme);
	if (it == themes_.end()) {
		CTheme t;
		if (!t.Load(theme)) {
			return std::vector<wxBitmap>();
		}

		it = themes_.insert(std::make_pair(theme, t)).first;
	}

	return it->second.GetAllImages(size);
}

bool CThemeProvider::GetThemeData(std::wstring const& theme, std::wstring & name, std::wstring & author, std::wstring & email)
{
	auto it = themes_.find(theme);
	if (it == themes_.end()) {
		CTheme t;
		if (!t.Load(theme)) {
			return false;
		}

		it = themes_.insert(std::make_pair(theme, t)).first;
	}

	name = it->second.get_name();
	author = it->second.get_author();
	email = it->second.get_mail();

	return true;
}

wxIconBundle CThemeProvider::GetIconBundle(const wxArtID& id, const wxArtClient&)
{
	wxIconBundle iconBundle;

	if (id.Left(4) != _T("ART_")) {
		return iconBundle;
	}

	wxString name = fz::str_tolower_ascii(id.Mid(4));

	const wxChar* dirs[] = { _T("16x16/"), _T("32x32/"), _T("48x48/") };

	CLocalPath const resourcePath = wxGetApp().GetResourceDir();

	for (auto const& dir : dirs) {
		wxString file = resourcePath.GetPath() + dir + name + _T(".png");
		if (!wxFileName::FileExists(file)) {
			continue;
		}

		iconBundle.AddIcon(wxIcon(file, wxBITMAP_TYPE_PNG));
	}

	return iconBundle;
}

void CThemeProvider::OnOptionsChanged(changed_options_t const&)
{
	std::wstring name = COptions::Get()->GetOption(OPTION_ICONS_THEME);
	if (themes_.find(name) == themes_.end()) {
		CTheme theme;
		if (theme.Load(name)) {
			themes_[name] = theme;
		}
	}

	wxArtProvider::Remove(this);
	wxArtProvider::Push(this);
}

wxSize CThemeProvider::GetIconSize(iconSize size, bool userScaled)
{
	int s;
	if (size == iconSizeSmall) {
		s = wxSystemSettings::GetMetric(wxSYS_SMALLICON_X);
		if (s <= 0) {
			s = 16;
		}
	}
	else if (size == iconSize24) {
		s = wxSystemSettings::GetMetric(wxSYS_SMALLICON_X);
		if (s <= 0) {
			s = 24;
		}
		else {
			s += s / 2;
		}
	}
	else if (size == iconSizeLarge) {
		s = wxSystemSettings::GetMetric(wxSYS_ICON_X);
		if (s <= 0) {
			s = 48;
		}
		else {
			s += s / 2;
		}
	}
	else {
		s = wxSystemSettings::GetMetric(wxSYS_ICON_X);
		if (s <= 0) {
			s = 32;
		}
	}

	wxSize ret(s, s);

#ifdef __WXGTK__
	GdkScreen * screen = gdk_screen_get_default();
	if (screen) {
		static gdouble const scale = gdk_screen_get_resolution(screen);
		if (scale >= 48) {
			ret = ret.Scale(scale / 96.f, scale / 96.f);
		}
	}
#endif

	if (userScaled) {
		float scale = static_cast<float>(COptions::Get()->GetOptionVal(OPTION_ICONS_SCALE));
		ret = ret.Scale(scale / 100.f, scale / 100.f);
	}

	return ret;
}

double CThemeProvider::GetUIScaleFactor()
{
	int x = GetIconSize(iconSizeSmall).x;
	if (!x) {
		return 1.;
	}
	else {
		return x / 16.;
	}
}
