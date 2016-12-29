#include <libfilezilla_engine.h>
#include <directorylistingparser.h>

#include <libfilezilla/format.hpp>

#include <cppunit/extensions/HelperMacros.h>
#include <list>

#include <string.h>
/*
 * This testsuite asserts the correctness of the directory listing parser.
 * It's main purpose is to ensure that all known formats are recognized and
 * parsed as expected. Due to the high amount of variety and unfortunately
 * also ambiguity, the parser is very fragile.
 */

struct t_entry
{
	std::string data;
	CDirentry reference;
	ServerType serverType;
};

class CDirectoryListingParserTest final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(CDirectoryListingParserTest);
	InitEntries();
	for (unsigned int i = 0; i < m_entries.size(); ++i) {
		CPPUNIT_TEST(testIndividual);
	}
	CPPUNIT_TEST(testAll);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp();
	void tearDown() {}

	void testIndividual();
	void testAll();
	void testSpecial();

	static std::vector<t_entry> m_entries;

	static fz::mutex m_sync;

protected:
	static void InitEntries();

	t_entry m_entry;
};

fz::mutex CDirectoryListingParserTest::m_sync;
std::vector<t_entry> CDirectoryListingParserTest::m_entries;

CPPUNIT_TEST_SUITE_REGISTRATION(CDirectoryListingParserTest);

typedef fz::shared_value<std::wstring> R;
typedef fz::sparse_optional<std::wstring> O;

static int calcYear(int month, int day)
{
	auto const now = fz::datetime::now();
	auto const tm = now.get_tm(fz::datetime::local);
	int const cur_year = tm.tm_year + 1900;
	int const cur_month = tm.tm_mon + 1;
	int const cur_day = tm.tm_mday;

	// Not exact but good enough for our purpose
	int const day_of_year = month * 31 + day;
	int const cur_day_of_year = cur_month * 31 + cur_day;
	if (day_of_year > (cur_day_of_year + 1)) {
		return cur_year - 1;
	}
	else {
		return cur_year;
	}
}

