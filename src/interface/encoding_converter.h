#ifndef FILEZILLA_INTERFACE_ENCODING_CONVERTER_HEADER
#define FILEZILLA_INTERFACE_ENCODING_CONVERTER_HEADER

#include "engine_context.h"

#include <memory>

class CustomEncodingConverter final : public CustomEncodingConverterBase
{
public:

	static CustomEncodingConverter const& Get();

	virtual std::wstring toLocal(std::wstring const& encoding, char const* buffer, size_t len) const override;
	virtual std::string toServer(std::wstring const& encoding, wchar_t const* buffer, size_t len) const override;

private:
	CustomEncodingConverter() = default;
};

#endif
