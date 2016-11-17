#include <filezilla.h>
#include "sizeformatting_base.h"
#include "optionsbase.h"
#ifndef FZ_WINDOWS
#include <langinfo.h>
#endif

#include <libfilezilla/format.hpp>

#include <math.h>
#include <assert.h>

namespace {
wchar_t const prefix[] = { ' ', 'K', 'M', 'G', 'T', 'P', 'E' };

std::wstring ToString(int64_t n, wchar_t const* const sepBegin = 0, wchar_t const* const sepEnd = 0)
{
	std::wstring ret;
	if (!n) {
		ret = L"0";
	}
	else {
		bool neg = false;
		if (n < 0) {
			n *= -1;
			neg = true;
		}

		wchar_t buf[60];
		wchar_t * const end = &buf[sizeof(buf) / sizeof(wchar_t) - 1];
		wchar_t * p = end;

		int d = 0;
		while (n != 0) {
			*--p = '0' + n % 10;
			n /= 10;

			if (sepBegin && !(++d % 3) && n != 0) {
				wchar_t *q = p - (sepEnd - sepBegin);
				for (wchar_t const* s = sepBegin; s != sepEnd; ++s) {
					*q++ = *s;
				}
				p -= sepEnd - sepBegin;
			}
		}

		if (neg) {
			*--p = '-';
		}

		ret.assign(p, end - p);
	}
	return ret;
}
}

std::wstring CSizeFormatBase::Format(COptionsBase* pOptions, int64_t size, bool add_bytes_suffix, CSizeFormatBase::_format format, bool thousands_separator, int num_decimal_places)
{
	assert(format != formats_count);
	assert(size >= 0);
	if (size < 0) {
		size = 0;
	}

	if (format == bytes) {
		std::wstring result = FormatNumber(pOptions, size, &thousands_separator);

		if (!add_bytes_suffix) {
			return result;
		}
		else {
			return fz::sprintf(fztranslate("%s byte", "%s bytes", size), result);
		}
	}

	std::wstring places;

	int divider;
	if (format == si1000) {
		divider = 1000;
	}
	else {
		divider = 1024;
	}

	// Exponent (2^(10p) or 10^(3p) depending on option
	int p = 0;

	int64_t r = size;
	int remainder = 0;
	bool clipped = false;
	while (r > divider && p < 6) {
		int64_t const rr = r / divider;
		if (remainder != 0) {
			clipped = true;
		}
		remainder = static_cast<int>(r - rr * divider);
		r = rr;
		++p;
	}
	if (!num_decimal_places) {
		if (remainder != 0 || clipped) {
			++r;
		}
	}
	else if (p) { // Don't add decimal places on exact bytes
		if (format != si1000) {
			// Binary, need to convert 1024 into range from 1-1000
			if (clipped) {
				++remainder;
				clipped = false;
			}
			remainder = (int)ceil((double)remainder * 1000 / 1024);
		}

		int max;
		switch (num_decimal_places)
		{
		default:
			num_decimal_places = 1;
			// Fall-through
		case 1:
			max = 9;
			divider = 100;
			break;
		case 2:
			max = 99;
			divider = 10;
			break;
		case 3:
			max = 999;
			break;
		}

		if (num_decimal_places != 3) {
			if (remainder % divider) {
				clipped = true;
			}
			remainder /= divider;
		}

		if (clipped) {
			remainder++;
		}
		if (remainder > max) {
			r++;
			remainder = 0;
		}

		wchar_t fmt[] = L"%00d";
		fmt[2] = '0' + num_decimal_places;
		places = fz::sprintf(fmt, remainder);
	}

	std::wstring result = ToString(r, 0, 0);
	if (!places.empty()) {
		std::wstring const& sep = GetRadixSeparator();

		result += sep;
		result += places;
	}
	result += ' ';

	static wchar_t byte_unit = 0;
	if (!byte_unit) {
		std::wstring t = _("B <Unit symbol for bytes. Only translate first letter>"); // @translator: Only translate first letter.
		byte_unit = t[0];
	}

	if (!p) {
		return result + byte_unit;
	}

	result += prefix[p];
	if (format == iec) {
		result += 'i';
	}

	result += byte_unit;

	return result;
}

