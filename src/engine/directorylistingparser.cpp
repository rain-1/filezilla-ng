#include <filezilla.h>
#include "directorylistingparser.h"
#include "ControlSocket.h"

#include <libfilezilla/format.hpp>

#include <algorithm>
#include <vector>

#include <string.h>

std::map<std::wstring, int> CDirectoryListingParser::m_MonthNamesMap;

//#define LISTDEBUG_MVS
//#define LISTDEBUG
#ifdef LISTDEBUG
static char const data[][150]={
	"" // Has to be terminated with empty string
};

#endif

namespace {
struct ObjectCache
{
	fz::shared_value<std::wstring> const& get(std::wstring const& v)
	{
		auto it = std::lower_bound(cache.begin(), cache.end(), v);

		if (it == cache.end() || !(*it == v)) {
			it = cache.emplace(it, v);
		}
		return *it;
	}

	// Vector coupled with binary search and sorted insertion is fastest
	// alternative as we expect a relatively low amount of inserts.
	// Note that we cannot use set, as it it cannot search based on a different type.
	std::vector<fz::shared_value<std::wstring>> cache;
};


ObjectCache objcache;
}

class CToken final
{
protected:
	enum TokenInformation
	{
		Unknown,
		Yes,
		No
	};

public:
	CToken() = default;

	enum t_numberBase
	{
		decimal,
		hex
	};

	CToken(wchar_t const* p, unsigned int len)
		: m_pToken(p)
		, m_len(len)
	{}

	wchar_t const* GetToken() const
	{
		return m_pToken;
	}

	unsigned int GetLength() const
	{
		return m_len;
	}

	std::wstring GetString() const
	{
		if (!m_pToken || !m_len) {
			return std::wstring();
		}
		else {
			return std::wstring(m_pToken, m_len);
		}
	}

	bool IsNumeric(t_numberBase base = decimal)
	{
		switch (base)
		{
		case decimal:
		default:
			if (m_numeric == Unknown) {
				m_numeric = Yes;
				for (unsigned int i = 0; i < m_len; ++i) {
					if (m_pToken[i] < '0' || m_pToken[i] > '9') {
						m_numeric = No;
						break;
					}
				}
			}
			return m_numeric == Yes;
		case hex:
			for (unsigned int i = 0; i < m_len; ++i) {
				auto const c = m_pToken[i];
				if ((c < '0' || c > '9') && (c < 'A' || c > 'F') && (c < 'a' || c > 'f')) {
					return false;
				}
			}
			return true;
		}
	}

	bool IsNumeric(unsigned int start, unsigned int len)
	{
		for (unsigned int i = start; i < std::min(start + len, m_len); ++i) {
			if (m_pToken[i] < '0' || m_pToken[i] > '9') {
				return false;
			}
		}
		return true;
	}

	bool IsLeftNumeric()
	{
		if (m_leftNumeric == Unknown) {
			if (m_len < 2) {
				m_leftNumeric = No;
			}
			else if (m_pToken[0] < '0' || m_pToken[0] > '9') {
				m_leftNumeric = No;
			}
			else {
				m_leftNumeric = Yes;
			}
		}
		return m_leftNumeric == Yes;
	}

	bool IsRightNumeric()
	{
		if (m_rightNumeric == Unknown) {
			if (m_len < 2) {
				m_rightNumeric = No;
			}
			else if (m_pToken[m_len - 1] < '0' || m_pToken[m_len - 1] > '9') {
				m_rightNumeric = No;
			}
			else {
				m_rightNumeric = Yes;
			}
		}
		return m_rightNumeric == Yes;
	}

	int Find(const wchar_t* chr, int start = 0) const
	{
		if (!chr) {
			return -1;
		}

		for (unsigned int i = start; i < m_len; ++i) {
			for (int c = 0; chr[c]; ++c) {
				if (m_pToken[i] == chr[c]) {
					return i;
				}
			}
		}
		return -1;
	}

	int Find(wchar_t chr, int start = 0) const
	{
		if (!m_pToken) {
			return -1;
		}

		for (unsigned int i = start; i < m_len; ++i) {
			if (m_pToken[i] == chr) {
				return i;
			}
		}

		return -1;
	}

	int64_t GetNumber(unsigned int start, int len)
	{
		if (len == -1) {
			len = m_len - start;
		}
		if (len < 1) {
			return -1;
		}

		if (start + static_cast<unsigned int>(len) > m_len) {
			return -1;
		}

		if (m_pToken[start] < '0' || m_pToken[start] > '9') {
			return -1;
		}

		int64_t number = 0;
		for (unsigned int i = start; i < (start + len); ++i) {
			if (m_pToken[i] < '0' || m_pToken[i] > '9') {
				break;
			}
			number *= 10;
			number += m_pToken[i] - '0';
		}
		return number;
	}

	int64_t GetNumber(t_numberBase base = decimal)
	{
		switch (base) {
		default:
		case decimal:
			if (m_number == -1) {
				if (IsNumeric() || IsLeftNumeric()) {
					m_number = 0;
					for (unsigned int i = 0; i < m_len; ++i) {
						if (m_pToken[i] < '0' || m_pToken[i] > '9') {
							break;
						}
						m_number *= 10;
						m_number += m_pToken[i] - '0';
					}
				}
				else if (IsRightNumeric()) {
					m_number = 0;
					int start = m_len - 1;
					while (m_pToken[start - 1] >= '0' && m_pToken[start - 1] <= '9') {
						--start;
					}
					for (unsigned int i = start; i < m_len; ++i) {
						m_number *= 10;
						m_number += m_pToken[i] - '0';
					}
				}
			}
			return m_number;
		case hex:
			{
				int64_t number = 0;
				for (unsigned int i = 0; i < m_len; ++i) {
					const wchar_t& c = m_pToken[i];
					if (c >= '0' && c <= '9') {
						number *= 16;
						number += c - '0';
					}
					else if (c >= 'a' && c <= 'f') {
						number *= 16;
						number += c - '0' + 10;
					}
					else if (c >= 'A' && c <= 'F') {
						number *= 16;
						number += c - 'A' + 10;
					}
					else {
						return -1;
					}
				}
				return number;
			}
		}
	}

	wchar_t operator[](unsigned int n) const
	{
		if (n >= m_len) {
			return 0;
		}

		return m_pToken[n];
	}

protected:
	wchar_t const* m_pToken{};
	unsigned int m_len{};

	TokenInformation m_numeric{Unknown};
	TokenInformation m_leftNumeric{Unknown};
	TokenInformation m_rightNumeric{Unknown};
	int64_t m_number{-1};
};

class CLine final
{
public:
	CLine(std::wstring && line, int trailing_whitespace = -1)
		: trailing_whitespace_(trailing_whitespace)
		, line_(line)
	{
		m_Tokens.reserve(10);
		m_LineEndTokens.reserve(10);
		while (m_parsePos < line_.size() && (line_[m_parsePos] == ' ' || line_[m_parsePos] == '\t')) {
			++m_parsePos;
		}
	}

	~CLine()
	{
		std::vector<CToken *>::iterator iter;
		for (iter = m_Tokens.begin(); iter != m_Tokens.end(); ++iter)
			delete *iter;
		for (iter = m_LineEndTokens.begin(); iter != m_LineEndTokens.end(); ++iter)
			delete *iter;
	}

	bool GetToken(unsigned int n, CToken &token, bool toEnd = false, bool include_whitespace = false)
	{
		if (!toEnd) {
			if (m_Tokens.size() > n) {
				token = *(m_Tokens[n]);
				return true;
			}

			size_t start = m_parsePos;
			while (m_parsePos < line_.size()) {
				if (line_[m_parsePos] == ' ' || line_[m_parsePos] == '\t') {
					CToken *pToken = new CToken(line_.c_str() + start, m_parsePos - start);
					m_Tokens.push_back(pToken);

					while (m_parsePos < line_.size() && (line_[m_parsePos] == ' ' || line_[m_parsePos] == '\t'))
						++m_parsePos;

					if (m_Tokens.size() > n) {
						token = *(m_Tokens[n]);
						return true;
					}

					start = m_parsePos;
				}
				++m_parsePos;
			}
			if (m_parsePos != start) {
				CToken *pToken = new CToken(line_.c_str() + start, m_parsePos - start);
				m_Tokens.push_back(pToken);
			}

			if (m_Tokens.size() > n) {
				token = *(m_Tokens[n]);
				return true;
			}

			return false;
		}
		else {
			if (include_whitespace) {
				int prev = n;
				if (prev)
					--prev;

				CToken ref;
				if (!GetToken(prev, ref))
					return false;
				wchar_t const* p = ref.GetToken() + ref.GetLength() + 1;

				auto const newLen = line_.size() - (p - line_.c_str());
				if (newLen <= 0) {
					return false;
				}
				token = CToken(p, newLen);
				return true;
			}

			if (m_LineEndTokens.size() > n) {
				token = *(m_LineEndTokens[n]);
				return true;
			}

			if (m_Tokens.size() <= n) {
				if (!GetToken(n, token)) {
					return false;
				}
			}

			if (trailing_whitespace_ == -1) {
				trailing_whitespace_ = 0;
				size_t i = line_.size() - 1;
				while (i < line_.size() && (line_[i] == ' ' || line_[i] == '\t')) {
					--i;
					++trailing_whitespace_;
				}
			}

			for (unsigned int i = static_cast<unsigned int>(m_LineEndTokens.size()); i <= n; ++i) {
				const CToken *refToken = m_Tokens[i];
				const wchar_t* p = refToken->GetToken();
				auto const newLen = line_.size() - (p - line_.c_str()) - trailing_whitespace_;
				if (newLen <= 0) {
					return false;
				}
				CToken *pToken = new CToken(p, newLen);
				m_LineEndTokens.push_back(pToken);
			}
			token = *(m_LineEndTokens[n]);
			return true;
		}
	};

