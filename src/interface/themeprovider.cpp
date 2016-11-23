#include <filezilla.h>
#include "themeprovider.h"
#include "filezillaapp.h"
#include "Options.h"
#include "xmlfunctions.h"

#include <wx/animate.h>

#include <libfilezilla/format.hpp>
#include <libfilezilla/local_filesys.hpp>

#include <utility>

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
		it = cache_.insert(std::make_pair(name, std::map<wxSize, wxBitmap, size_cmp>())).first;
	}
	else {
		if (it->second.empty()) {
			// The name is known but the icon does not exist
			static wxBitmap empty;
			return empty;
		}
	}

	// Look for correct size
	auto & sizeCache = it->second;
	auto sit = sizeCache.find(size);
	if (sit != sizeCache.end()) {
		return sit->second;
	}

	return DoLoadBitmap(name, size, sizeCache);
}

wxBitmap const& CTheme::DoLoadBitmap(std::wstring const& name, wxSize const& size, std::map<wxSize, wxBitmap, size_cmp> & cache)
{
	// Go through all the theme sizes and look for the file we need

	// First look equal or larger icon
	auto const pivot = sizes_.lower_bound(size);
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

wxBitmap const& CTheme::LoadBitmapWithSpecificSizeAndScale(std::wstring const& name, wxSize const& size, wxSize const& scale, std::map<wxSize, wxBitmap, size_cmp> & cache)
{
	wxBitmap const& bmp = LoadBitmapWithSpecificSize(name, size, cache);
	if (!bmp.IsOk()) {
		return bmp;
	}

	if (bmp.GetSize() == scale) {
		return bmp;
	}

	// need to scale
	wxImage img = bmp.ConvertToImage();
	img.Rescale(scale.x, scale.y, wxIMAGE_QUALITY_HIGH);
	auto inserted = cache.insert(std::make_pair(scale, wxBitmap(img)));
	return inserted.first->second;
}

wxBitmap const& CTheme::LoadBitmapWithSpecificSize(std::wstring const& name, wxSize const& size, std::map<wxSize, wxBitmap, size_cmp> & cache)
{
	auto it = cache.find(size);
	if (it != cache.end()) {
		return it->second;
	}

	wxImage img(path_ + fz::sprintf(L"%dx%d/%s.png", size.x, size.y, name), wxBITMAP_TYPE_PNG);
	if (!img.Ok()) {
		static wxBitmap empty;
		return empty;
	}

	if (img.HasMask() && !img.HasAlpha()) {
		img.InitAlpha();
	}
	if (img.GetSize() != size) {
		img.Rescale(size.x, size.y, wxIMAGE_QUALITY_HIGH);
	}
	auto inserted = cache.insert(std::make_pair(size, wxBitmap(img)));
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
	if (unthemed.Load(std::wstring(), { wxSize(16,16), wxSize(20,20), wxSize(22,22), wxSize(24,24), wxSize(32,32), wxSize(48,48) })) {
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

std::vector<wxString> CThemeProvider::GetThemes()
{
	std::vector<wxString> themes;

	CLocalPath const resourceDir = wxGetApp().GetResourceDir();

	wxDir dir(resourceDir.GetPath());
	bool found;
	wxString subdir;
	for (found = dir.GetFirst(&subdir, _T("*"), wxDIR_DIRS); found; found = dir.GetNext(&subdir)) {
		if (wxFileName::FileExists(resourceDir.GetPath() + subdir + _T("/") + _T("theme.xml"))) {
			themes.push_back(subdir);
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

bool CThemeProvider::GetThemeData(const wxString& themePath, wxString& name, wxString& author, wxString& email)
{
	std::wstring const file(wxGetApp().GetResourceDir().GetPath() + themePath + _T("/theme.xml"));
	CXmlFile xml(file);
	auto theme = xml.Load().child("Theme");
	if (!theme) {
		return false;
	}

	name = GetTextElement(theme, "Name");
	author = GetTextElement(theme, "Author");
	email = GetTextElement(theme, "Mail");
	return true;
}

std::vector<std::wstring> CThemeProvider::GetThemeSizes(const wxString& themePath, bool & scalable)
{
	std::vector<std::wstring> sizes;

	std::wstring const file(wxGetApp().GetResourceDir().GetPath() + themePath + _T("/theme.xml"));
	CXmlFile xml(file);
	auto theme = xml.Load().child("Theme");
	if (!theme) {
		return sizes;
	}

	scalable = std::string(theme.child_value("scalable")) == "1";

	for (auto xSize = theme.child("size"); xSize; xSize = xSize.next_sibling("size")) {
		std::wstring size = GetTextElement(xSize);
		if (size.empty()) {
			continue;
		}
		sizes.push_back(size);
	}

	return sizes;
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

bool CThemeProvider::ThemeHasSize(const wxString& themePath, const wxString& size)
{
	std::wstring const file(wxGetApp().GetResourceDir().GetPath() + themePath + _T("theme.xml"));
	CXmlFile xml(file);
	auto theme = xml.Load().child("Theme");
	if (!theme) {
		return false;
	}

	for (auto xSize = theme.child("size"); xSize; xSize = xSize.next_sibling("size")) {
		wxString s = GetTextElement(xSize);
		if (size == s) {
			return true;
		}
	}

	return false;
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
	if (userScaled) {
		float scale = static_cast<float>(COptions::Get()->GetOptionVal(OPTION_ICONS_SCALE));
		ret = ret.Scale(scale / 100.f, scale / 100.f);
	}

	return ret;
}
