#ifndef FILEZILLA_INTERFACE_SIZEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_SIZEFORMATTING_HEADER

#include <sizeformatting_base.h>

class CSizeFormat final : public CSizeFormatBase
{
public:
	static std::wstring FormatNumber(int64_t size, bool* thousands_separator = 0);

	static std::wstring GetUnitWithBase(_unit unit, int base);
	static std::wstring GetUnit(_unit unit, _format = formats_count);
	static std::wstring FormatUnit(int64_t size, _unit unit, int base = 1024);

	static std::wstring Format(int64_t size, bool add_bytes_suffix, _format format, bool thousands_separator, int num_decimal_places);
	static std::wstring Format(int64_t size, bool add_bytes_suffix = false);
};

#endif