	CLine *Concat(CLine const* pLine) const
	{
		std::wstring n;
		n.reserve(line_.size() + pLine->line_.size() + 1);
		n = line_;
		n += ' ';
		n += pLine->line_;
		return new CLine(std::move(n), pLine->trailing_whitespace_);
	}

protected:
	std::vector<CToken *> m_Tokens;
	std::vector<CToken *> m_LineEndTokens;
	size_t m_parsePos{};
	int trailing_whitespace_;
	std::wstring const line_;
};

CDirectoryListingParser::CDirectoryListingParser(CControlSocket* pControlSocket, const CServer& server, listingEncoding::type encoding)
	: m_pControlSocket(pControlSocket)
	, m_currentOffset(0)
	, m_totalData()
	, m_prevLine(0)
	, m_server(server)
	, m_fileListOnly(true)
	, m_maybeMultilineVms(false)
	, m_listingEncoding(encoding)
{
	if (m_MonthNamesMap.empty()) {
		//Fill the month names map

		//English month names
		m_MonthNamesMap[L"jan"] = 1;
		m_MonthNamesMap[L"feb"] = 2;
		m_MonthNamesMap[L"mar"] = 3;
		m_MonthNamesMap[L"apr"] = 4;
		m_MonthNamesMap[L"may"] = 5;
		m_MonthNamesMap[L"jun"] = 6;
		m_MonthNamesMap[L"june"] = 6;
		m_MonthNamesMap[L"jul"] = 7;
		m_MonthNamesMap[L"july"] = 7;
		m_MonthNamesMap[L"aug"] = 8;
		m_MonthNamesMap[L"sep"] = 9;
		m_MonthNamesMap[L"sept"] = 9;
		m_MonthNamesMap[L"oct"] = 10;
		m_MonthNamesMap[L"nov"] = 11;
		m_MonthNamesMap[L"dec"] = 12;

		//Numerical values for the month
		m_MonthNamesMap[L"1"] = 1;
		m_MonthNamesMap[L"01"] = 1;
		m_MonthNamesMap[L"2"] = 2;
		m_MonthNamesMap[L"02"] = 2;
		m_MonthNamesMap[L"3"] = 3;
		m_MonthNamesMap[L"03"] = 3;
		m_MonthNamesMap[L"4"] = 4;
		m_MonthNamesMap[L"04"] = 4;
		m_MonthNamesMap[L"5"] = 5;
		m_MonthNamesMap[L"05"] = 5;
		m_MonthNamesMap[L"6"] = 6;
		m_MonthNamesMap[L"06"] = 6;
		m_MonthNamesMap[L"7"] = 7;
		m_MonthNamesMap[L"07"] = 7;
		m_MonthNamesMap[L"8"] = 8;
		m_MonthNamesMap[L"08"] = 8;
		m_MonthNamesMap[L"9"] = 9;
		m_MonthNamesMap[L"09"] = 9;
		m_MonthNamesMap[L"10"] = 10;
		m_MonthNamesMap[L"11"] = 11;
		m_MonthNamesMap[L"12"] = 12;

		//German month names
		m_MonthNamesMap[L"mrz"] = 3;
		m_MonthNamesMap[L"m\xe4r"] = 3;
		m_MonthNamesMap[L"m\xe4rz"] = 3;
		m_MonthNamesMap[L"mai"] = 5;
		m_MonthNamesMap[L"juni"] = 6;
		m_MonthNamesMap[L"juli"] = 7;
		m_MonthNamesMap[L"okt"] = 10;
		m_MonthNamesMap[L"dez"] = 12;

		//Austrian month names
		m_MonthNamesMap[L"j\xe4n"] = 1;

		//French month names
		m_MonthNamesMap[L"janv"] = 1;
		m_MonthNamesMap[L"f\xe9" L"b"] = 1;
		m_MonthNamesMap[L"f\xe9v"] = 2;
		m_MonthNamesMap[L"fev"] = 2;
		m_MonthNamesMap[L"f\xe9vr"] = 2;
		m_MonthNamesMap[L"fevr"] = 2;
		m_MonthNamesMap[L"mars"] = 3;
		m_MonthNamesMap[L"mrs"] = 3;
		m_MonthNamesMap[L"avr"] = 4;
		m_MonthNamesMap[L"avril"] = 4;
		m_MonthNamesMap[L"juin"] = 6;
		m_MonthNamesMap[L"juil"] = 7;
		m_MonthNamesMap[L"jui"] = 7;
		m_MonthNamesMap[L"ao\xfb"] = 8;
		m_MonthNamesMap[L"ao\xfbt"] = 8;
		m_MonthNamesMap[L"aout"] = 8;
		m_MonthNamesMap[L"d\xe9" L"c"] = 12;
		m_MonthNamesMap[L"dec"] = 12;

		//Italian month names
		m_MonthNamesMap[L"gen"] = 1;
		m_MonthNamesMap[L"mag"] = 5;
		m_MonthNamesMap[L"giu"] = 6;
		m_MonthNamesMap[L"lug"] = 7;
		m_MonthNamesMap[L"ago"] = 8;
		m_MonthNamesMap[L"set"] = 9;
		m_MonthNamesMap[L"ott"] = 10;
		m_MonthNamesMap[L"dic"] = 12;

		//Spanish month names
		m_MonthNamesMap[L"ene"] = 1;
		m_MonthNamesMap[L"fbro"] = 2;
		m_MonthNamesMap[L"mzo"] = 3;
		m_MonthNamesMap[L"ab"] = 4;
		m_MonthNamesMap[L"abr"] = 4;
		m_MonthNamesMap[L"agto"] = 8;
		m_MonthNamesMap[L"sbre"] = 9;
		m_MonthNamesMap[L"obre"] = 9;
		m_MonthNamesMap[L"nbre"] = 9;
		m_MonthNamesMap[L"dbre"] = 9;

		//Polish month names
		m_MonthNamesMap[L"sty"] = 1;
		m_MonthNamesMap[L"lut"] = 2;
		m_MonthNamesMap[L"kwi"] = 4;
		m_MonthNamesMap[L"maj"] = 5;
		m_MonthNamesMap[L"cze"] = 6;
		m_MonthNamesMap[L"lip"] = 7;
		m_MonthNamesMap[L"sie"] = 8;
		m_MonthNamesMap[L"wrz"] = 9;
		m_MonthNamesMap[L"pa\x9f"] = 10;
		m_MonthNamesMap[L"pa\xbc"] = 10; // ISO-8859-2
		m_MonthNamesMap[L"paz"] = 10; // ASCII
		m_MonthNamesMap[L"pa\xc5\xba"] = 10; // UTF-8
		m_MonthNamesMap[L"pa\x017a"] = 10; // some servers send this
		m_MonthNamesMap[L"lis"] = 11;
		m_MonthNamesMap[L"gru"] = 12;

		//Russian month names
		m_MonthNamesMap[L"\xff\xed\xe2"] = 1;
		m_MonthNamesMap[L"\xf4\xe5\xe2"] = 2;
		m_MonthNamesMap[L"\xec\xe0\xf0"] = 3;
		m_MonthNamesMap[L"\xe0\xef\xf0"] = 4;
		m_MonthNamesMap[L"\xec\xe0\xe9"] = 5;
		m_MonthNamesMap[L"\xe8\xfe\xed"] = 6;
		m_MonthNamesMap[L"\xe8\xfe\xeb"] = 7;
		m_MonthNamesMap[L"\xe0\xe2\xe3"] = 8;
		m_MonthNamesMap[L"\xf1\xe5\xed"] = 9;
		m_MonthNamesMap[L"\xee\xea\xf2"] = 10;
		m_MonthNamesMap[L"\xed\xee\xff"] = 11;
		m_MonthNamesMap[L"\xe4\xe5\xea"] = 12;

		//Dutch month names
		m_MonthNamesMap[L"mrt"] = 3;
		m_MonthNamesMap[L"mei"] = 5;

		//Portuguese month names
		m_MonthNamesMap[L"out"] = 10;

		//Finnish month names
		m_MonthNamesMap[L"tammi"] = 1;
		m_MonthNamesMap[L"helmi"] = 2;
		m_MonthNamesMap[L"maalis"] = 3;
		m_MonthNamesMap[L"huhti"] = 4;
		m_MonthNamesMap[L"touko"] = 5;
		m_MonthNamesMap[L"kes\xe4"] = 6;
		m_MonthNamesMap[L"hein\xe4"] = 7;
		m_MonthNamesMap[L"elo"] = 8;
		m_MonthNamesMap[L"syys"] = 9;
		m_MonthNamesMap[L"loka"] = 10;
		m_MonthNamesMap[L"marras"] = 11;
		m_MonthNamesMap[L"joulu"] = 12;

		//Slovenian month names
		m_MonthNamesMap[L"avg"] = 8;

		//Icelandic
		m_MonthNamesMap[L"ma\x00ed"] = 5;
		m_MonthNamesMap[L"j\x00fan"] = 6;
		m_MonthNamesMap[L"j\x00fal"] = 7;
		m_MonthNamesMap[L"\x00e1g"] = 8;
		m_MonthNamesMap[L"n\x00f3v"] = 11;
		m_MonthNamesMap[L"des"] = 12;

		//Lithuanian
		m_MonthNamesMap[L"sau"] = 1;
		m_MonthNamesMap[L"vas"] = 2;
		m_MonthNamesMap[L"kov"] = 3;
		m_MonthNamesMap[L"bal"] = 4;
		m_MonthNamesMap[L"geg"] = 5;
		m_MonthNamesMap[L"bir"] = 6;
		m_MonthNamesMap[L"lie"] = 7;
		m_MonthNamesMap[L"rgp"] = 8;
		m_MonthNamesMap[L"rgs"] = 9;
		m_MonthNamesMap[L"spa"] = 10;
		m_MonthNamesMap[L"lap"] = 11;
		m_MonthNamesMap[L"grd"] = 12;

		// Hungarian
		m_MonthNamesMap[L"szept"] = 9;

		//There are more languages and thus month
		//names, but as long as nobody reports a
		//problem, I won't add them, there are way
		//too many languages

		// Some servers send a combination of month name and number,
		// Add corresponding numbers to the month names.
		std::map<std::wstring, int> combo;
		for (auto iter = m_MonthNamesMap.begin(); iter != m_MonthNamesMap.end(); ++iter) {
			// January could be 1 or 0, depends how the server counts
			combo[fz::sprintf(L"%s%02d", iter->first, iter->second)] = iter->second;
			combo[fz::sprintf(L"%s%02d", iter->first, iter->second - 1)] = iter->second;
			if (iter->second < 10)
				combo[fz::sprintf(L"%s%d", iter->first, iter->second)] = iter->second;
			else
				combo[fz::sprintf(L"%s%d", iter->first, iter->second % 10)] = iter->second;
			if (iter->second <= 10)
				combo[fz::sprintf(L"%s%d", iter->first, iter->second - 1)] = iter->second;
			else
				combo[fz::sprintf(L"%s%d", iter->first, (iter->second - 1) % 10)] = iter->second;
		}
		m_MonthNamesMap.insert(combo.begin(), combo.end());

		m_MonthNamesMap[L"1"] = 1;
		m_MonthNamesMap[L"2"] = 2;
		m_MonthNamesMap[L"3"] = 3;
		m_MonthNamesMap[L"4"] = 4;
		m_MonthNamesMap[L"5"] = 5;
		m_MonthNamesMap[L"6"] = 6;
		m_MonthNamesMap[L"7"] = 7;
		m_MonthNamesMap[L"8"] = 8;
		m_MonthNamesMap[L"9"] = 9;
		m_MonthNamesMap[L"10"] = 10;
		m_MonthNamesMap[L"11"] = 11;
		m_MonthNamesMap[L"12"] = 12;
	}

#ifdef LISTDEBUG
	for (unsigned int i = 0; data[i][0]; ++i) {
		unsigned int len = (unsigned int)strlen(data[i]);
		char *pData = new char[len + 3];
		strcpy(pData, data[i]);
		strcat(pData, "\r\n");
		AddData(pData, len + 2);
	}
#endif
}

