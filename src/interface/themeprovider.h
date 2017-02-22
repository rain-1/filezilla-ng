#ifndef __THEMEPROVIDER_H__
#define __THEMEPROVIDER_H__

#include <option_change_event_handler.h>
#include <memory>

#include <libfilezilla/time.hpp>

enum iconSize
{
	iconSizeSmall,
	iconSize24,
	iconSizeNormal,
	iconSizeLarge
};

class CTheme final
{
public:
	bool Load(std::wstring const& theme);
	bool Load(std::wstring const& theme, std::vector<wxSize> sizes);

	wxBitmap const& LoadBitmap(std::wstring const& name, wxSize const& size);
	wxAnimation LoadAnimation(std::wstring const& name, wxSize const& size);

	static wxSize StringToSize(std::wstring const&);

	std::wstring get_theme() const { return theme_; }
	std::wstring get_name() const { return name_; }
	std::wstring get_author() const { return author_; }
	std::wstring get_mail() const { return mail_; }

	std::vector<wxBitmap> GetAllImages(wxSize const& size);
private:
	struct size_cmp final
	{
		bool operator()(wxSize const& a, wxSize const&b) const {
			return a.x < b.x || (a.x == b.x && a.y < b.y);
		}
	};

	struct cacheEntry
	{
		// Converting from wxImage to wxBitmap to wxImage is quite slow, so cache the images as well.
		std::map<wxSize, wxBitmap, size_cmp> bitmaps_;
		std::map<wxSize, wxImage, size_cmp> images_;
	};

	wxBitmap const& DoLoadBitmap(std::wstring const& name, wxSize const& size, cacheEntry & cache);

	wxBitmap const& LoadBitmapWithSpecificSizeAndScale(std::wstring const& name, wxSize const& size, wxSize const& scale, cacheEntry & cache);

	wxImage const& LoadImageWithSpecificSize(std::wstring const& file, wxSize const& size, cacheEntry & cache);

	std::wstring theme_;
	std::wstring path_;

	std::wstring name_;
	std::wstring author_;
	std::wstring mail_;

	fz::datetime timestamp_;

	std::map<wxSize, bool, size_cmp> sizes_;

	std::map<std::wstring, cacheEntry> cache_;
};

class CThemeProvider final : public wxArtProvider, protected COptionChangeEventHandler
{
public:
	CThemeProvider();
	virtual ~CThemeProvider();

	static std::vector<std::wstring> GetThemes();
	std::vector<wxBitmap> GetAllImages(std::wstring const& theme, wxSize const& size);
	bool GetThemeData(std::wstring const& theme, std::wstring& name, std::wstring& author, std::wstring& email);
	static wxIconBundle GetIconBundle(const wxArtID& id, const wxArtClient& client = wxART_OTHER);

	static wxSize GetIconSize(iconSize size, bool userScaled = false);

	// Note: Always 1 on OS X
	static double GetUIScaleFactor();

	static CThemeProvider* Get();

	wxAnimation CreateAnimation(wxArtID const& id, wxSize const& size);

	virtual wxBitmap CreateBitmap(wxArtID const& id, wxArtClient const& client, wxSize const& size);

protected:

	virtual void OnOptionsChanged(changed_options_t const& options);

	std::map<std::wstring, CTheme> themes_;
};

#endif //__THEMEPROVIDER_H__
