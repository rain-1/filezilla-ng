#include <filezilla.h>
#include "sizeformatting.h"
#include "Options.h"

std::wstring CSizeFormat::Format(int64_t size, bool add_bytes_suffix, CSizeFormat::_format format, bool thousands_separator, int num_decimal_places)
{
	return CSizeFormatBase::Format(COptions::Get(), size, add_bytes_suffix, format, thousands_separator, num_decimal_places);
}

std::wstring CSizeFormat::Format(int64_t size, bool add_bytes_suffix)
{
	return CSizeFormatBase::Format(COptions::Get(), size, add_bytes_suffix);
}

std::wstring CSizeFormat::FormatUnit(int64_t size, CSizeFormat::_unit unit, int base)
{
	return CSizeFormatBase::FormatUnit(COptions::Get(), size, unit, base);
}

std::wstring CSizeFormat::GetUnitWithBase(_unit unit, int base)
{
	return CSizeFormatBase::GetUnitWithBase(COptions::Get(), unit, base);
}

std::wstring CSizeFormat::GetUnit(_unit unit, CSizeFormat::_format format)
{
	return CSizeFormatBase::GetUnit(COptions::Get(), unit, format);
}

std::wstring CSizeFormat::FormatNumber(int64_t size, bool* thousands_separator)
{
	return CSizeFormatBase::FormatNumber(COptions::Get(), size, thousands_separator);
}