CDirectoryListingParser::~CDirectoryListingParser()
{
	for (auto iter = m_DataList.begin(); iter != m_DataList.end(); ++iter) {
		delete [] iter->p;
	}

	delete m_prevLine;
}

bool CDirectoryListingParser::ParseData(bool partial)
{
	DeduceEncoding();

	bool error = false;
	CLine *pLine = GetLine(partial, error);
	while (pLine) {
		bool res = ParseLine(*pLine, m_server.GetType(), false);
		if (!res) {
			if (m_prevLine) {
				CLine* pConcatenatedLine = m_prevLine->Concat(pLine);
				res = ParseLine(*pConcatenatedLine, m_server.GetType(), true);
				delete pConcatenatedLine;
				delete m_prevLine;

				if (res) {
					delete pLine;
					m_prevLine = 0;
				}
				else {
					m_prevLine = pLine;
				}
			}
			else {
				m_prevLine = pLine;
			}
		}
		else {
			delete m_prevLine;
			m_prevLine = 0;
			delete pLine;
		}
		pLine = GetLine(partial, error);
	};

	return !error;
}

CDirectoryListing CDirectoryListingParser::Parse(const CServerPath &path)
{
	CDirectoryListing listing;
	listing.path = path;
	listing.m_firstListTime = fz::monotonic_clock::now();

	if (!ParseData(false)) {
		listing.m_flags |= CDirectoryListing::listing_failed;
		return listing;
	}

	if (!m_fileList.empty()) {
		assert(m_entryList.empty());

		for (auto const& file : m_fileList) {
			CDirentry entry;
			entry.name = file;
			entry.flags = 0;
			entry.size = -1;
			m_entryList.emplace_back(std::move(entry));
		}
	}

	listing.Assign(m_entryList);

	return listing;
}

bool CDirectoryListingParser::ParseLine(CLine &line, ServerType const serverType, bool concatenated, CDirentry const* override)
{
	fz::shared_value<CDirentry> refEntry;
	CDirentry & entry = refEntry.get();

	bool res;
	int ires;

	if (serverType == ZVM) {
		res = ParseAsZVM(line, entry);
		if (res)
			goto done;
	}
	else if (serverType == HPNONSTOP) {
		res = ParseAsHPNonstop(line, entry);
		if (res)
			goto done;
	}

	ires = ParseAsMlsd(line, entry);
	if (ires == 1)
		goto done;
	else if (ires == 2)
		goto skip;
	res = ParseAsUnix(line, entry, true); // Common 'ls -l'
	if (res)
		goto done;
	res = ParseAsDos(line, entry);
	if (res)
		goto done;
	res = ParseAsEplf(line, entry);
	if (res)
		goto done;
	res = ParseAsVms(line, entry);
	if (res)
		goto done;
	res = ParseOther(line, entry);
	if (res)
		goto done;
	res = ParseAsIbm(line, entry);
	if (res)
		goto done;
	res = ParseAsWfFtp(line, entry);
	if (res)
		goto done;
	res = ParseAsIBM_MVS(line, entry);
	if (res)
		goto done;
	res = ParseAsIBM_MVS_PDS(line, entry);
	if (res)
		goto done;
	res = ParseAsOS9(line, entry);
	if (res)
		goto done;
#ifndef LISTDEBUG_MVS
	if (serverType == MVS)
#endif //LISTDEBUG_MVS
	{
		res = ParseAsIBM_MVS_Migrated(line, entry);
		if (res)
			goto done;
		res = ParseAsIBM_MVS_PDS2(line, entry);
		if (res)
			goto done;
		res = ParseAsIBM_MVS_Tape(line, entry);
		if (res)
			goto done;
	}
	res = ParseAsUnix(line, entry, false); // 'ls -l' but without the date/time
	if (res)
		goto done;

	// Some servers just send a list of filenames. If a line could not be parsed,
	// check if it's a filename. If that's the case, store it for later, else clear
	// list of stored files.
	// If parsing finishes and no entries could be parsed and none of the lines
	// contained a space, assume it's a raw filelisting.

	if (!concatenated) {
		CToken token;
		if (!line.GetToken(0, token, true) || token.Find(' ') != -1) {
			m_maybeMultilineVms = false;
			m_fileList.clear();
			m_fileListOnly = false;
		}
		else {
			m_maybeMultilineVms = token.Find(';') != -1;
			if (m_fileListOnly)
				m_fileList.emplace_back(token.GetString());
		}
	}
	else
		m_maybeMultilineVms = false;

	if (!override || override->name.empty()) {
		return false;
	}
done:

	if (override) {
		// If SFTP is uses we already have precise data for some fields
		if (!override->name.empty()) {
			entry.name = override->name;
		}
		if (!override->time.empty()) {
			entry.time = override->time;
		}
	}

	m_maybeMultilineVms = false;
	m_fileList.clear();
	m_fileListOnly = false;

	// Don't add . or ..
	if (entry.name == L"." || entry.name == L"..")
		return true;

	if (serverType == VMS && entry.is_dir()) {
		// Trim version information from directories
		auto pos = entry.name.rfind(';');
		if (pos != std::wstring::npos && pos > 0)
			entry.name = entry.name.substr(0, pos);
	}

	{
		auto const timezoneOffset = m_server.GetTimezoneOffset();
		if (timezoneOffset) {
			entry.time += fz::duration::from_minutes(timezoneOffset);
		}
	}

	m_entryList.emplace_back(std::move(refEntry));

skip:
	m_maybeMultilineVms = false;
	m_fileList.clear();
	m_fileListOnly = false;

	return true;
}

bool CDirectoryListingParser::ParseAsUnix(CLine &line, CDirentry &entry, bool expect_date)
{
	int index = 0;
	CToken token;
	if (!line.GetToken(index, token))
		return false;

	wchar_t chr = token[0];
	if (chr != 'b' &&
		chr != 'c' &&
		chr != 'd' &&
		chr != 'l' &&
		chr != 'p' &&
		chr != 's' &&
		chr != '-')
		return false;

	std::wstring permissions = token.GetString();

	entry.flags = 0;

	if (chr == 'd' || chr == 'l')
		entry.flags |= CDirentry::flag_dir;

	if (chr == 'l')
		entry.flags |= CDirentry::flag_link;

	// Check for netware servers, which split the permissions into two parts
	bool netware = false;
	if (token.GetLength() == 1) {
		if (!line.GetToken(++index, token))
			return false;
		permissions += L" " + token.GetString();
		netware = true;
	}

	int numOwnerGroup = 3;
	if (!netware) {
		// Filter out link count, we don't need it
		if (!line.GetToken(++index, token))
			return false;

		if (!token.IsNumeric())
			--index;
	}

	// Repeat until numOwnerGroup is 0 since not all servers send every possible field
	int startindex = index;
	do {
		// Reset index
		index = startindex;

		std::wstring ownerGroup;
		for (int i = 0; i < numOwnerGroup; ++i) {
			if (!line.GetToken(++index, token))
				return false;
			if (i)
				ownerGroup += L" ";
			ownerGroup += token.GetString();
		}

		if (!line.GetToken(++index, token))
			return false;

		// Check for concatenated groupname and size fields
		if (!ParseComplexFileSize(token, entry.size)) {
			if (!token.IsRightNumeric())
				continue;
			entry.size = token.GetNumber();
		}

		// Append missing group to ownerGroup
		if (!token.IsNumeric() && token.IsRightNumeric()) {
			if (!ownerGroup.empty())
				ownerGroup += L" ";

			std::wstring const group = token.GetString();
			int i;
			for (i = group.size() - 1;
				 i >= 0 && group[i] >= '0' && group[i] <= '9';
				 --i) {}

			ownerGroup += group.substr(0, i + 1);
		}

		if (expect_date) {
			entry.time = fz::datetime();
			if (!ParseUnixDateTime(line, index, entry))
				continue;
		}

		// Get the filename
		if (!line.GetToken(++index, token, 1))
			continue;

		entry.name = token.GetString();

		// Filter out special chars at the end of the filenames
		chr = token[token.GetLength() - 1];
		if (chr == '/' ||
			chr == '|' ||
			chr == '*')
			entry.name.pop_back();

		if (entry.is_link()) {
			size_t pos;
			if ((pos = entry.name.find(L" -> ")) != std::wstring::npos) {
				entry.target = fz::sparse_optional<std::wstring>(entry.name.substr(pos + 4));
				entry.name = entry.name.substr(0, pos);
			}
		}

		entry.time += m_timezoneOffset;

		entry.permissions = objcache.get(permissions);
		entry.ownerGroup = objcache.get(ownerGroup);
		return true;
	}
	while (numOwnerGroup--);

	return false;
}