std::wstring CSizeFormatBase::Format(COptionsBase* pOptions, int64_t size, bool add_bytes_suffix)
{
	_format const format = _format(pOptions->GetOptionVal(OPTION_SIZE_FORMAT));
	bool const thousands_separator = pOptions->GetOptionVal(OPTION_SIZE_USETHOUSANDSEP) != 0;
	int const num_decimal_places = pOptions->GetOptionVal(OPTION_SIZE_DECIMALPLACES);

	return Format(pOptions, size, add_bytes_suffix, format, thousands_separator, num_decimal_places);
}

std::wstring CSizeFormatBase::FormatUnit(COptionsBase* pOptions, int64_t size, CSizeFormatBase::_unit unit, int base)
{
	_format format = _format(pOptions->GetOptionVal(OPTION_SIZE_FORMAT));
	if (base == 1000) {
		format = si1000;
	}
	else if (format != si1024) {
		format = iec;
	}
	return FormatNumber(pOptions, size) + L" " + GetUnit(pOptions, unit, format);
}

std::wstring CSizeFormatBase::GetUnitWithBase(COptionsBase* pOptions, _unit unit, int base)
{
	_format format = _format(pOptions->GetOptionVal(OPTION_SIZE_FORMAT));
	if (base == 1000) {
		format = si1000;
	}
	else if (format != si1024) {
		format = iec;
	}
	return GetUnit(pOptions, unit, format);
}

std::wstring CSizeFormatBase::GetUnit(COptionsBase* pOptions, _unit unit, CSizeFormatBase::_format format)
{
	std::wstring ret;
	if (unit != byte) {
		ret = prefix[unit];
	}

	if (format == formats_count) {
		format = _format(pOptions->GetOptionVal(OPTION_SIZE_FORMAT));
	}
	if (format == iec || format == bytes) {
		ret += 'i';
	}

	static wchar_t byte_unit = 0;
	if (!byte_unit) {
		std::wstring t = _("B <Unit symbol for bytes. Only translate first letter>"); // @translator: Only translate first letter.
		byte_unit = t[0];
	}

	ret += byte_unit;

	return ret;
}

namespace {
std::wstring DoGetThousandsSeparator()
{
	std::wstring sep;
#ifdef FZ_WINDOWS
	wchar_t tmp[5];
	int count = ::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, tmp, 5);
	if (count) {
		sep = tmp;
	}
#else
	char* chr = nl_langinfo(THOUSEP);
	if (chr && *chr) {
		sep = fz::to_wstring(chr);
	}
#endif
	if (sep.size() > 5) {
		sep = sep.substr(0, 5);
	}
	return sep;
}

std::wstring DoGetRadixSeparator()
{
	std::wstring sep;
#ifdef FZ_WINDOWS
	wchar_t tmp[5];
	int count = ::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, tmp, 5);
	if (!count) {
		sep = L".";
	}
	else {
		tmp[4] = 0;
		sep = tmp;
	}
#else
	char* chr = nl_langinfo(RADIXCHAR);
	if (!chr || !*chr) {
		sep = L".";
	}
	else {
		sep = fz::to_wstring(chr);
	}
#endif

	return sep;
}
}

std::wstring const& CSizeFormatBase::GetThousandsSeparator()
{
	static std::wstring const sep = DoGetThousandsSeparator();
	return sep;
}

std::wstring const& CSizeFormatBase::GetRadixSeparator()
{
	static std::wstring const sep = DoGetRadixSeparator();
	return sep;
}

std::wstring CSizeFormatBase::FormatNumber(COptionsBase* pOptions, int64_t size, bool* thousands_separator)
{
	std::wstring sep;
	wchar_t const* sepBegin = 0;
	wchar_t const* sepEnd = 0;

	if ((!thousands_separator || *thousands_separator) && pOptions->GetOptionVal(OPTION_SIZE_USETHOUSANDSEP) != 0) {
		sep = GetThousandsSeparator();
		if (!sep.empty()) {
			sepBegin = sep.c_str();
			sepEnd = sepBegin + sep.size();
		}
	}

	return ToString(size, sepBegin, sepEnd);
}
