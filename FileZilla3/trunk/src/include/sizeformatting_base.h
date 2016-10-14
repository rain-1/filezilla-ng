#ifndef FILEZILLA_ENGINE_SIZEFORMATTING_BASE_HEADER
#define FILEZILLA_ENGINE_SIZEFORMATTING_BASE_HEADER

class COptionsBase;
class CSizeFormatBase
{
public:
	enum _format
	{
		bytes,
		iec,
		si1024,
		si1000,

		formats_count
	};

	// We stop at Exa. If someone has files bigger than that, he can afford to
	// make a donation to have this changed ;)
	enum _unit
	{
		byte,
		kilo,
		mega,
		giga,
		tera,
		peta,
		exa
	};

	static std::wstring FormatNumber(COptionsBase* pOptions, int64_t size, bool* thousands_separator = 0);

	static std::wstring GetUnitWithBase(COptionsBase* pOptions, _unit unit, int base);
	static std::wstring GetUnit(COptionsBase* pOptions, _unit unit, _format = formats_count);
	static std::wstring FormatUnit(COptionsBase* pOptions, int64_t size, _unit unit, int base = 1024);

	static std::wstring Format(COptionsBase* pOptions, int64_t size, bool add_bytes_suffix, _format format, bool thousands_separator, int num_decimal_places);
	static std::wstring Format(COptionsBase* pOptions, int64_t size, bool add_bytes_suffix = false);

	static std::wstring const& GetThousandsSeparator();
	static std::wstring const& GetRadixSeparator();
};

#endif