bool CDirectoryListingParser::ParseUnixDateTime(CLine & line, int &index, CDirentry &entry)
{
	bool mayHaveTime = true;
	bool bHasYearAndTime = false;

	CToken token;

	// Get the month date field
	CToken dateMonth;
	if (!line.GetToken(++index, token)) {
		return false;
	}

	int year = -1;
	int month = -1;
	int day = -1;
	long hour = -1;
	long minute = -1;

	// Some servers use the following date formats:
	// 26-05 2002, 2002-10-14, 01-jun-99 or 2004.07.15
	// slashes instead of dashes are also possible
	int pos = token.Find(L"-/.");
	if (pos != -1) {
		int pos2 = token.Find(L"-/.", pos + 1);
		if (pos2 == -1) {
			if (token[pos] != '.') {
				// something like 26-05 2002
				day = token.GetNumber(pos + 1, token.GetLength() - pos - 1);
				if (day < 1 || day > 31) {
					return false;
				}
				dateMonth = CToken(token.GetToken(), pos);
			}
			else {
				dateMonth = token;
			}
		}
		else if (token[pos] != token[pos2]) {
			return false;
		}
		else {
			if (!ParseShortDate(token, entry)) {
				return false;
			}

			if (token[pos] == '.') {
				return true;
			}

			tm t = entry.time.get_tm(fz::datetime::utc);
			year = t.tm_year + 1900;
			month = t.tm_mon + 1;
			day = t.tm_mday;
		}
	}
	else if (token.IsNumeric()) {
		if (token.GetNumber() > 1000 && token.GetNumber() < 10000) {
			// Two possible variants:
			// 1) 2005 3 13
			// 2) 2005 13 3
			// assume first one.
			year = token.GetNumber();
			if (!line.GetToken(++index, dateMonth)) {
				return false;
			}
			mayHaveTime = false;
		}
		else {
			dateMonth = token;
		}
	}
	else {
		if (token.IsLeftNumeric() && (unsigned int)token[token.GetLength() - 1] > 127 &&
			token.GetNumber() > 1000)
		{
			if (token.GetNumber() > 10000)
				return false;

			// Asian date format: 2005xxx 5xx 20xxx with some non-ascii characters following
			year = token.GetNumber();
			if (!line.GetToken(++index, dateMonth)) {
				return false;
			}
			mayHaveTime = false;
		}
		else {
			dateMonth = token;
		}
	}

	if (day < 1) {
		// Get day field
		if (!line.GetToken(++index, token)) {
			return false;
		}

		int dateDay;

		// Check for non-numeric day
		if (!token.IsNumeric() && !token.IsLeftNumeric()) {
			int offset = 0;
			if (dateMonth.GetString().back() == '.')
				++offset;
			if (!dateMonth.IsNumeric(0, dateMonth.GetLength() - offset))
				return false;
			dateDay = dateMonth.GetNumber(0, dateMonth.GetLength() - offset);
			dateMonth = token;
		}
		else if( token.GetLength() == 5 && token[2] == ':' && token.IsRightNumeric() ) {
			// This is a time. We consumed too much already.
			return false;
		}
		else {
			dateDay = token.GetNumber();
			if (token[token.GetLength() - 1] == ',') {
				bHasYearAndTime = true;
			}
		}

		if (dateDay < 1 || dateDay > 31) {
			return false;
		}
		day = dateDay;
	}

	if (month < 1) {
		std::wstring strMonth = dateMonth.GetString();
		if (dateMonth.IsLeftNumeric() && (unsigned int)strMonth[strMonth.size() - 1] > 127) {
			// Most likely an Asian server sending some unknown language specific
			// suffix at the end of the monthname. Filter it out.
			int i;
			for (i = strMonth.size() - 1; i > 0; --i) {
				if (strMonth[i] >= '0' && strMonth[i] <= '9') {
					break;
				}
			}
			strMonth = strMonth.substr(0, i + 1);
		}
		// Check month name
		while (!strMonth.empty() && (strMonth.back() == ',' || strMonth.back() == '.')) {
			strMonth.pop_back();
		}
		if (!GetMonthFromName(strMonth, month)) {
			return false;
		}
	}

	// Get time/year field
	if (!line.GetToken(++index, token)) {
		return false;
	}

	pos = token.Find(L":.-");
	if (pos != -1 && mayHaveTime) {
		// token is a time
		if (!pos || static_cast<size_t>(pos) == (token.GetLength() - 1)) {
			return false;
		}

		std::wstring str = token.GetString();
		hour = fz::to_integral<int>(str.substr(0, pos), -1);
		minute = fz::to_integral<int>(str.substr(pos + 1), -1);

		if (hour < 0 || hour > 23) {
			// Allow alternate midnight representation
			if (hour != 24 || minute != 0) {
				return false;
			}
		}
		else if (minute < 0 || minute > 59) {
			return false;
		}

		// Some servers use times only for files newer than 6 months
		if (year <= 0) {
			assert(month != -1 && day != -1);
			tm const t = fz::datetime::now().get_tm(fz::datetime::utc);
			year = t.tm_year + 1900;
			int const currentDayOfYear = t.tm_mday + 31 * t.tm_mon;
			int const fileDayOfYear = day + 31 * (month - 1);

			// We have to compare with an offset of one. In the worst case,
			// the server's timezone might be up to 24 hours ahead of the
			// client.
			// Problem: Servers which do send the time but not the year even
			// one day away from getting 1 year old. This is far more uncommon
			// however.
			if ((currentDayOfYear + 1) < fileDayOfYear) {
				year -= 1;
			}
		}
	}
	else if (year <= 0) {
		// token is a year
		if (!token.IsNumeric() && !token.IsLeftNumeric()) {
			return false;
		}

		year = token.GetNumber();

		if (year > 3000) {
			return false;
		}
		if (year < 1000) {
			year += 1900;
		}

		if (bHasYearAndTime) {
			if (!line.GetToken(++index, token)) {
				return false;
			}

			if (token.Find(':') == 2 && token.GetLength() == 5 && token.IsLeftNumeric() && token.IsRightNumeric()) {
				pos = token.Find(':');
				// token is a time
				if (!pos || static_cast<size_t>(pos) == (token.GetLength() - 1)) {
					return false;
				}

				std::wstring str = token.GetString();

				hour = fz::to_integral<int>(str.substr(0, pos), -1);
				minute = fz::to_integral<int>(str.substr(pos + 1), -1);

				if (hour < 0 || hour > 23) {
					// Allow alternate midnight representation
					if (hour != 24 || minute != 0) {
						return false;
					}
				}
				else if (minute < 0 || minute > 59) {
					return false;
				}
			}
			else {
				--index;
			}
		}
	}
	else {
		--index;
	}

	if (!entry.time.set(fz::datetime::utc, year, month, day, hour, minute)) {
		return false;
	}

	return true;
}

bool CDirectoryListingParser::ParseShortDate(CToken &token, CDirentry &entry, bool saneFieldOrder)
{
	if (token.GetLength() < 1)
		return false;

	bool gotYear = false;
	bool gotMonth = false;
	bool gotDay = false;
	bool gotMonthName = false;

	int year = 0;
	int month = 0;
	int day = 0;

	int pos = token.Find(L"-./");
	if (pos < 1)
		return false;

	if (!token.IsNumeric(0, pos)) {
		// Seems to be monthname-dd-yy

		// Check month name
		std::wstring const dateMonth = token.GetString().substr(0, pos);
		if (!GetMonthFromName(dateMonth, month))
			return false;
		gotMonth = true;
		gotMonthName = true;
	}
	else if (pos == 4) {
		// Seems to be yyyy-mm-dd
		year = token.GetNumber(0, pos);
		if (year < 1900 || year > 3000)
			return false;
		gotYear = true;
	}
	else if (pos <= 2) {
		int64_t value = token.GetNumber(0, pos);
		if (token[pos] == '.') {
			// Maybe dd.mm.yyyy
			if (value < 1 || value > 31)
				return false;
			day = value;
			gotDay = true;
		}
		else {
			if (saneFieldOrder) {
				year = value;
				if (year < 50)
					year += 2000;
				else
					year += 1900;
				gotYear = true;
			}
			else {
				// Detect mm-dd-yyyy or mm/dd/yyyy and
				// dd-mm-yyyy or dd/mm/yyyy
				if (value < 1)
					return false;
				if (value > 12) {
					if (value > 31)
						return false;

					day = value;
					gotDay = true;
				}
				else {
					month = value;
					gotMonth = true;
				}
			}
		}
	}
	else
		return false;

	int pos2 = token.Find(L"-./", pos + 1);
	if (pos2 == -1 || (pos2 - pos) == 1)
		return false;
	if (static_cast<size_t>(pos2) == (token.GetLength() - 1))
		return false;

	// If we already got the month and the second field is not numeric,
	// change old month into day and use new token as month
	if (!token.IsNumeric(pos + 1, pos2 - pos - 1) && gotMonth) {
		if (gotMonthName)
			return false;

		if (gotDay)
			return false;

		gotDay = true;
		gotMonth = false;
		day = month;
	}

	if (gotYear || gotDay) {
		// Month field in yyyy-mm-dd or dd-mm-yyyy
		// Check month name
		std::wstring dateMonth = token.GetString().substr(pos + 1, pos2 - pos - 1);
		if (!GetMonthFromName(dateMonth, month))
			return false;
		gotMonth = true;
	}
	else {
		int64_t value = token.GetNumber(pos + 1, pos2 - pos - 1);
		// Day field in mm-dd-yyyy
		if (value < 1 || value > 31)
			return false;
		day = value;
		gotDay = true;
	}

	int64_t value = token.GetNumber(pos2 + 1, token.GetLength() - pos2 - 1);
	if (gotYear) {
		// Day field in yyy-mm-dd
		if (value <= 0 || value > 31)
			return false;
		day = value;
		gotDay = true;
	}
	else {
		if (value < 0 || value > 9999)
			return false;

		if (value < 50)
			value += 2000;
		else if (value < 1000)
			value += 1900;
		year = value;

		gotYear = true;
	}

	assert(gotYear);
	assert(gotMonth);
	assert(gotDay);

	if (!entry.time.set(fz::datetime::utc, year, month, day)) {
		return false;
	}

	return true;
}