void CDirectoryListingParserTest::InitEntries()
{
	// Unix-style listings
	// -------------------

	// We start with a perfect example of a unix style directory listing without anomalies.
	m_entries.emplace_back(t_entry({
			"dr-xr-xr-x   2 root     other        512 Apr  8  1994 01-unix-std dir",
			{
				L"01-unix-std dir",
				512,
				R(L"dr-xr-xr-x"),
				R(L"root other"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 1994, 4, 8)
			},
			DEFAULT
		}));

	// This one is a recent file with a time instead of the year.
	m_entries.emplace_back(t_entry({
			"-rw-r--r--   1 root     other        531 3 29 03:26 02-unix-std file",
			{
				L"02-unix-std file",
				531,
				R(L"-rw-r--r--"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(3, 29), 3, 29, 3, 26)
			},
			DEFAULT
		}));

	// Group omitted
	m_entries.emplace_back(t_entry({
			"dr-xr-xr-x   2 root                  512 Apr  8  1994 03-unix-nogroup dir",
			{
				L"03-unix-nogroup dir",
				512,
				R(L"dr-xr-xr-x"),
				R(L"root"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 1994, 4, 8)
			},
			DEFAULT
		}));

	// Symbolic link
	m_entries.emplace_back(t_entry({
			"lrwxrwxrwx   1 root     other          7 Jan 25 00:17 04-unix-std link -> usr/bin",
			{
				L"04-unix-std link",
				7,
				R(L"lrwxrwxrwx"),
				R(L"root other"),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(L"usr/bin"),
				fz::datetime(fz::datetime::utc, calcYear(1, 25), 1, 25, 0, 17)
			},
			DEFAULT
		}));

	// Some listings with uncommon date/time format
	// --------------------------------------------

	m_entries.emplace_back(t_entry({
			"-rw-r--r--   1 root     other        531 09-26 2000 05-unix-date file",
			{
				L"05-unix-date file",
				531,
				R(L"-rw-r--r--"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 9, 26)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-rw-r--r--   1 root     other        531 09-26 13:45 06-unix-date file",
			{
				L"06-unix-date file",
				531,
				R(L"-rw-r--r--"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(9, 26), 9, 26, 13, 45)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-rw-r--r--   1 root     other        531 2005-06-07 21:22 07-unix-date file",
			{
				L"07-unix-date file",
				531,
				R(L"-rw-r--r--"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2005, 6, 7, 21, 22)
			},
			DEFAULT
		}));


	// Unix style with size information in kilobytes
	m_entries.emplace_back(t_entry({
			"-rw-r--r--   1 root     other  33.5k Oct 5 21:22 08-unix-namedsize file",
			{
				L"08-unix-namedsize file",
				335 * 1024 / 10,
				R(L"-rw-r--r--"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(10, 5), 10, 5, 21, 22)
			},
			DEFAULT
		}));

	// NetWare style listings
	// ----------------------

	m_entries.emplace_back(t_entry({
			"d [R----F--] supervisor            512       Jan 16 18:53    09-netware dir",
			{
				L"09-netware dir",
				512,
				R(L"d [R----F--]"),
				R(L"supervisor"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(1, 16), 1, 16, 18, 53)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"- [R----F--] rhesus             214059       Oct 20 15:27    10-netware file",
			{
				L"10-netware file",
				214059,
				R(L"- [R----F--]"),
				R(L"rhesus"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(10, 20), 10, 20, 15, 27)
			},
			DEFAULT
		}));

	// NetPresenz for the Mac
	// ----------------------

	// Actually this one isn't parsed properly:
	// The numerical username is mistaken as size. However,
	// this is ambiguous to the normal unix style listing.
	// It's not possible to recognize both formats the right way.
	m_entries.emplace_back(t_entry({
			"-------r--         326  1391972  1392298 Nov 22  1995 11-netpresenz file",
			{
				L"11-netpresenz file",
				1392298,
				R(L"-------r--"),
				R(L"1391972"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 1995, 11, 22)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"drwxrwxr-x               folder        2 May 10  1996 12-netpresenz dir",
			{
				L"12-netpresenz dir",
				2,
				R(L"drwxrwxr-x"),
				R(L"folder"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 1996, 5, 10)
			},
			DEFAULT
		}));

	// A format with domain field some windows servers send
	m_entries.emplace_back(t_entry({
			"-rw-r--r--   1 group domain user 531 Jan 29 03:26 13-unix-domain file",
			{
				L"13-unix-domain file",
				531,
				R(L"-rw-r--r--"),
				R(L"group domain user"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(1, 29), 1, 29, 3, 26)
			},
			DEFAULT
		}));

	// EPLF directory listings
	// -----------------------

	// See http://cr.yp.to/ftp/list/eplf.html (mirrored at https://filezilla-project.org/specs/eplf.html)

	m_entries.emplace_back(t_entry({
			"+i8388621.48594,m825718503,r,s280,up755\t14-eplf file",
			{
				L"14-eplf file",
				280,
				R(L"755"),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 1996, 3, 1, 22, 15, 3)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"+i8388621.50690,m824255907,/,\t15-eplf dir",
			{
				L"15-eplf dir",
				-1,
				R(),
				R(),
				CDirentry::flag_dir | 0,
				O(),
				fz::datetime(fz::datetime::utc, 1996, 2, 13, 23, 58, 27)
			},
			DEFAULT
		}));

	// MSDOS type listing used by old IIS
	// ----------------------------------

	m_entries.emplace_back(t_entry({
			"04-27-00  12:09PM       <DIR>          16-dos-dateambiguous dir",
			{
				L"16-dos-dateambiguous dir",
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 4, 27, 12, 9)
			},
			DEFAULT
		}));

	// Ambiguous date and AM/PM crap. Some evil manager must have forced the poor devs to implement this
	m_entries.emplace_back(t_entry({
			"04-06-00  03:47PM                  589 17-dos-dateambiguous file",
			{
				L"17-dos-dateambiguous file",
				589,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 4, 6, 15, 47)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"2002-09-02  18:48       <DIR>          18-dos-longyear dir",
			{
				L"18-dos-longyear dir",
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2002, 9, 2, 18, 48)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"2002-09-02  19:06                9,730 19-dos-longyear file",
			{
				L"19-dos-longyear file",
				9730,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2002, 9, 2, 19, 6)
			},
			DEFAULT
		}));

	// Numerical unix style listing
	m_entries.emplace_back(t_entry({
			"0100644   500  101   12345    123456789       20-unix-numerical file",
			{
				L"20-unix-numerical file",
				12345,
				R(L"0100644"),
				R(L"500 101"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 1973, 11, 29, 21, 33, 9)
			},
			DEFAULT
		}));

	// VShell servers
	// --------------

	m_entries.emplace_back(t_entry({
			"206876  Apr 04, 2000 21:06 21-vshell-old file",
			{
				L"21-vshell-old file",
				206876,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 4, 4, 21, 6)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"0  Dec 12, 2002 02:13 22-vshell-old dir/",
			{
				L"22-vshell-old dir",
				0,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2002, 12, 12, 2, 13)
			},
			DEFAULT
		}));

	/* This type of directory listings is sent by some newer versions of VShell
	 * both year and time in one line is uncommon. */
	m_entries.emplace_back(t_entry({
			"-rwxr-xr-x    1 user group        9 Oct 08, 2002 09:47 23-vshell-new file",
			{
				L"23-vshell-new file",
				9,
				R(L"-rwxr-xr-x"),
				R(L"user group"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2002, 10, 8, 9, 47)
			},
			DEFAULT
		}));

	// OS/2 server format
	// ------------------

	// This server obviously isn't Y2K aware
	m_entries.emplace_back(t_entry({
			"36611      A    04-23-103  10:57  24-os2 file",
			{
				L"24-os2 file",
				36611,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 4, 23, 10, 57)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			" 1123      A    07-14-99   12:37  25-os2 file",
			{
				L"25-os2 file",
				1123,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 1999, 7, 14, 12, 37)
			},
			DEFAULT
		}));

	// Another server not aware of Y2K
	m_entries.emplace_back(t_entry({
			"    0 DIR       02-11-103  16:15  26-os2 dir",
			{
				L"26-os2 dir",
				0,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 2, 11, 16, 15)
			},
			DEFAULT
		}));

	// Again Y2K
	m_entries.emplace_back(t_entry({
			" 1123 DIR  A    10-05-100  23:38  27-os2 dir",
			{
				L"27-os2 dir",
				1123,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 10, 5, 23, 38)
			},
			DEFAULT
		}));

	// Localized date formats
	// ----------------------

	m_entries.emplace_back(t_entry({
			"dr-xr-xr-x   2 root     other      2235 26. Juli, 20:10 28-datetest-ger dir",
			{
				L"28-datetest-ger dir",
				2235,
				R(L"dr-xr-xr-x"),
				R(L"root other"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(7, 26), 7, 26, 20, 10)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"dr-xr-xr-x   2 root     other      2235 szept 26 20:10 28b-datetest-hungarian dir",
			{
				L"28b-datetest-hungarian dir",
				2235,
				R(L"dr-xr-xr-x"),
				R(L"root other"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(9, 26), 9, 26, 20, 10)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-r-xr-xr-x   2 root     other      2235 2.   Okt.  2003 29-datetest-ger file",
			{
				L"29-datetest-ger file",
				2235,
				R(L"-r-xr-xr-x"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 10, 2)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-r-xr-xr-x   2 root     other      2235 1999/10/12 17:12 30-datetest file",
			{
				L"30-datetest file",
				2235,
				R(L"-r-xr-xr-x"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 1999, 10, 12, 17, 12)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-r-xr-xr-x   2 root     other      2235 24-04-2003 17:12 31-datetest file",
			{
				L"31-datetest file",
				2235,
				R(L"-r-xr-xr-x"),
				R(L"root other"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 4, 24, 17, 12)
			},
			DEFAULT
		}));

	// Japanese listing
	// Remark: I'v no idea in which encoding the foreign characters are, but
	// it's not valid UTF-8. Parser has to be able to cope with it somehow.
	m_entries.emplace_back(t_entry({
			"-rw-r--r--   1 root       sys           8473  4\x8c\x8e 18\x93\xfa 2003\x94\x4e 32-datatest-japanese file",
			{
				L"32-datatest-japanese file",
				8473,
				R(L"-rw-r--r--"),
				R(L"root sys"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 4, 18)
			},
			DEFAULT
		}));

	// Some other asian listing format. Those >127 chars are just examples

	m_entries.emplace_back(t_entry({
			"-rwxrwxrwx   1 root     staff          0 2003   3\xed\xef 20 33-asian date file",
			{
				L"33-asian date file",
				0,
				R(L"-rwxrwxrwx"),
				R(L"root staff"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 3, 20)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-r--r--r-- 1 root root 2096 8\xed 17 08:52 34-asian date file",
			{
				L"34-asian date file",
				2096,
				R(L"-r--r--r--"),
				R(L"root root"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(8, 17), 8, 17, 8, 52)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-r-xr-xr-x   2 root  root  96 2004.07.15   35-dotted-date file",
			{
				L"35-dotted-date file",
				96,
				R(L"-r-xr-xr-x"),
				R(L"root root"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2004, 7, 15)
			},
			DEFAULT
		}));

	// VMS listings
	// ------------

	m_entries.emplace_back(t_entry({
			"36-vms-dir.DIR;1  1 19-NOV-2001 21:41 [root,root] (RWE,RWE,RE,RE)",
			{
				L"36-vms-dir",
				512,
				R(L"RWE,RWE,RE,RE"),
				R(L"root,root"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2001, 11, 19, 21, 41)
			},
			DEFAULT
		}));


	m_entries.emplace_back(t_entry({
			"37-vms-file;1       155   2-JUL-2003 10:30:13.64",
			{
				L"37-vms-file;1",
				79360,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 7, 2, 10, 30, 13)
			},
			DEFAULT
		}));

	/* VMS style listing without time */
	m_entries.emplace_back(t_entry({
			"38-vms-notime-file;1    2/8    7-JAN-2000    [IV2_XXX]   (RWED,RWED,RE,)",
			{
				L"38-vms-notime-file;1",
				1024,
				R(L"RWED,RWED,RE,"),
				R(L"IV2_XXX"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 1, 7)
			},
			DEFAULT
		}));

	/* Localized month */
	m_entries.emplace_back(t_entry({
			"39-vms-notime-file;1    6/8    15-JUI-2002    PRONAS   (RWED,RWED,RE,)",
			{
				L"39-vms-notime-file;1",
				3072,
				R(L"RWED,RWED,RE,"),
				R(L"PRONAS"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2002, 7, 15)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"40-vms-multiline-file;1\r\n170774/170775     24-APR-2003 08:16:15  [FTP_CLIENT,SCOT]      (RWED,RWED,RE,)",
			{
				L"40-vms-multiline-file;1",
				87436288,
				R(L"RWED,RWED,RE,"),
				R(L"FTP_CLIENT,SCOT"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 4, 24, 8, 16, 15)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"41-vms-multiline-file;1\r\n10     2-JUL-2003 10:30:08.59  [FTP_CLIENT,SCOT]      (RWED,RWED,RE,)",
			{
				L"41-vms-multiline-file;1",
				5120,
				R(L"RWED,RWED,RE,"),
				R(L"FTP_CLIENT,SCOT"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 7, 2, 10, 30, 8)
			},
			DEFAULT
		}));

	// VMS style listings with a different field order
	m_entries.emplace_back(t_entry({
			"42-vms-alternate-field-order-file;1   [SUMMARY]    1/3     2-AUG-2006 13:05  (RWE,RWE,RE,)",
			{
				L"42-vms-alternate-field-order-file;1",
				512,
				R(L"RWE,RWE,RE,"),
				R(L"SUMMARY"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2006, 8, 2, 13, 5)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"43-vms-alternate-field-order-file;1       17-JUN-1994 17:25:37     6308/13     (RWED,RWED,R,)",
			{
				L"43-vms-alternate-field-order-file;1",
				3229696,
				R(L"RWED,RWED,R,"),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 1994, 6, 17, 17, 25, 37)
			},
			DEFAULT
		}));

	// Miscellaneous listings
	// ----------------------

	/* IBM AS/400 style listing */
	m_entries.emplace_back(t_entry({
			"QSYS            77824 02/23/00 15:09:55 *DIR 44-ibm-as400 dir/",
			{
				L"44-ibm-as400 dir",
				77824,
				R(),
				R(L"QSYS"),
				CDirentry::flag_dir | 0,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 2, 23, 15, 9, 55)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"QSYS            77824 23/02/00 15:09:55 *FILE 45-ibm-as400-date file",
			{
				L"45-ibm-as400-date file",
				77824,
				R(),
				R(L"QSYS"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2000, 2, 23, 15, 9, 55)
			},
			DEFAULT
		}));

	/* aligned directory listing with too long size */
	m_entries.emplace_back(t_entry({
			"-r-xr-xr-x longowner longgroup123456 Feb 12 17:20 46-unix-concatsize file",
			{
				L"46-unix-concatsize file",
				123456,
				R(L"-r-xr-xr-x"),
				R(L"longowner longgroup"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(2, 12), 2, 12, 17, 20)
			},
			DEFAULT
		}));

	/* short directory listing with month name */
	m_entries.emplace_back(t_entry({
			"-r-xr-xr-x 2 owner group 4512 01-jun-99 47_unix_shortdatemonth file",
			{
				L"47_unix_shortdatemonth file",
				4512,
				R(L"-r-xr-xr-x"),
				R(L"owner group"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 1999, 6, 1)
			},
			DEFAULT
		}));

	/* Nortel wfFtp router */
	m_entries.emplace_back(t_entry({
			"48-nortel-wfftp-file       1014196  06/03/04  Thur.   10:20:03",
			{
				L"48-nortel-wfftp-file",
				1014196,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2004, 6, 3, 10, 20, 3)
			},
			DEFAULT
		}));

	/* VxWorks based server used in Nortel routers */
	m_entries.emplace_back(t_entry({
			"2048    Feb-28-1998  05:23:30   49-nortel-vxworks dir <DIR>",
			{
				L"49-nortel-vxworks dir",
				2048,
				R(),
				R(),
				CDirentry::flag_dir | 0,
				O(),
				fz::datetime(fz::datetime::utc, 1998, 2, 28, 5, 23, 30)
			},
			DEFAULT
		}));

	/* the following format is sent by the Connect:Enterprise server by Sterling Commerce */
	m_entries.emplace_back(t_entry({
			"-C--E-----FTP B BCC3I1       7670  1294495 Jan 13 07:42 50-conent file",
			{
				L"50-conent file",
				1294495,
				R(L"-C--E-----FTP"),
				R(L"B BCC3I1 7670"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(1, 13), 1, 13, 7, 42)
			},
			DEFAULT
		}));

	/* Microware OS-9
	 * Notice the yy/mm/dd date format */
	m_entries.emplace_back(t_entry({
			"20.20 07/03/29 1026 d-ewrewr 2650 85920 51-OS-9 dir",
			{
				L"51-OS-9 dir",
				85920,
				R(L"d-ewrewr"),
				R(L"20.20"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2007, 3, 29)
			},
			DEFAULT
		}));

	/* Localised Unix style listing. Month and day fields are swapped */
	m_entries.emplace_back(t_entry({
			"drwxr-xr-x 3 user group 512 01 oct 2004 52-swapped-daymonth dir",
			{
				L"52-swapped-daymonth dir",
				512,
				R(L"drwxr-xr-x"),
				R(L"user group"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2004, 10, 1)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"-r--r--r-- 0125039 12 Nov 11 2005 53-noownergroup file",
			{
				L"53-noownergroup file",
				12,
				R(L"-r--r--r--"),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2005, 11, 11)
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			// Valid UTF-8 encoding
			"drwxr-xr-x   5 root     sys          512 2005\xEB\x85\x84  1\xEC\x9B\x94  6\xEC\x9D\xBC 54-asian date year first dir",
			{
				L"54-asian date year first dir",
				512,
				R(L"drwxr-xr-x"),
				R(L"root sys"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2005, 1, 6)
			},
			DEFAULT
		}));

	/* IBM AS/400 style listing with localized date*/
	m_entries.emplace_back(t_entry({
			"QPGMR           36864 18.09.06 14:21:26 *FILE      55-AS400.FILE",
			{
				L"55-AS400.FILE",
				36864,
				R(),
				R(L"QPGMR"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2006, 9, 18, 14, 21, 26)
			},
			DEFAULT
		}));

	/* VMS style listing with complex size */
	m_entries.emplace_back(t_entry({
			"56-VMS-complex-size;1 2KB 23-SEP-2005 14:57:07.27",
			{
				L"56-VMS-complex-size;1",
				2048,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2005, 9, 23, 14, 57, 7)
			},
			DEFAULT
		}));

	/* HP NonStop */
	m_entries.emplace_back(t_entry({
			"57-HP_NonStop 101 528 6-Apr-07 14:21:18 255, 0 \"oooo\"",
			{
				L"57-HP_NonStop",
				528,
				R(L"\"oooo\""),
				R(L"255, 0"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2007, 4, 6, 14, 21, 18)
			},
			HPNONSTOP
		}));

	// Only difference is in the owner/group field, no delimiting space.
	m_entries.emplace_back(t_entry({
			"58-HP_NonStop 101 528 6-Apr-07 14:21:18 255,255 \"oooo\"",
			{
				L"58-HP_NonStop",
				528,
				R(L"\"oooo\""),
				R(L"255,255"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2007, 4, 6, 14, 21, 18)
			},
			HPNONSTOP
		}));


	m_entries.emplace_back(t_entry({
			"drwxr-xr-x 6 user sys 1024 30. Jan., 12:40 59-localized-date-dir",
			{
				L"59-localized-date-dir",
				1024,
				R(L"drwxr-xr-x"),
				R(L"user sys"),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, calcYear(1, 30), 1, 30, 12, 40)
			},
			DEFAULT
		}));

	// MVS variants
	//
	// Note: I am not quite sure of these get parsed correctly, but so far
	//       nobody did complain. Formats added here with what I think
	//       is at least somewhat correct, so that there won't be any
	//       regressions at least.

	// The following 5 are loosely based on this format:
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.emplace_back(t_entry({
			"WYOSPT 3420   2003/05/21  1  200  FB      80  8053  PS  60-MVS.FILE",
			{
				L"60-MVS.FILE",
				100,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2003, 5, 21)
		},
		DEFAULT
	}));

	m_entries.emplace_back(t_entry({
			"WPTA01 3290   2004/03/04  1    3  FB      80  3125  PO  61-MVS.DATASET",
			{
				L"61-MVS.DATASET",
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2004, 3, 04)
		},
		DEFAULT
	}));

	m_entries.emplace_back(t_entry({
			"NRP004 3390   **NONE**    1   15  NONE     0     0  PO  62-MVS-NONEDATE.DATASET",
			{
				L"62-MVS-NONEDATE.DATASET",
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime()
		},
		DEFAULT
	}));

	m_entries.emplace_back(t_entry({
			"TSO005 3390   2005/06/06 213000 U 0 27998 PO 63-MVS.DATASET",
			{
				L"63-MVS.DATASET",
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				fz::datetime(fz::datetime::utc, 2005, 6, 6)
		},
		DEFAULT
	}));

	m_entries.emplace_back(t_entry({
			"TSO004 3390   VSAM 64-mvs-file",
			{
				L"64-mvs-file",
				-1,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
		},
		DEFAULT
	}));

	// MVS Dataset members
	//
	// As common with IBM misdesign, multiple styles exist.

	// Speciality: Some members have no attributes at all.
	// Requires servertype to be MVS or it won't be parsed, as
	// it would conflict with lots of other servers.
	m_entries.emplace_back(t_entry({
			"65-MVS-PDS-MEMBER",
			{
				L"65-MVS-PDS-MEMBER",
				-1,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
		},
		MVS
	}));

	// Name         VV.MM   Created      Changed       Size  Init  Mod Id
	m_entries.emplace_back(t_entry({
			"66-MVSPDSMEMBER 01.01 2004/06/22 2004/06/22 16:32   128   128    0 BOBY12",
			{
				L"66-MVSPDSMEMBER",
				128,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2004, 6, 22, 16, 32)
		},
		MVS
	}));

	// Hexadecimal size
	m_entries.emplace_back(t_entry({
			"67-MVSPDSMEMBER2 00B308 000411  00 FO                31    ANY",
			{
				L"67-MVSPDSMEMBER2",
				45832,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
		},
		MVS
	}));

	m_entries.emplace_back(t_entry({
			"68-MVSPDSMEMBER3 00B308 000411  00 FO        RU      ANY    24",
			{
				L"68-MVSPDSMEMBER3",
				45832,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
			},
			MVS
		}));

	// Migrated MVS file
	m_entries.emplace_back(t_entry({
			"Migrated				69-SOME.FILE",
			{
				L"69-SOME.FILE",
				-1,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
			},
			MVS
		}));

	// z/VM, another IBM abomination. Description by Alexandre Charbey
	// Requires type set to ZVM or it cannot be parsed.
	//
	// 70-ZVMFILE
	//   is a filename
	// TRACE
	//   is a filetype (extension, like exe or com or jpg...)
	// V
	//   is the file format. Designates how records are arranged in a file. F=Fixed and V=Variable. I don't think you care
	// 65
	//   is the logical record length.
	// 107
	//   is Number of records in a file.
	// 2
	//   (seems wrong) is the block size ( iirc 1 is 127, 2 is 381, 3 is 1028 and 4 is 4072 - not sure - the numbers are not the usual binary numbers)
	// there is the date/time
	// 060191
	//   I think it is some internal stuff saying who the file belongs to.  191 is the "handle" of the user's disk. I don't know what 060 is. This 060191 is what FZ shows in its file list.
	m_entries.emplace_back(t_entry({
			"70-ZVMFILE  TRACE   V        65      107        2 2005-10-04 15:28:42 060191",
			{
				L"70-ZVMFILE.TRACE",
				6955,
				R(),
				R(L"060191"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2005, 10, 4, 15, 28, 42)
			},
			ZVM
		}));

	m_entries.emplace_back(t_entry({
			"drwxr-xr-x 3 slopri devlab 512 71-unix-dateless",
			{
				L"71-unix-dateless",
				512,
				R(L"drwxr-xr-x"),
				R(L"slopri devlab"),
				CDirentry::flag_dir,
				O(),
				fz::datetime()
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
			"Type=file;mOdIfY=20081105165215;size=1234; 72-MLSD-file",
			{
				L"72-MLSD-file",
				1234,
				R(),
				R(),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2008, 11, 5, 16, 52, 15)
			},
			DEFAULT
		}));

	// Yet another MVS format.
	// Follows the below structure but with all but the first two and the last field empty.
	// Furthermore, Unit is "Tape"
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.emplace_back(t_entry({
			"V43525 Tape                                             73-MSV-TAPE.FILE",
			{
				L"73-MSV-TAPE.FILE",
				-1,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
			},
			MVS
		}));

	m_entries.emplace_back(t_entry({
			"Type=file; 74-MLSD-whitespace trailing\t ",
			{
				L"74-MLSD-whitespace trailing\t ",
				-1,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
			},
			DEFAULT
		}));

		m_entries.emplace_back(t_entry({
			"Type=file; \t 75-MLSD-whitespace leading",
			{
				L"\t 75-MLSD-whitespace leading",
				-1,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
			},
			DEFAULT
		}));

		m_entries.emplace_back(t_entry({
			"modify=20080426135501;perm=;size=65718921;type=file;unique=802U1066013B;UNIX.group=1179;UNIX.mode=00;UNIX.owner=1179; 75 MLSD file with empty permissions",
			{
				L"75 MLSD file with empty permissions",
				65718921,
				R(L"00"),
				R(L"1179 1179"),
				0,
				O(),
				fz::datetime(fz::datetime::utc, 2008, 4, 26, 13, 55, 1)
			},
			DEFAULT
		}));

		m_entries.emplace_back(t_entry({
			"type=OS.unix=slink:/foo; 76 MLSD symlink",
			{
				L"76 MLSD symlink",
				-1,
				R(),
				R(),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(L"/foo"),
				fz::datetime()
			},
			DEFAULT
		}));

		m_entries.emplace_back(t_entry({
			"type=OS.UNIX=symlink; 76b MLSD symlink",
			{
				L"76b MLSD symlink",
				-1,
				R(),
				R(),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(),
				fz::datetime()
			},
			DEFAULT
		}));

		// Old ietf draft for MLST earlier than mlst-07 has no trailing semicolon after facts
		m_entries.emplace_back(t_entry({
			"type=file 77 MLSD file no trailing semicolon after facts < mlst-07",
			{
				L"77 MLSD file no trailing semicolon after facts < mlst-07",
				-1,
				R(),
				R(),
				0,
				O(),
				fz::datetime()
			},
			DEFAULT
		}));

		m_entries.emplace_back(t_entry({
			"type=OS.unix=slink; 77 MLSD symlink notarget",
			{
				L"77 MLSD symlink notarget",
				-1,
				R(),
				R(),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(),
				fz::datetime()
			},
			DEFAULT
		}));

	m_entries.emplace_back(t_entry({
		"size=1365694195;type=file;modify=20090722092510;\tadsl TV 2009-07-22 08-25-10 78 mlsd file that can get parsed as unix.file",
		{
			L"adsl TV 2009-07-22 08-25-10 78 mlsd file that can get parsed as unix.file",
			1365694195,
			R(),
			R(),
			0,
			O(),
			fz::datetime(fz::datetime::utc, 2009, 7, 22, 9, 25, 10)
		},
		DEFAULT
	}));

	// MVS entry with a large number of used blocks:
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.emplace_back(t_entry({
		"WYOSPT 3420   2003/05/21  1 ????  FB      80  8053  PS  79-MVS.FILE",
		{
			L"79-MVS.FILE",
			100,
			R(),
			R(),
			0,
			O(),
			fz::datetime(fz::datetime::utc, 2003, 5, 21)
		},
		DEFAULT
	}));

	// MVS entry with a large number of used blocks:
	// https://forum.filezilla-project.org/viewtopic.php?t=21667
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.emplace_back(t_entry({
		"GISBWI 3390   2011/08/25  2 ++++  FB     904 18080  PS  80-MVS.FILE",
		{
			L"80-MVS.FILE",
			100,
			R(),
			R(),
			0,
			O(),
			fz::datetime(fz::datetime::utc, 2011, 8, 25)
		},
		DEFAULT
	}));

	// MVS entry with PO-E Dsorg indicating direrctory. See
	// https://forum.filezilla-project.org/viewtopic.php?t=19374 for reference.
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.emplace_back(t_entry({
		"WYOSPT 3420   2003/05/21  1 3 U 6447    6447  PO-E 81-MVS.DIR",
		{
			L"81-MVS.DIR",
			-1,
			R(),
			R(),
			CDirentry::flag_dir,
			O(),
			fz::datetime(fz::datetime::utc, 2003, 5, 21)
		},
		DEFAULT
	}));

	m_entries.push_back(t_entry({
		"drwxrwxrwx   1 0        0               0 29 Jul 02:27 2014 Invoices",
		{
			L"2014 Invoices",
			0,
			R(L"drwxrwxrwx"),
			R(L"0 0"),
			CDirentry::flag_dir,
			O(),
			fz::datetime(fz::datetime::utc, calcYear(7, 29), 7, 29, 2, 27)
		},
		DEFAULT
	}));


	m_entries.emplace_back(t_entry({
		"Type=file;mOdIfY=19681105165215;size=1234; MLSD pre-epoch",
		{
			L"MLSD pre-epoch",
			1234,
			R(),
			R(),
			0,
			O(),
			fz::datetime(fz::datetime::utc, 1968, 11, 5, 16, 52, 15)
		},
		DEFAULT
	}));

	m_entries.emplace_back(t_entry({
		"-rw-------      1  99999999 0              3 Apr   4 24:00 alternate_midnight",
		{
			L"alternate_midnight",
			3,
			R(L"-rw-------"),
			R(L"99999999 0"),
			0,
			O(),
			fz::datetime(fz::datetime::utc, calcYear(4, 4), 4, 5, 0, 0)
		},
		DEFAULT
	}));

/*
	std::wstring name;
	int64_t size;
	std::wstring permissions;
	std::wstring ownerGroup;
	int flags;
	std::wstring target; // Set to linktarget it link is true

	wxDateTime time;
*/

	// Fix line endings
	for (auto & entry : m_entries) {
		entry.data += "\r\n";
	}
}

void CDirectoryListingParserTest::testIndividual()
{
	m_sync.lock();

	static int index = 0;
	t_entry const& entry = m_entries[index++];

	m_sync.unlock();

	CServer server;
	server.SetType(entry.serverType);

	CDirectoryListingParser parser(0, server);

	size_t const len = entry.data.size();
	char* data = new char[len];
	memcpy(data, entry.data.c_str(), len);
	parser.AddData(data, len);

	CDirectoryListing listing = parser.Parse(CServerPath());

	std::string msg = fz::sprintf("Data: %s, count: %d", entry.data, listing.GetCount());
	fz::replace_substrings(msg, "\r", std::string());
	fz::replace_substrings(msg, "\n", std::string());

	CPPUNIT_ASSERT_MESSAGE(msg, listing.GetCount() == 1);

	msg = fz::sprintf("Data: %s  Expected:\n%s\n  Got:\n%s", entry.data, entry.reference.dump(), listing[0].dump());
	CPPUNIT_ASSERT_MESSAGE(msg, listing[0] == entry.reference);
}

void CDirectoryListingParserTest::testAll()
{
	CServer server;
	CDirectoryListingParser parser(0, server);
	for (auto const& entry : m_entries) {
		server.SetType(entry.serverType);
		parser.SetServer(server);
		size_t const len = entry.data.size();
		char* data = new char[len];
		memcpy(data, entry.data.c_str(), len);
		parser.AddData(data, len);
	}
	CDirectoryListing listing = parser.Parse(CServerPath());

	CPPUNIT_ASSERT(listing.GetCount() == m_entries.size());

	unsigned int i = 0;
	for (auto iter = m_entries.begin(); iter != m_entries.end(); iter++, i++) {
		std::string msg = fz::sprintf("Data: %s  Expected:\n%s\n  Got:\n%s", iter->data, iter->reference.dump(), listing[i].dump());

		CPPUNIT_ASSERT_MESSAGE(msg, listing[i] == iter->reference);
	}
}

void CDirectoryListingParserTest::setUp()
{
}
