#include <filezilla.h>

#include "encoding_converter.h"

#include <wx/strconv.h>

namespace {
thread_local std::map<std::wstring, std::unique_ptr<wxCSConv>> converters_;
thread_local std::vector<char> toServerBuffer_;
thread_local std::vector<wchar_t> toLocalBuffer_;

wxCSConv& GetConverter(std::wstring const& encoding)
{
	auto & conv_ = converters_[encoding];
	if (!conv_) {
		conv_ = std::make_unique<wxCSConv>(encoding);
	}

	return *conv_;
}
}

CustomEncodingConverter const& CustomEncodingConverter::Get()
{
	static CustomEncodingConverter ret;
	return ret;
}

std::wstring CustomEncodingConverter::toLocal(std::wstring const& encoding, char const* buffer, size_t len) const
{
	std::wstring ret;

	auto & conv = GetConverter(encoding);
	if (conv.IsOk()) {

		if (toLocalBuffer_.size() <= len) {
			toLocalBuffer_.resize(len + 1);
		}

		size_t written = conv.ToWChar(&toLocalBuffer_[0], toLocalBuffer_.size() - 1, buffer, len);
		if (written != wxCONV_FAILED) { 
			ret.assign(&toLocalBuffer_[0], &toLocalBuffer_[written]);
		}
	}
	return ret;
}

std::string CustomEncodingConverter::toServer(std::wstring const& encoding, wchar_t const* buffer, size_t len) const
{
	std::string ret;

	auto & conv = GetConverter(encoding);
	if (conv.IsOk()) {

		// We assume no encoding needs more than 4 characters per byte.
		if (toServerBuffer_.size() <= len * 4) {
			toServerBuffer_.resize(len * 4 + 1);
		}

		// Pro-tip: Never ever look into the wxMBConv internals if you value your sanity.
		size_t written = conv.FromWChar(&toServerBuffer_[0], toServerBuffer_.size() - 1, buffer, len);
		if (written != wxCONV_FAILED) {
			ret.assign(&toServerBuffer_[0], &toServerBuffer_[written]);
		}
	}
	return ret;
}