bool CDirectoryListingParser::ParseAsDos(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// Get first token, has to be a valid date
	if (!line.GetToken(index, token)) {
		return false;
	}

	entry.flags = 0;

	if (!ParseShortDate(token, entry)) {
		return false;
	}

	// Extract time
	if (!line.GetToken(++index, token)) {
		return false;
	}

	if (!ParseTime(token, entry)) {
		return false;
	}

	// If next token is <DIR>, entry is a directory
	// else, it should be the filesize.
	if (!line.GetToken(++index, token))
		return false;

	if (token.GetString() == L"<DIR>") {
		entry.flags |= CDirentry::flag_dir;
		entry.size = -1;
	}
	else if (token.IsNumeric() || token.IsLeftNumeric()) {
		// Convert size, filter out separators
		int64_t size = 0;
		int len = token.GetLength();
		for (int i = 0; i < len; ++i) {
			auto const chr = token[i];
			if (chr == ',' || chr == '.') {
				continue;
			}
			if (chr < '0' || chr > '9') {
				return false;
			}

			size *= 10;
			size += chr - '0';
		}
		entry.size = size;
	}
	else {
		return false;
	}

	// Extract filename
	if (!line.GetToken(++index, token, true)) {
		return false;
	}
	entry.name = token.GetString();

	entry.target.clear();
	entry.ownerGroup = objcache.get(std::wstring());
	entry.permissions = entry.ownerGroup;
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseTime(CToken &token, CDirentry &entry)
{
	if (!entry.has_date())
		return false;

	int pos = token.Find(':');
	if (pos < 1 || static_cast<unsigned int>(pos) >= (token.GetLength() - 1))
		return false;

	int64_t hour = token.GetNumber(0, pos);
	if (hour < 0 || hour > 24)
		return false;

	// See if we got seconds
	int pos2 = token.Find(':', pos + 1);
	int len;
	if (pos2 == -1)
		len = -1;
	else
		len = pos2 - pos - 1;

	if (!len)
		return false;

	int64_t minute = token.GetNumber(pos + 1, len);
	if (minute < 0 || minute > 59)
		return false;

	int64_t seconds = -1;
	if (pos2 != -1) {
		// Parse seconds
		seconds = token.GetNumber(pos2 + 1, -1);
		if (seconds < 0 || seconds > 60)
			return false;
	}

	// Convert to 24h format
	if (!token.IsRightNumeric()) {
		if (token[token.GetLength() - 2] == 'P') {
			if (hour < 12)
				hour += 12;
		}
		else
			if (hour == 12)
				hour = 0;
	}

	return entry.time.imbue_time(hour, minute, seconds);
}

bool CDirectoryListingParser::ParseAsEplf(CLine &line, CDirentry &entry)
{
	CToken token;
	if (!line.GetToken(0, token, true))
		return false;

	if (token[0] != '+')
		return false;

	int pos = token.Find('\t');
	if (pos == -1 || static_cast<size_t>(pos) == (token.GetLength() - 1))
		return false;

	entry.name = token.GetString().substr(pos + 1);

	entry.flags = 0;
	entry.size = -1;

	std::wstring permissions;

	int fact = 1;
	while (fact < pos) {
		int separator = token.Find(',', fact);
		int len;
		if (separator == -1) {
			len = pos - fact;
		}
		else {
			len = separator - fact;
		}

		if (!len) {
			++fact;
			continue;
		}

		auto const type = token[fact];

		if (type == '/') {
			entry.flags |= CDirentry::flag_dir;
		}
		else if (type == 's') {
			entry.size = token.GetNumber(fact + 1, len - 1);
		}
		else if (type == 'm') {
			int64_t number = token.GetNumber(fact + 1, len - 1);
			if (number < 0) {
				return false;
			}
			entry.time = fz::datetime(static_cast<time_t>(number), fz::datetime::seconds);
		}
		else if (type == 'u' && len > 2 && token[fact + 1] == 'p') {
			permissions = token.GetString().substr(fact + 2, len - 2);
		}

		fact += len + 1;
	}

	entry.permissions = objcache.get(permissions);
	entry.ownerGroup = objcache.get(std::wstring());
	return true;
}

namespace {
std::wstring Unescape(const std::wstring& str, wchar_t escape)
{
	std::wstring res;
	for (unsigned int i = 0; i < str.size(); ++i) {
		wchar_t c = str[i];
		if (c == escape) {
			++i;
			if (i == str.size() || !str[i]) {
				break;
			}
			c = str[i];
		}
		res += c;
	}

	return res;
}
}

bool CDirectoryListingParser::ParseAsVms(CLine &line, CDirentry &entry)
{
	CToken token;
	int index = 0;

	if (!line.GetToken(index, token))
		return false;

	int pos = token.Find(';');
	if (pos == -1)
		return false;

	entry.flags = 0;

	if (pos > 4 && token.GetString().substr(pos - 4, 4) == L".DIR") {
		entry.flags |= CDirentry::flag_dir;
		if (token.GetString().substr(pos) == L";1")
			entry.name = token.GetString().substr(0, pos - 4);
		else
			entry.name = token.GetString().substr(0, pos - 4) + token.GetString().substr(pos);
	}
	else
		entry.name = token.GetString();

	// Some VMS servers escape special characters like additional dots with ^
	entry.name = Unescape(entry.name, '^');

	if (!line.GetToken(++index, token))
		return false;

	std::wstring ownerGroup;
	std::wstring permissions;

	// This field can either be the filesize, a username (at least that's what I think) enclosed in [] or a date.
	if (!token.IsNumeric() && !token.IsLeftNumeric()) {
		// Must be username
		const int len = token.GetLength();
		if (len < 3 || token[0] != '[' || token[len - 1] != ']')
			return false;
		ownerGroup = token.GetString().substr(1, len - 2);

		if (!line.GetToken(++index, token))
			return false;
		if (!token.IsNumeric() && !token.IsLeftNumeric())
			return false;
	}

	// Current token is either size or date
	bool gotSize = false;
	pos = token.Find('/');

	if (!pos)
		return false;

	if (token.IsNumeric() || (pos != -1 && token.Find('/', pos + 1) == -1)) {
		// Definitely size
		CToken sizeToken;
		if (pos == -1)
			sizeToken = token;
		else
			sizeToken = CToken(token.GetToken(), pos);
		if (!ParseComplexFileSize(sizeToken, entry.size, 512))
			return false;
		gotSize = true;

		if (!line.GetToken(++index, token))
			return false;
	}
	else if (pos == -1 && token.IsLeftNumeric()) {
		// Perhaps size
		if (ParseComplexFileSize(token, entry.size, 512)) {
			gotSize = true;

			if (!line.GetToken(++index, token))
				return false;
		}
	}

	// Get date
	if (!ParseShortDate(token, entry))
		return false;

	// Get time
	if (!line.GetToken(++index, token))
		return true;

	if (!ParseTime(token, entry)) {
		int len = token.GetLength();
		if (token[0] == '[' && token[len - 1] != ']')
			return false;
		if (token[0] == '(' && token[len - 1] != ')')
			return false;
		if (token[0] != '[' && token[len - 1] == ']')
			return false;
		if (token[0] != '(' && token[len - 1] == ')')
			return false;
		--index;
	}

	if (!gotSize) {
		// Get size
		if (!line.GetToken(++index, token))
			return false;

		if (!token.IsNumeric() && !token.IsLeftNumeric())
			return false;

		pos = token.Find('/');
		if (!pos)
			return false;

		CToken sizeToken;
		if (pos == -1)
			sizeToken = token;
		else
			sizeToken = CToken(token.GetToken(), pos);
		if (!ParseComplexFileSize(sizeToken, entry.size, 512))
			return false;
	}

	// Owner / group and permissions
	while (line.GetToken(++index, token)) {
		const int len = token.GetLength();
		if (len > 2 && token[0] == '(' && token[len - 1] == ')') {
			if (!permissions.empty())
				permissions += L" ";
			permissions += token.GetString().substr(1, len - 2);
		}
		else if (len > 2 && token[0] == '[' && token[len - 1] == ']') {
			if (!ownerGroup.empty())
				ownerGroup += L" ";
			ownerGroup += token.GetString().substr(1, len - 2);
		}
		else {
			if (!ownerGroup.empty())
				ownerGroup += L" ";
			ownerGroup += token.GetString();
		}
	}
	entry.permissions = objcache.get(permissions);
	entry.ownerGroup = objcache.get(ownerGroup);

	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsIbm(CLine &line, CDirentry &entry)
{
	int index = 0;

	// Get owner
	CToken ownerGroupToken;
	if (!line.GetToken(index, ownerGroupToken))
		return false;

	// Get size
	CToken token;
	if (!line.GetToken(++index, token))
		return false;

	if (!token.IsNumeric())
		return false;

	entry.size = token.GetNumber();

	// Get date
	if (!line.GetToken(++index, token))
		return false;

	entry.flags = 0;

	if (!ParseShortDate(token, entry))
		return false;

	// Get time
	if (!line.GetToken(++index, token))
		return false;

	if (!ParseTime(token, entry))
		return false;

	// Get filename
	if (!line.GetToken(index + 2, token, 1))
		return false;

	entry.name = token.GetString();
	if (token[token.GetLength() - 1] == '/') {
		entry.name.pop_back();
		entry.flags |= CDirentry::flag_dir;
	}

	entry.ownerGroup = objcache.get(ownerGroupToken.GetString());
	entry.permissions = objcache.get(std::wstring());

	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseOther(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken firstToken;

	if (!line.GetToken(index, firstToken)) {
		return false;
	}

	if (!firstToken.IsNumeric()) {
		return false;
	}

	// Possible formats: Numerical unix, VShell or OS/2

	CToken token;
	if (!line.GetToken(++index, token)) {
		return false;
	}

	entry.flags = 0;

	// If token is a number, than it's the numerical Unix style format,
	// else it's the VShell, OS/2 or nortel.VxWorks format
	if (token.IsNumeric()) {
		if (firstToken.GetLength() >= 2 && firstToken[1] == '4') {
			entry.flags |= CDirentry::flag_dir;
		}

		std::wstring ownerGroup = token.GetString();

		if (!line.GetToken(++index, token)) {
			return false;
		}

		ownerGroup += L" " + token.GetString();

		// Get size
		if (!line.GetToken(++index, token)) {
			return false;
		}

		if (!token.IsNumeric()) {
			return false;
		}

		entry.size = token.GetNumber();

		// Get date/time
		if (!line.GetToken(++index, token)) {
			return false;
		}

		int64_t number = token.GetNumber();
		if (number < 0) {
			return false;
		}
		entry.time = fz::datetime(static_cast<time_t>(number), fz::datetime::seconds);

		// Get filename
		if (!line.GetToken(++index, token, true)) {
			return false;
		}

		entry.name = token.GetString();
		entry.target.clear();

		entry.permissions = objcache.get(firstToken.GetString());
		entry.ownerGroup = objcache.get(ownerGroup);
	}
	else {
		// Possible conflict with multiline VMS listings
		if (m_maybeMultilineVms) {
			return false;
		}

		// VShell, OS/2 or nortel.VxWorks style format
		entry.size = firstToken.GetNumber();

		// Get date
		std::wstring dateMonth = token.GetString();
		int month = 0;
		if (!GetMonthFromName(dateMonth, month)) {
			// OS/2 or nortel.VxWorks
			int skippedCount = 0;
			do {
				if (token.GetString() == L"DIR") {
					entry.flags |= CDirentry::flag_dir;
				}
				else if (token.Find(L"-/.") != -1) {
					break;
				}

				++skippedCount;

				if (!line.GetToken(++index, token)) {
					return false;
				}
			} while (true);

			if (!ParseShortDate(token, entry)) {
				return false;
			}

			// Get time
			if (!line.GetToken(++index, token)) {
				return false;
			}

			if (!ParseTime(token, entry)) {
				return false;
			}

			// Get filename
			if (!line.GetToken(++index, token, true)) {
				return false;
			}

			entry.name = token.GetString();
			if (entry.name.size() >= 5) {
				std::wstring type = fz::str_tolower_ascii(entry.name.substr(entry.name.size() - 5));
				if (!skippedCount && type == L"<dir>") {
					entry.flags |= CDirentry::flag_dir;
					entry.name = entry.name.substr(0, entry.name.size() - 5);
					while (!entry.name.empty() && entry.name.back() == ' ') {
						entry.name.pop_back();
					}
				}
			}
		}
		else {
			// Get day
			if (!line.GetToken(++index, token)) {
				return false;
			}

			if (!token.IsNumeric() && !token.IsLeftNumeric()) {
				return false;
			}

			int64_t day = token.GetNumber();
			if (day < 0 || day > 31) {
				return false;
			}

			// Get Year
			if (!line.GetToken(++index, token)) {
				return false;
			}

			if (!token.IsNumeric()) {
				return false;
			}

			int64_t year = token.GetNumber();
			if (year < 50) {
				year += 2000;
			}
			else if (year < 1000) {
				year += 1900;
			}

			if (!entry.time.set(fz::datetime::utc, year, month, day)) {
				return false;
			}

			// Get time
			if (!line.GetToken(++index, token)) {
				return false;
			}

			if (!ParseTime(token, entry)) {
				return false;
			}

			// Get filename
			if (!line.GetToken(++index, token, 1)) {
				return false;
			}

			entry.name = token.GetString();
			auto const chr = token[token.GetLength() - 1];
			if (chr == '/' || chr == '\\') {
				entry.flags |= CDirentry::flag_dir;
				entry.name.pop_back();
			}
		}
		entry.target.clear();
		entry.ownerGroup = objcache.get(std::wstring());
		entry.permissions = entry.ownerGroup;
		entry.time += m_timezoneOffset;
	}

	return true;
}

bool CDirectoryListingParser::AddData(char *pData, int len)
{
	ConvertEncoding(pData, len);

	m_DataList.emplace_back(pData, len);
	m_totalData += len;

	if (m_totalData < 512) {
		return true;
	}

	return ParseData(true);
}

bool CDirectoryListingParser::AddLine(std::wstring && line, std::wstring && name, fz::datetime const& time)
{
	if (m_pControlSocket) {
		m_pControlSocket->LogMessageRaw(MessageType::RawList, line);
	}

	CDirentry override;
	override.name = std::move(name);
	override.time = time;
	CLine l(std::move(line));
	ParseLine(l, m_server.GetType(), true, &override);

	return true;
}

CLine *CDirectoryListingParser::GetLine(bool breakAtEnd, bool &error)
{
	while (!m_DataList.empty()) {
		// Trim empty lines and spaces
		auto iter = m_DataList.begin();
		int len = iter->len;
		while (iter->p[m_currentOffset] == '\r' || iter->p[m_currentOffset] == '\n'
			|| iter->p[m_currentOffset] == ' ' || iter->p[m_currentOffset] == '\t'
			|| !iter->p[m_currentOffset])
		{
			++m_currentOffset;
			if (m_currentOffset >= len) {
				delete [] iter->p;
				++iter;
				m_currentOffset = 0;
				if (iter == m_DataList.end()) {
					m_DataList.clear();
					return 0;
				}
				len = iter->len;
			}
		}
		m_DataList.erase(m_DataList.begin(), iter);
		iter = m_DataList.begin();

		// Remember start offset and find next linebreak
		int startpos = m_currentOffset;
		int reslen = 0;

		int currentOffset = m_currentOffset;
		while (iter->p[currentOffset] != '\n' && iter->p[currentOffset] != '\r' && iter->p[currentOffset]) {
			++reslen;

			++currentOffset;
			if (currentOffset >= len) {
				++iter;
				if (iter == m_DataList.end()) {
					if (reslen > 10000) {
						if (m_pControlSocket) {
							m_pControlSocket->LogMessage(MessageType::Error, _("Received a line exceeding 10000 characters, aborting."));
						}
						error = true;
						return 0;
					}
					if (breakAtEnd) {
						return 0;
					}
					break;
				}
				len = iter->len;
				currentOffset = 0;
			}
		}

		if (reslen > 10000) {
			if (m_pControlSocket) {
				m_pControlSocket->LogMessage(MessageType::Error, _("Received a line exceeding 10000 characters, aborting."));
			}
			error = true;
			return 0;
		}
		m_currentOffset = currentOffset;

		// Reslen is now the length of the line, including any terminating whitespace
		int const buflen = reslen;
		char *res = new char[buflen + 1];
		res[buflen] = 0;

		int respos = 0;

		// Copy line data
		auto i = m_DataList.begin();
		while (i != iter && reslen) {
			int copylen = i->len - startpos;
			if (copylen > reslen) {
				copylen = reslen;
			}
			memcpy(&res[respos], &i->p[startpos], copylen);
			reslen -= copylen;
			respos += i->len - startpos;
			startpos = 0;

			delete [] i->p;
			++i;
		};

		// Copy last chunk
		if (iter != m_DataList.end() && reslen) {
			int copylen = m_currentOffset-startpos;
			if (copylen > reslen) {
				copylen = reslen;
			}
			memcpy(&res[respos], &iter->p[startpos], copylen);
			if (reslen >= iter->len) {
				delete [] iter->p;
				m_DataList.erase(m_DataList.begin(), ++iter);
			}
			else {
				m_DataList.erase(m_DataList.begin(), iter);
			}
		}
		else {
			m_DataList.erase(m_DataList.begin(), iter);
		}

		std::wstring buffer;
		if (m_pControlSocket) {
			buffer = m_pControlSocket->ConvToLocal(res, buflen);
			m_pControlSocket->LogMessageRaw(MessageType::RawList, buffer);
		}
		else {
			buffer = fz::to_wstring_from_utf8(res);
			if (buffer.empty()) {
				buffer = fz::to_wstring(res);
				if (buffer.empty()) {
					buffer = std::wstring(res, res + strlen(res));
				}
			}
		}
		delete [] res;

		// Strip BOM
		if (buffer[0] == 0xfeff) {
			buffer = buffer.substr(1);
		}

		if (!buffer.empty()) {
			return new CLine(std::move(buffer));
		}
	}

	return 0;
}

bool CDirectoryListingParser::ParseAsWfFtp(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// Get filename
	if (!line.GetToken(index++, token))
		return false;

	entry.name = token.GetString();

	// Get filesize
	if (!line.GetToken(index++, token))
		return false;

	if (!token.IsNumeric())
		return false;

	entry.size = token.GetNumber();

	entry.flags = 0;

	// Parse date
	if (!line.GetToken(index++, token))
		return false;

	if (!ParseShortDate(token, entry))
		return false;

	// Unused token
	if (!line.GetToken(index++, token))
		return false;

	if (token.GetString().back() != '.')
		return false;

	// Parse time
	if (!line.GetToken(index++, token, true))
		return false;

	if (!ParseTime(token, entry))
		return false;

	entry.ownerGroup = objcache.get(std::wstring());
	entry.permissions = entry.ownerGroup;
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// volume
	if (!line.GetToken(index++, token))
		return false;

	// unit
	if (!line.GetToken(index++, token))
		return false;

	// Referred date
	if (!line.GetToken(index++, token))
		return false;

	entry.flags = 0;
	if (token.GetString() != L"**NONE**" && !ParseShortDate(token, entry)) {
		// Perhaps of the following type:
		// TSO004 3390 VSAM FOO.BAR
		if (token.GetString() != L"VSAM")
			return false;

		if (!line.GetToken(index++, token))
			return false;

		entry.name = token.GetString();
		if (entry.name.find(' ') != std::wstring::npos)
			return false;

		entry.size = -1;
		entry.ownerGroup = objcache.get(std::wstring());
		entry.permissions = entry.ownerGroup;

		return true;
	}

	// ext
	if (!line.GetToken(index++, token))
		return false;
	if (!token.IsNumeric())
		return false;

	int prevLen = token.GetLength();

	// used
	if (!line.GetToken(index++, token))
		return false;
	if (token.IsNumeric() || token.GetString() == L"????" || token.GetString() == L"++++" ) {
		// recfm
		if (!line.GetToken(index++, token))
			return false;
		if (token.IsNumeric())
			return false;
	}
	else {
		if (prevLen < 6)
			return false;
	}

	// lrecl
	if (!line.GetToken(index++, token))
		return false;
	if (!token.IsNumeric())
		return false;

	// blksize
	if (!line.GetToken(index++, token))
		return false;
	if (!token.IsNumeric())
		return false;

	// dsorg
	if (!line.GetToken(index++, token))
		return false;

	if (token.GetString() == L"PO" || token.GetString() == L"PO-E")
	{
		entry.flags |= CDirentry::flag_dir;
		entry.size = -1;
	}
	else
		entry.size = 100;

	// name of dataset or sequential file
	if (!line.GetToken(index++, token, true))
		return false;

	entry.name = token.GetString();

	entry.ownerGroup = objcache.get(std::wstring());
	entry.permissions = entry.ownerGroup;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_PDS(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// pds member name
	if (!line.GetToken(index++, token))
		return false;
	entry.name = token.GetString();

	// vv.mm
	if (!line.GetToken(index++, token))
		return false;

	entry.flags = 0;

	// creation date
	if (!line.GetToken(index++, token))
		return false;
	if (!ParseShortDate(token, entry))
		return false;

	// modification date
	if (!line.GetToken(index++, token))
		return false;
	if (!ParseShortDate(token, entry))
		return false;

	// modification time
	if (!line.GetToken(index++, token))
		return false;
	if (!ParseTime(token, entry))
		return false;

	// size
	if (!line.GetToken(index++, token))
		return false;
	if (!token.IsNumeric())
		return false;
	entry.size = token.GetNumber();

	// init
	if (!line.GetToken(index++, token))
		return false;
	if (!token.IsNumeric())
		return false;

	// mod
	if (!line.GetToken(index++, token))
		return false;
	if (!token.IsNumeric())
		return false;

	// id
	if (!line.GetToken(index++, token, true))
		return false;

	entry.ownerGroup = objcache.get(std::wstring());
	entry.permissions = entry.ownerGroup;
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_Migrated(CLine &line, CDirentry &entry)
{
	// Migrated MVS file
	// "Migrated				SOME.NAME"

	int index = 0;
	CToken token;
	if (!line.GetToken(index, token))
		return false;

	std::wstring s = fz::str_tolower_ascii(token.GetString());
	if (s != L"migrated")
		return false;

	if (!line.GetToken(++index, token))
		return false;

	entry.name = token.GetString();

	if (line.GetToken(++index, token))
		return false;

	entry.flags = 0;
	entry.size = -1;
	entry.ownerGroup = objcache.get(std::wstring());
	entry.permissions = entry.ownerGroup;

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_PDS2(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;
	if (!line.GetToken(index, token))
		return false;

	entry.name = token.GetString();

	entry.flags = 0;
	entry.ownerGroup = objcache.get(std::wstring());
	entry.permissions = entry.ownerGroup;
	entry.size = -1;

	if (!line.GetToken(++index, token))
		return true;

	entry.size = token.GetNumber(CToken::hex);
	if (entry.size == -1)
		return false;

	// Unused hexadecimal token
	if (!line.GetToken(++index, token))
		return false;
	if (!token.IsNumeric(CToken::hex))
		return false;

	// Unused numeric token
	if (!line.GetToken(++index, token))
		return false;
	if (!token.IsNumeric())
		return false;

	int start = ++index;
	while (line.GetToken(index, token)) {
		++index;
	}
	if ((index - start < 2))
		return false;
	--index;

	if (!line.GetToken(index, token)) {
		return false;
	}
	if (!token.IsNumeric() && (token.GetString() != L"ANY"))
		return false;

	if (!line.GetToken(index - 1, token)) {
		return false;
	}
	if (!token.IsNumeric() && (token.GetString() != L"ANY"))
		return false;

	for (int i = start; i < index - 1; ++i) {
		if (!line.GetToken(i, token)) {
			return false;
		}
		int len = token.GetLength();
		for (int j = 0; j < len; ++j)
			if (token[j] < 'A' || token[j] > 'Z')
				return false;
	}

	return true;
}

bool CDirectoryListingParser::ParseAsIBM_MVS_Tape(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// volume
	if (!line.GetToken(index++, token))
		return false;

	// unit
	if (!line.GetToken(index++, token))
		return false;

	std::wstring s = fz::str_tolower_ascii(token.GetString());
	if (s != L"tape")
		return false;

	// dsname
	if (!line.GetToken(index++, token))
		return false;

	entry.name = token.GetString();
	entry.flags = 0;
	entry.ownerGroup = objcache.get(std::wstring());
	entry.permissions = objcache.get(std::wstring());
	entry.size = -1;

	if (line.GetToken(index++, token))
		return false;

	return true;
}

bool CDirectoryListingParser::ParseComplexFileSize(CToken& token, int64_t& size, int blocksize /*=-1*/)
{
	if (token.IsNumeric()) {
		size = token.GetNumber();
		if (blocksize != -1) {
			size *= blocksize;
		}

		return true;
	}

	int len = token.GetLength();

	auto last = token[len - 1];
	if (last == 'B' || last == 'b') {
		if (len == 1) {
			return false;
		}

		auto const c = token[--len - 1];
		if (c < '0' || c > '9') {
			--len;
			last = c;
		}
		else {
			last = 0;
		}
	}
	else if (last >= '0' && last <= '9') {
		last = 0;
	}
	else {
		if (--len == 0) {
			return false;
		}
	}

	size = 0;

	int dot = -1;
	for (int i = 0; i < len; ++i) {
		auto const c = token[i];
		if (c >= '0' && c <= '9') {
			size *= 10;
			size += c - '0';
		}
		else if (c == '.') {
			if (dot != -1) {
				return false;
			}
			dot = len - i - 1;
		}
		else {
			return false;
		}
	}
	switch (last)
	{
	case 'k':
	case 'K':
		size *= 1024;
		break;
	case 'm':
	case 'M':
		size *= 1024 * 1024;
		break;
	case 'g':
	case 'G':
		size *= 1024 * 1024 * 1024;
		break;
	case 't':
	case 'T':
		size *= 1024 * 1024;
		size *= 1024 * 1024;
		break;
	case 'b':
	case 'B':
		break;
	case 0:
		if (blocksize != -1) {
			size *= blocksize;
		}
		break;
	default:
		return false;
	}
	while (dot-- > 0) {
		size /= 10;
	}

	return true;
}

int CDirectoryListingParser::ParseAsMlsd(CLine &line, CDirentry &entry)
{
	// MLSD format as described here: http://www.ietf.org/internet-drafts/draft-ietf-ftpext-mlst-16.txt

	// Parsing is done strict, abort on slightest error.

	CToken token;

	if (!line.GetToken(0, token)) {
		return 0;
	}

	std::wstring const facts = token.GetString();
	if (facts.empty()) {
		return 0;
	}

	entry.flags = 0;
	entry.size = -1;
	entry.time.clear();
	entry.target.clear();

	std::wstring owner, ownername, group, groupname, user, uid, gid;
	std::wstring ownerGroup;
	std::wstring permissions;

	size_t start = 0;
	while (start < facts.size()) {
		auto delim = facts.find(';', start);
		if (delim == std::wstring::npos) {
			delim = facts.size();
		}
		else if (delim < start + 3) {
			return 0;
		}

		auto const pos = facts.find('=', start);
		if (pos == std::wstring::npos || pos < start + 1 || pos > delim) {
			return 0;
		}

		std::wstring factname = fz::str_tolower_ascii(facts.substr(start, pos - start));
		std::wstring value = facts.substr(pos + 1, delim - pos - 1);
		if (factname == L"type") {
			auto colonPos = value.find(':');
			std::wstring valuePrefix;
			if (colonPos == std::wstring::npos) {
				valuePrefix = fz::str_tolower_ascii(value);
			}
			else {
				valuePrefix = fz::str_tolower_ascii(value.substr(0, colonPos));
			}

			if (valuePrefix == L"dir" && colonPos == std::wstring::npos) {
				entry.flags |= CDirentry::flag_dir;
			}
			else if (valuePrefix == L"os.unix=slink" || valuePrefix == L"os.unix=symlink") {
				entry.flags |= CDirentry::flag_dir | CDirentry::flag_link;
				if (colonPos != std::wstring::npos) {
					entry.target = fz::sparse_optional<std::wstring>(value.substr(colonPos));
				}
			}
			else if ((valuePrefix == L"cdir" || valuePrefix == L"pdir") && colonPos == std::wstring::npos) {
				// Current and parent directory, don't parse it
				return 2;
			}
		}
		else if (factname == L"size") {
			entry.size = 0;

			for (unsigned int i = 0; i < value.size(); ++i) {
				if (value[i] < '0' || value[i] > '9') {
					return 0;
				}
				entry.size *= 10;
				entry.size += value[i] - '0';
			}
		}
		else if (factname == L"modify" ||
			(!entry.has_date() && factname == L"create"))
		{
			entry.time = fz::datetime(value, fz::datetime::utc);
			if (entry.time.empty()) {
				return 0;
			}
		}
		else if (factname == L"perm") {
			if (!value.empty()) {
				if (!permissions.empty()) {
					permissions = value + L" (" + permissions + L")";
				}
				else {
					permissions = value;
				}
			}
		}
		else if (factname == L"unix.mode") {
			if (!permissions.empty()) {
				permissions = permissions + L" (" + value + L")";
			}
			else {
				permissions = value;
			}
		}
		else if (factname == L"unix.owner") {
			owner = value;
		}
		else if (factname == L"unix.ownername") {
			ownername = value;
		}
		else if (factname == L"unix.group") {
			group = value;
		}
		else if (factname == L"unix.groupname") {
			groupname = value;
		}
		else if (factname == L"unix.user") {
			user = value;
		}
		else if (factname == L"unix.uid") {
			uid = value;
		}
		else if (factname == L"unix.gid") {
			gid = value;
		}

		start = delim + 1;
	}

	// The order of the facts is undefined, so assemble ownerGroup in correct
	// order
	if (!ownername.empty()) {
		ownerGroup = ownername;
	}
	else if (!owner.empty()) {
		ownerGroup = owner;
	}
	else if (!user.empty()) {
		ownerGroup = user;
	}
	else if (!uid.empty()) {
		ownerGroup = uid;
	}

	if (!groupname.empty()) {
		ownerGroup += L" " + groupname;
	}
	else if (!group.empty()) {
		ownerGroup += L" " + group;
	}
	else if (!gid.empty()) {
		ownerGroup += L" " + gid;
	}

	if (!line.GetToken(1, token, true, true)) {
		return 0;
	}

	entry.name = token.GetString();
	entry.ownerGroup = objcache.get(ownerGroup);
	entry.permissions = objcache.get(permissions);

	return 1;
}

bool CDirectoryListingParser::ParseAsOS9(CLine &line, CDirentry &entry)
{
	int index = 0;

	// Get owner
	CToken ownerGroupToken;
	if (!line.GetToken(index++, ownerGroupToken))
		return false;

	// Make sure it's number.number
	int pos = ownerGroupToken.Find('.');
	if (pos == -1 || !pos || pos == ((int)ownerGroupToken.GetLength() - 1))
		return false;

	if (!ownerGroupToken.IsNumeric(0, pos))
		return false;

	if (!ownerGroupToken.IsNumeric(pos + 1, ownerGroupToken.GetLength() - pos - 1))
		return false;

	entry.flags = 0;

	// Get date
	CToken token;
	if (!line.GetToken(index++, token))
		return false;

	if (!ParseShortDate(token, entry, true))
		return false;

	// Unused token
	if (!line.GetToken(index++, token))
		return false;

	// Get perms
	CToken permToken;
	if (!line.GetToken(index++, permToken))
		return false;

	if (permToken[0] == 'd')
		entry.flags |= CDirentry::flag_dir;

	// Unused token
	if (!line.GetToken(index++, token))
		return false;

	// Get Size
	if (!line.GetToken(index++, token))
		return false;

	if (!token.IsNumeric())
		return false;

	entry.size = token.GetNumber();

	// Filename
	if (!line.GetToken(index++, token, true))
		return false;

	entry.name = token.GetString();
	entry.ownerGroup = objcache.get(ownerGroupToken.GetString());
	entry.permissions = objcache.get(permToken.GetString());

	return true;
}

void CDirectoryListingParser::Reset()
{
	for (auto iter = m_DataList.begin(); iter != m_DataList.end(); ++iter)
		delete [] iter->p;
	m_DataList.clear();

	delete m_prevLine;
	m_prevLine = 0;

	m_entryList.clear();
	m_fileList.clear();
	m_currentOffset = 0;
	m_fileListOnly = true;
	m_maybeMultilineVms = false;
}

bool CDirectoryListingParser::ParseAsZVM(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// Get name
	if (!line.GetToken(index, token))
		return false;

	entry.name = token.GetString();

	// Get filename extension
	if (!line.GetToken(++index, token))
		return false;
	entry.name += L"." + token.GetString();

	// File format. Unused
	if (!line.GetToken(++index, token))
		return false;
	std::wstring format = token.GetString();
	if (format != L"V" && format != L"F")
		return false;

	// Record length
	if (!line.GetToken(++index, token))
		return false;

	if (!token.IsNumeric())
		return false;

	entry.size = token.GetNumber();

	// Number of records
	if (!line.GetToken(++index, token))
		return false;

	if (!token.IsNumeric())
		return false;

	entry.size *= token.GetNumber();

	// Unused (Block size?)
	if (!line.GetToken(++index, token))
		return false;

	if (!token.IsNumeric())
		return false;

	entry.flags = 0;

	// Date
	if (!line.GetToken(++index, token))
		return false;

	if (!ParseShortDate(token, entry, true))
		return false;

	// Time
	if (!line.GetToken(++index, token))
		return false;

	if (!ParseTime(token, entry))
		return false;

	// Owner
	CToken ownerGroupToken;
	if (!line.GetToken(++index, ownerGroupToken))
		return false;

	// No further token!
	if (line.GetToken(++index, token))
		return false;

	entry.ownerGroup = objcache.get(ownerGroupToken.GetString());
	entry.permissions = objcache.get(std::wstring());
	entry.target.clear();
	entry.time += m_timezoneOffset;

	return true;
}

bool CDirectoryListingParser::ParseAsHPNonstop(CLine &line, CDirentry &entry)
{
	int index = 0;
	CToken token;

	// Get name
	if (!line.GetToken(index, token))
		return false;

	entry.name = token.GetString();

	// File code, numeric, unsuded
	if (!line.GetToken(++index, token))
		return false;
	if (!token.IsNumeric())
		return false;

	// Size
	if (!line.GetToken(++index, token))
		return false;
	if (!token.IsNumeric())
		return false;

	entry.size = token.GetNumber();

	entry.flags = 0;

	// Date
	if (!line.GetToken(++index, token))
		return false;
	if (!ParseShortDate(token, entry, false))
		return false;

	// Time
	if (!line.GetToken(++index, token))
		return false;
	if (!ParseTime(token, entry))
		return false;

	// Owner
	if (!line.GetToken(++index, token))
		return false;
	std::wstring ownerGroup = token.GetString();

	if (token[token.GetLength() - 1] == ',') {
		// Owner, part 2
		if (!line.GetToken(++index, token))
			return false;
		ownerGroup += L" " + token.GetString();
	}

	// Permissions
	CToken permToken;
	if (!line.GetToken(++index, permToken))
		return false;

	// Nothing
	if (line.GetToken(++index, token))
		return false;

	entry.permissions = objcache.get(permToken.GetString());
	entry.ownerGroup = objcache.get(ownerGroup);

	return true;
}

bool CDirectoryListingParser::GetMonthFromName(const std::wstring& name, int &month)
{
	std::wstring lower = fz::str_tolower_ascii(name);
	auto iter = m_MonthNamesMap.find(lower);
	if (iter == m_MonthNamesMap.end())
		return false;

	month = iter->second;

	return true;
}

char ebcdic_table[256] = {
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 0
	' ',  ' ',  ' ',  ' ',  ' ',  '\n', ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '\n', // 1
	' ',  ' ',  ' ',  ' ',  ' ',  '\n', ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 2
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 3
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '.',  '<',  '(',  '+',  '|',  // 4
	'&',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '!',  '$',  '*',  ')',  ';',  ' ',  // 5
	'-',  '/',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '|',  ',',  '%',  '_',  '>',  '?',  // 6
	' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '`',  ':',  '#',  '@',  '\'', '=',  '"',  // 7
	' ',  'a',  'b',  'c',  'd',  'e',  'f',  'g',  'h',  'i',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 8
	' ',  'j',  'k',  'l',  'm',  'n',  'o',  'p',  'q',  'r',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // 9
	' ',  '~',  's',  't',  'u',  'v',  'w',  'x',  'y',  'z',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // a
	'^',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  '[',  ']',  ' ',  ' ',  ' ',  ' ',  // b
	'{',  'A',  'B',  'C',  'D',  'E',  'F',  'G',  'H',  'I',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // c
	'}',  'J',  'K',  'L',  'M',  'N',  'O',  'P',  'Q',  'R',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // d
	'\\', ' ',  'S',  'T',  'U',  'V',  'W',  'X',  'Y',  'Z',  ' ',  ' ',  ' ',  ' ',  ' ',  ' ',  // e
	'0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  ' ',  ' ',  ' ',  ' ',  ' ',  ' '   // f
};

void CDirectoryListingParser::ConvertEncoding(char *pData, int len)
{
	if (m_listingEncoding != listingEncoding::ebcdic) {
		return;
	}

	for (int i = 0; i < len; ++i) {
		pData[i] = ebcdic_table[static_cast<unsigned char>(pData[i])];
	}
}

void CDirectoryListingParser::DeduceEncoding()
{
	if (m_listingEncoding != listingEncoding::unknown) {
		return;
	}

	int count[256];

	memset(&count, 0, sizeof(int)*256);

	for (auto const& data : m_DataList) {
		for (int i = 0; i < data.len; ++i) {
			++count[static_cast<unsigned char>(data.p[i])];
		}
	}

	int count_normal = 0;
	int count_ebcdic = 0;
	for (int i = '0'; i <= '9'; ++i) {
		count_normal += count[i];
	}
	for (int i = 'a'; i <= 'z'; ++i) {
		count_normal += count[i];
	}
	for (int i = 'A'; i <= 'Z'; ++i) {
		count_normal += count[i];
	}

	for (int i = 0x81; i <= 0x89; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0x91; i <= 0x99; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xa2; i <= 0xa9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xc1; i <= 0xc9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xd1; i <= 0xd9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xe2; i <= 0xe9; ++i) {
		count_ebcdic += count[i];
	}
	for (int i = 0xf0; i <= 0xf9; ++i) {
		count_ebcdic += count[i];
	}


	if ((count[0x1f] || count[0x15] || count[0x25]) && !count[0x0a] && count[static_cast<unsigned char>('@')] && count[static_cast<unsigned char>('@')] > count[static_cast<unsigned char>(' ')] && count_ebcdic > count_normal) {
		if (m_pControlSocket) {
			m_pControlSocket->LogMessage(MessageType::Status, _("Received a directory listing which appears to be encoded in EBCDIC."));
		}
		m_listingEncoding = listingEncoding::ebcdic;
		for (auto & data : m_DataList) {
			ConvertEncoding(data.p, data.len);
		}
	}
	else {
		m_listingEncoding = listingEncoding::normal;
	}
}
