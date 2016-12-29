#include <filezilla.h>
#include "directorylistingparser.h"
#include <cppunit/extensions/HelperMacros.h>
#include <list>

/*
 * This testsuite asserts the correctness of the CServerPath class.
 */

class CServerPathTest final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(CServerPathTest);
	CPPUNIT_TEST(testGetPath);
	CPPUNIT_TEST(testHasParent);
	CPPUNIT_TEST(testGetParent);
	CPPUNIT_TEST(testFormatSubdir);
	CPPUNIT_TEST(testGetCommonParent);
	CPPUNIT_TEST(testFormatFilename);
	CPPUNIT_TEST(testChangePath);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void testGetPath();
	void testHasParent();
	void testGetParent();
	void testFormatSubdir();
	void testGetCommonParent();
	void testFormatFilename();
	void testChangePath();

protected:
};

CPPUNIT_TEST_SUITE_REGISTRATION(CServerPathTest);

void CServerPathTest::testGetPath()
{
	const CServerPath unix1(L"/");
	CPPUNIT_ASSERT(unix1.GetPath() == L"/");

	const CServerPath unix2(L"/foo");
	CPPUNIT_ASSERT(unix2.GetPath() == L"/foo");

	const CServerPath unix3(L"/foo/bar");
	CPPUNIT_ASSERT(unix3.GetPath() == L"/foo/bar");


	const CServerPath vms1(L"FOO:[BAR]");
	CPPUNIT_ASSERT(vms1.GetPath() == L"FOO:[BAR]");

	const CServerPath vms2(L"FOO:[BAR.TEST]");
	CPPUNIT_ASSERT(vms2.GetPath() == L"FOO:[BAR.TEST]");

	const CServerPath vms3(L"FOO:[BAR^.TEST]");
	CPPUNIT_ASSERT(vms3.GetPath() == L"FOO:[BAR^.TEST]");

	const CServerPath vms4(L"FOO:[BAR^.TEST.SOMETHING]");
	CPPUNIT_ASSERT(vms4.GetPath() == L"FOO:[BAR^.TEST.SOMETHING]");


	const CServerPath dos1(L"C:\\");
	CPPUNIT_ASSERT(dos1.GetPath() == L"C:\\");

	const CServerPath dos2(L"C:\\FOO");
	CPPUNIT_ASSERT(dos2.GetPath() == L"C:\\FOO");

	const CServerPath dos3(L"md:\\", DOS);
	CPPUNIT_ASSERT(dos3.GetPath() == L"md:\\");

	const CServerPath dos4(L"C:", DOS);
	CPPUNIT_ASSERT(dos4.GetPath() == L"C:\\");

	const CServerPath dos5(L"C:\\FOO\\");
	CPPUNIT_ASSERT(dos5.GetPath() == L"C:\\FOO");


	const CServerPath mvs1(L"'FOO'", MVS);
	CPPUNIT_ASSERT(mvs1.GetPath() == L"'FOO'");

	const CServerPath mvs2(L"'FOO.'", MVS);
	CPPUNIT_ASSERT(mvs2.GetPath() == L"'FOO.'");

	const CServerPath mvs3(L"'FOO.BAR'", MVS);
	CPPUNIT_ASSERT(mvs3.GetPath() == L"'FOO.BAR'");

	const CServerPath mvs4(L"'FOO.BAR.'", MVS);
	CPPUNIT_ASSERT(mvs4.GetPath() == L"'FOO.BAR.'");

	const CServerPath vxworks1(L":foo:");
	CPPUNIT_ASSERT(vxworks1.GetPath() == L":foo:");

	const CServerPath vxworks2(L":foo:bar");
	CPPUNIT_ASSERT(vxworks2.GetPath() == L":foo:bar");

	const CServerPath vxworks3(L":foo:bar/test");
	CPPUNIT_ASSERT(vxworks3.GetPath() == L":foo:bar/test");

	// ZVM is same as Unix, only makes difference in directory
	// listing parser.
	const CServerPath zvm1(L"/", ZVM);
	CPPUNIT_ASSERT(zvm1.GetPath() == L"/");

	const CServerPath zvm2(L"/foo", ZVM);
	CPPUNIT_ASSERT(zvm2.GetPath() == L"/foo");

	const CServerPath zvm3(L"/foo/bar", ZVM);
	CPPUNIT_ASSERT(zvm3.GetPath() == L"/foo/bar");

	const CServerPath hpnonstop1(L"\\mysys", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop1.GetPath() == L"\\mysys");

	const CServerPath hpnonstop2(L"\\mysys.$myvol", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop2.GetPath() == L"\\mysys.$myvol");

	const CServerPath hpnonstop3(L"\\mysys.$myvol.mysubvol", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop3.GetPath() == L"\\mysys.$myvol.mysubvol");

	const CServerPath dos_virtual1(L"\\");
	CPPUNIT_ASSERT(dos_virtual1.GetPath() == L"\\");

	const CServerPath dos_virtual2(L"\\foo");
	CPPUNIT_ASSERT(dos_virtual2.GetPath() == L"\\foo");

	const CServerPath dos_virtual3(L"\\foo\\bar");
	CPPUNIT_ASSERT(dos_virtual3.GetPath() == L"\\foo\\bar");

	const CServerPath cygwin1(L"/", CYGWIN);
	CPPUNIT_ASSERT(cygwin1.GetPath() == L"/");

	const CServerPath cygwin2(L"/foo", CYGWIN);
	CPPUNIT_ASSERT(cygwin2.GetPath() == L"/foo");

	const CServerPath cygwin3(L"//", CYGWIN);
	CPPUNIT_ASSERT(cygwin3.GetPath() == L"//");

	const CServerPath cygwin4(L"//foo", CYGWIN);
	CPPUNIT_ASSERT(cygwin4.GetPath() == L"//foo");


	const CServerPath dos_slashes1(L"C:\\", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(dos_slashes1.GetPath() == L"C:/");

	const CServerPath dos_slashes2(L"C:\\FOO", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(dos_slashes2.GetPath() == L"C:/FOO");

	const CServerPath dos_slashes3(L"md:\\", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(dos_slashes3.GetPath() == L"md:/");

	const CServerPath dos_slashes4(L"C:", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(dos_slashes4.GetPath() == L"C:/");

	const CServerPath dos_slashes5(L"C:\\FOO\\", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(dos_slashes5.GetPath() == L"C:/FOO");
}

void CServerPathTest::testHasParent()
{
	const CServerPath unix1(L"/");
	CPPUNIT_ASSERT(!unix1.HasParent());

	const CServerPath unix2(L"/foo");
	CPPUNIT_ASSERT(unix2.HasParent());

	const CServerPath unix3(L"/foo/bar");
	CPPUNIT_ASSERT(unix3.HasParent());

	const CServerPath vms1(L"FOO:[BAR]");
	CPPUNIT_ASSERT(!vms1.HasParent());

	const CServerPath vms2(L"FOO:[BAR.TEST]");
	CPPUNIT_ASSERT(vms2.HasParent());

	const CServerPath vms3(L"FOO:[BAR^.TEST]");
	CPPUNIT_ASSERT(!vms3.HasParent());

	const CServerPath vms4(L"FOO:[BAR^.TEST.SOMETHING]");
	CPPUNIT_ASSERT(vms4.HasParent());

	const CServerPath dos1(L"C:\\");
	CPPUNIT_ASSERT(!dos1.HasParent());

	const CServerPath dos2(L"C:\\FOO");
	CPPUNIT_ASSERT(dos2.HasParent());

	const CServerPath mvs1(L"'FOO'", MVS);
	CPPUNIT_ASSERT(!mvs1.HasParent());

	const CServerPath mvs2(L"'FOO.'", MVS);
	CPPUNIT_ASSERT(!mvs2.HasParent());

	const CServerPath mvs3(L"'FOO.BAR'", MVS);
	CPPUNIT_ASSERT(mvs3.HasParent());

	const CServerPath mvs4(L"'FOO.BAR.'", MVS);
	CPPUNIT_ASSERT(mvs4.HasParent());

	const CServerPath vxworks1(L":foo:");
	CPPUNIT_ASSERT(!vxworks1.HasParent());

	const CServerPath vxworks2(L":foo:bar");
	CPPUNIT_ASSERT(vxworks2.HasParent());

	const CServerPath vxworks3(L":foo:bar/test");
	CPPUNIT_ASSERT(vxworks3.HasParent());

	// ZVM is same as Unix, only makes difference in directory
	// listing parser.
	const CServerPath zvm1(L"/", ZVM);
	CPPUNIT_ASSERT(!zvm1.HasParent());

	const CServerPath zvm2(L"/foo", ZVM);
	CPPUNIT_ASSERT(zvm2.HasParent());

	const CServerPath zvm3(L"/foo/bar", ZVM);
	CPPUNIT_ASSERT(zvm3.HasParent());

	const CServerPath hpnonstop1(L"\\mysys", HPNONSTOP);
	CPPUNIT_ASSERT(!hpnonstop1.HasParent());

	const CServerPath hpnonstop2(L"\\mysys.$myvol", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop2.HasParent());

	const CServerPath hpnonstop3(L"\\mysys.$myvol.mysubvol", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop3.HasParent());

	const CServerPath dos_virtual1(L"\\");
	CPPUNIT_ASSERT(!dos_virtual1.HasParent());

	const CServerPath dos_virtual2(L"\\foo");
	CPPUNIT_ASSERT(dos_virtual2.HasParent());

	const CServerPath dos_virtual3(L"\\foo\\bar");
	CPPUNIT_ASSERT(dos_virtual3.HasParent());

	const CServerPath cygwin1(L"/", CYGWIN);
	CPPUNIT_ASSERT(!cygwin1.HasParent());

	const CServerPath cygwin2(L"/foo", CYGWIN);
	CPPUNIT_ASSERT(cygwin2.HasParent());

	const CServerPath cygwin3(L"/foo/bar", CYGWIN);
	CPPUNIT_ASSERT(cygwin3.HasParent());

	const CServerPath cygwin4(L"//", CYGWIN);
	CPPUNIT_ASSERT(!cygwin4.HasParent());

	const CServerPath cygwin5(L"//foo", CYGWIN);
	CPPUNIT_ASSERT(cygwin5.HasParent());

	const CServerPath cygwin6(L"//foo/bar", CYGWIN);
	CPPUNIT_ASSERT(cygwin6.HasParent());


	const CServerPath dos_slashes1(L"C:\\", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(!dos_slashes1.HasParent());

	const CServerPath dos_slashes2(L"C:\\FOO", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(dos_slashes2.HasParent());

}

void CServerPathTest::testGetParent()
{
	const CServerPath unix1(L"/");
	const CServerPath unix2(L"/foo");
	const CServerPath unix3(L"/foo/bar");
	CPPUNIT_ASSERT(unix2.GetParent() == unix1);
	CPPUNIT_ASSERT(unix3.GetParent() == unix2);

	const CServerPath vms1(L"FOO:[BAR]");
	const CServerPath vms2(L"FOO:[BAR.TEST]");
	const CServerPath vms3(L"FOO:[BAR^.TEST]");
	const CServerPath vms4(L"FOO:[BAR^.TEST.SOMETHING]");
	CPPUNIT_ASSERT(vms2.GetParent() == vms1);
	CPPUNIT_ASSERT(vms4.GetParent() == vms3);

	const CServerPath dos1(L"C:\\");
	const CServerPath dos2(L"C:\\FOO");
	CPPUNIT_ASSERT(dos2.GetParent() == dos1);

	const CServerPath mvs1(L"'FOO'", MVS);
	const CServerPath mvs2(L"'FOO.'", MVS);
	const CServerPath mvs3(L"'FOO.BAR'", MVS);
	const CServerPath mvs4(L"'FOO.BAR.'", MVS);
	CPPUNIT_ASSERT(mvs3.GetParent() == mvs2);
	CPPUNIT_ASSERT(mvs4.GetParent() == mvs2);

	const CServerPath vxworks1(L":foo:");
	const CServerPath vxworks2(L":foo:bar");
	const CServerPath vxworks3(L":foo:bar/test");
	CPPUNIT_ASSERT(vxworks2.GetParent() == vxworks1);
	CPPUNIT_ASSERT(vxworks3.GetParent() == vxworks2);

	// ZVM is same as Unix, only makes difference in directory
	// listing parser.
	const CServerPath zvm1(L"/", ZVM);
	const CServerPath zvm2(L"/foo", ZVM);
	const CServerPath zvm3(L"/foo/bar", ZVM);
	CPPUNIT_ASSERT(zvm2.GetParent() == zvm1);
	CPPUNIT_ASSERT(zvm3.GetParent() == zvm2);

	const CServerPath hpnonstop1(L"\\mysys", HPNONSTOP);
	const CServerPath hpnonstop2(L"\\mysys.$myvol", HPNONSTOP);
	const CServerPath hpnonstop3(L"\\mysys.$myvol.mysubvol", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop2.GetParent() == hpnonstop1);
	CPPUNIT_ASSERT(hpnonstop3.GetParent() == hpnonstop2);

	const CServerPath dos_virtual1(L"\\");
	const CServerPath dos_virtual2(L"\\foo");
	const CServerPath dos_virtual3(L"\\foo\\bar");
	CPPUNIT_ASSERT(dos_virtual2.GetParent() == dos_virtual1);
	CPPUNIT_ASSERT(dos_virtual3.GetParent() == dos_virtual2);

	const CServerPath cygwin1(L"/", CYGWIN);
	const CServerPath cygwin2(L"/foo", CYGWIN);
	const CServerPath cygwin3(L"/foo/bar", CYGWIN);
	const CServerPath cygwin4(L"//", CYGWIN);
	const CServerPath cygwin5(L"//foo", CYGWIN);
	const CServerPath cygwin6(L"//foo/bar", CYGWIN);
	CPPUNIT_ASSERT(cygwin2.GetParent() == cygwin1);
	CPPUNIT_ASSERT(cygwin3.GetParent() == cygwin2);
	CPPUNIT_ASSERT(cygwin5.GetParent() == cygwin4);
	CPPUNIT_ASSERT(cygwin6.GetParent() == cygwin5);

	const CServerPath dos_slashes1(L"C:\\", DOS_FWD_SLASHES);
	const CServerPath dos_slashes2(L"C:/FOO", DOS_FWD_SLASHES);
	CPPUNIT_ASSERT(dos_slashes2.GetParent() == dos_slashes1);

}

void CServerPathTest::testFormatSubdir()
{
	CServerPath path;
	path.SetType(VMS);

	CPPUNIT_ASSERT(path.FormatSubdir(L"FOO.BAR") == L"FOO^.BAR");
}

void CServerPathTest::testGetCommonParent()
{
	const CServerPath unix1(L"/foo");
	const CServerPath unix2(L"/foo/baz");
	const CServerPath unix3(L"/foo/bar");
	CPPUNIT_ASSERT(unix2.GetCommonParent(unix3) == unix1);
	CPPUNIT_ASSERT(unix1.GetCommonParent(unix1) == unix1);
	CPPUNIT_ASSERT(unix1.GetCommonParent(unix3) == unix1);

	const CServerPath vms1(L"FOO:[BAR]");
	const CServerPath vms2(L"FOO:[BAR.TEST]");
	const CServerPath vms3(L"GOO:[BAR");
	const CServerPath vms4(L"GOO:[BAR^.TEST.SOMETHING]");
	CPPUNIT_ASSERT(vms2.GetCommonParent(vms1) == vms1);
	CPPUNIT_ASSERT(vms3.GetCommonParent(vms1) == CServerPath());

	{
		const CServerPath dos1(L"C:\\");
		const CServerPath dos2(L"C:\\FOO");
		const CServerPath dos3(L"D:\\FOO");
		CPPUNIT_ASSERT(dos1.GetCommonParent(dos2) == dos1);
		CPPUNIT_ASSERT(dos2.GetCommonParent(dos3) == CServerPath());
	}

	const CServerPath mvs1(L"'FOO'", MVS);
	const CServerPath mvs2(L"'FOO.'", MVS);
	const CServerPath mvs3(L"'FOO.BAR'", MVS);
	const CServerPath mvs4(L"'FOO.BAR.'", MVS);
	const CServerPath mvs5(L"'BAR.'", MVS);
	const CServerPath mvs6(L"'FOO.BAR.BAZ'", MVS);
	CPPUNIT_ASSERT(mvs1.GetCommonParent(mvs2) == CServerPath());
	CPPUNIT_ASSERT(mvs3.GetCommonParent(mvs1) == CServerPath());
	CPPUNIT_ASSERT(mvs4.GetCommonParent(mvs3) == mvs2);
	CPPUNIT_ASSERT(mvs3.GetCommonParent(mvs4) == mvs2);
	CPPUNIT_ASSERT(mvs3.GetCommonParent(mvs2) == mvs2);
	CPPUNIT_ASSERT(mvs4.GetCommonParent(mvs2) == mvs2);
	CPPUNIT_ASSERT(mvs5.GetCommonParent(mvs2) == CServerPath());
	CPPUNIT_ASSERT(mvs6.GetCommonParent(mvs4) == mvs4);
	CPPUNIT_ASSERT(mvs6.GetCommonParent(mvs3) == mvs2);

	const CServerPath vxworks1(L":foo:");
	const CServerPath vxworks2(L":foo:bar");
	const CServerPath vxworks3(L":foo:baz");
	const CServerPath vxworks4(L":baz:bar");
	CPPUNIT_ASSERT(vxworks1.GetCommonParent(vxworks2) == vxworks1);
	CPPUNIT_ASSERT(vxworks2.GetCommonParent(vxworks3) == vxworks1);
	CPPUNIT_ASSERT(vxworks4.GetCommonParent(vxworks2) == CServerPath());

	// ZVM is same as Unix, only makes difference in directory
	// listing parser.
	const CServerPath zvm1(L"/foo", ZVM);
	const CServerPath zvm2(L"/foo/baz", ZVM);
	const CServerPath zvm3(L"/foo/bar", ZVM);
	CPPUNIT_ASSERT(zvm2.GetCommonParent(zvm3) == zvm1);
	CPPUNIT_ASSERT(zvm1.GetCommonParent(zvm1) == zvm1);

	CServerPath hpnonstop1(L"\\mysys", HPNONSTOP);
	CServerPath hpnonstop2(L"\\mysys.$myvol", HPNONSTOP);
	CServerPath hpnonstop3(L"\\mysys.$theirvol", HPNONSTOP);
	CServerPath hpnonstop4(L"\\theirsys.$myvol", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop2.GetCommonParent(hpnonstop3) == hpnonstop1);
	CPPUNIT_ASSERT(hpnonstop1.GetCommonParent(hpnonstop1) == hpnonstop1);
	CPPUNIT_ASSERT(hpnonstop2.GetCommonParent(hpnonstop4) == CServerPath());

	const CServerPath dos_virtual1(L"\\foo");
	const CServerPath dos_virtual2(L"\\foo\\baz");
	const CServerPath dos_virtual3(L"\\foo\\bar");
	CPPUNIT_ASSERT(dos_virtual2.GetCommonParent(dos_virtual3) == dos_virtual1);
	CPPUNIT_ASSERT(dos_virtual1.GetCommonParent(dos_virtual1) == dos_virtual1);

	const CServerPath cygwin1(L"/foo", CYGWIN);
	const CServerPath cygwin2(L"/foo/baz", CYGWIN);
	const CServerPath cygwin3(L"/foo/bar", CYGWIN);
	const CServerPath cygwin4(L"//foo", CYGWIN);
	const CServerPath cygwin5(L"//foo/baz", CYGWIN);
	const CServerPath cygwin6(L"//foo/bar", CYGWIN);
	CPPUNIT_ASSERT(cygwin2.GetCommonParent(cygwin3) == cygwin1);
	CPPUNIT_ASSERT(cygwin1.GetCommonParent(cygwin1) == cygwin1);
	CPPUNIT_ASSERT(cygwin1.GetCommonParent(cygwin3) == cygwin1);
	CPPUNIT_ASSERT(cygwin5.GetCommonParent(cygwin6) == cygwin4);
	CPPUNIT_ASSERT(cygwin4.GetCommonParent(cygwin4) == cygwin4);
	CPPUNIT_ASSERT(cygwin4.GetCommonParent(cygwin6) == cygwin4);
	CPPUNIT_ASSERT(cygwin4.GetCommonParent(cygwin1) == CServerPath());

	{
		const CServerPath dos_slashes1(L"C:\\", DOS_FWD_SLASHES);
		const CServerPath dos_slashes2(L"C:\\FOO", DOS_FWD_SLASHES);
		const CServerPath dos_slashes3(L"D:\\FOO", DOS_FWD_SLASHES);
		CPPUNIT_ASSERT(dos_slashes1.GetCommonParent(dos_slashes2) == dos_slashes1);
		CPPUNIT_ASSERT(dos_slashes2.GetCommonParent(dos_slashes3) == CServerPath());
	}

}

void CServerPathTest::testFormatFilename()
{
	const CServerPath unix1(L"/");
	const CServerPath unix2(L"/foo");
	CPPUNIT_ASSERT(unix1.FormatFilename(L"bar", false) == L"/bar");
	CPPUNIT_ASSERT(unix2.FormatFilename(L"bar", false) == L"/foo/bar");

	const CServerPath vms1(L"FOO:[BAR]");
	CPPUNIT_ASSERT(vms1.FormatFilename(L"BAZ", false) == L"FOO:[BAR]BAZ");

	{
		const CServerPath dos1(L"C:\\");
		const CServerPath dos2(L"C:\\foo");
		CPPUNIT_ASSERT(dos1.FormatFilename(L"bar", false) == L"C:\\bar");
		CPPUNIT_ASSERT(dos2.FormatFilename(L"bar", false) == L"C:\\foo\\bar");
	}

	const CServerPath mvs1(L"'FOO.BAR'", MVS);
	const CServerPath mvs2(L"'FOO.BAR.'", MVS);
	CPPUNIT_ASSERT(mvs1.FormatFilename(L"BAZ", false) == L"'FOO.BAR(BAZ)'");
	CPPUNIT_ASSERT(mvs2.FormatFilename(L"BAZ", false) == L"'FOO.BAR.BAZ'");

	const CServerPath vxworks1(L":foo:");
	const CServerPath vxworks2(L":foo:bar");
	const CServerPath vxworks3(L":foo:bar/test");
	CPPUNIT_ASSERT(vxworks1.FormatFilename(L"baz", false) == L":foo:baz");
	CPPUNIT_ASSERT(vxworks2.FormatFilename(L"baz", false) == L":foo:bar/baz");
	CPPUNIT_ASSERT(vxworks3.FormatFilename(L"baz", false) == L":foo:bar/test/baz");

	// ZVM is same as Unix, only makes difference in directory
	// listing parser.
	const CServerPath zvm1(L"/", ZVM);
	const CServerPath zvm2(L"/foo", ZVM);
	CPPUNIT_ASSERT(zvm1.FormatFilename(L"bar", false) == L"/bar");
	CPPUNIT_ASSERT(zvm2.FormatFilename(L"bar", false) == L"/foo/bar");

	const CServerPath hpnonstop1(L"\\mysys", HPNONSTOP);
	const CServerPath hpnonstop2(L"\\mysys.$myvol", HPNONSTOP);
	const CServerPath hpnonstop3(L"\\mysys.$myvol.mysubvol", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop1.FormatFilename(L"foo", false) == L"\\mysys.foo");
	CPPUNIT_ASSERT(hpnonstop2.FormatFilename(L"foo", false) == L"\\mysys.$myvol.foo");
	CPPUNIT_ASSERT(hpnonstop3.FormatFilename(L"foo", false) == L"\\mysys.$myvol.mysubvol.foo");

	const CServerPath dos_virtual1(L"/");
	const CServerPath dos_virtual2(L"/foo");
	CPPUNIT_ASSERT(dos_virtual1.FormatFilename(L"bar", false) == L"/bar");
	CPPUNIT_ASSERT(dos_virtual2.FormatFilename(L"bar", false) == L"/foo/bar");

	const CServerPath cygwin1(L"/", CYGWIN);
	const CServerPath cygwin2(L"/foo", CYGWIN);
	const CServerPath cygwin3(L"//", CYGWIN);
	const CServerPath cygwin4(L"//foo", CYGWIN);
	CPPUNIT_ASSERT(cygwin1.FormatFilename(L"bar", false) == L"/bar");
	CPPUNIT_ASSERT(cygwin2.FormatFilename(L"bar", false) == L"/foo/bar");
	CPPUNIT_ASSERT(cygwin3.FormatFilename(L"bar", false) == L"//bar");
	CPPUNIT_ASSERT(cygwin4.FormatFilename(L"bar", false) == L"//foo/bar");

	{
		const CServerPath dos_slashes1(L"C:\\", DOS_FWD_SLASHES);
		const CServerPath dos_slashes2(L"C:\\foo", DOS_FWD_SLASHES);
		CPPUNIT_ASSERT(dos_slashes1.FormatFilename(L"bar", false) == L"C:/bar");
		CPPUNIT_ASSERT(dos_slashes2.FormatFilename(L"bar", false) == L"C:/foo/bar");
	}

}

void CServerPathTest::testChangePath()
{
	CServerPath unix1(L"/foo/bar");
	CServerPath unix2(L"/foo/bar/baz");
	CServerPath unix3(L"/foo/baz");
	CServerPath unix4(L"/foo/bar/baz");
	CServerPath unix5(L"/foo");
	CPPUNIT_ASSERT(unix1.ChangePath(L"baz") && unix1 == unix2);
	CPPUNIT_ASSERT(unix2.ChangePath(L"../../baz") && unix2 == unix3);
	CPPUNIT_ASSERT(unix3.ChangePath(L"/foo/bar/baz") && unix3 == unix4);
	CPPUNIT_ASSERT(unix3.ChangePath(L".") && unix3 == unix4);
	std::wstring sub = L"../../bar";
	CPPUNIT_ASSERT(unix4.ChangePath(sub, true) && unix4 == unix5 && sub == L"bar");
	sub = L"bar/";
	CPPUNIT_ASSERT(!unix4.ChangePath(sub, true));

	const CServerPath vms1(L"FOO:[BAR]");
	CServerPath vms2(L"FOO:[BAR]");
	CServerPath vms3(L"FOO:[BAR.BAZ]");
	CServerPath vms4(L"FOO:[BAR]");
	CServerPath vms5(L"FOO:[BAR.BAZ.BAR]");
	CServerPath vms6(L"FOO:[BAR]");
	CServerPath vms7(L"FOO:[BAR]");
	CServerPath vms8(L"FOO:[BAR]");
	CServerPath vms9(L"FOO:[BAZ.BAR]");
	CServerPath vms10(L"DOO:[BAZ.BAR]");
	CPPUNIT_ASSERT(vms2.ChangePath(L"BAZ") && vms2 == vms3);
	CPPUNIT_ASSERT(vms4.ChangePath(L"BAZ.BAR") && vms4 == vms5);
	sub = L"BAZ.BAR";
	CPPUNIT_ASSERT(vms6.ChangePath(sub, true) && vms6 == vms1 && sub == L"BAZ.BAR");
	sub = L"[BAZ.BAR]FOO";
	CPPUNIT_ASSERT(!vms7.ChangePath(sub, false));
	CPPUNIT_ASSERT(vms8.ChangePath(sub, true) && vms8 == vms9 && sub == L"FOO");
	CPPUNIT_ASSERT(vms10.ChangePath(L"FOO:[BAR]") && vms10 == vms1);

	{
		const CServerPath dos1(L"c:\\bar");
		CServerPath dos2(L"c:\\bar");
		CServerPath dos3(L"c:\\bar\\baz");
		CServerPath dos4(L"c:\\bar");
		CServerPath dos5(L"c:\\bar\\baz\\bar");
		CServerPath dos6(L"c:\\bar\\baz\\");
		CServerPath dos7(L"d:\\bar");
		CServerPath dos8(L"c:\\bar\\baz");
		CServerPath dos9(L"c:\\bar\\");
		CServerPath dos10(L"md:\\bar\\", DOS);
		CPPUNIT_ASSERT(dos2.ChangePath(L"baz") && dos2 == dos3);
		CPPUNIT_ASSERT(dos4.ChangePath(L"baz\\bar") && dos4 == dos5);
		CPPUNIT_ASSERT(dos5.ChangePath(L"\\bar\\") && dos5 == dos1);
		CPPUNIT_ASSERT(dos6.ChangePath(L"..\\..\\.\\foo\\..\\bar") && dos6 == dos1);
		CPPUNIT_ASSERT(dos7.ChangePath(L"c:\\bar") && dos7 == dos1);
		sub = L"\\bar\\foo";
		CPPUNIT_ASSERT(dos8.ChangePath(sub, true) && dos8 == dos1 && sub == L"foo");
		sub = L"baz\\foo";
		CPPUNIT_ASSERT(dos9.ChangePath(sub, true) && dos9 == dos3 && sub == L"foo");
		CPPUNIT_ASSERT(dos9.ChangePath(L"md:\\bar\\") && dos9 == dos10);
	}

	const CServerPath mvs1(L"'BAR.'", MVS);
	CServerPath mvs2(L"'BAR.'", MVS);
	CServerPath mvs3(L"'BAR.BAZ.'", MVS);
	CServerPath mvs4(L"'BAR.'", MVS);
	CServerPath mvs5(L"'BAR.BAZ.BAR'", MVS);
	CServerPath mvs6(L"'BAR.'", MVS);
	CServerPath mvs7(L"'BAR.'", MVS);
	CServerPath mvs8(L"'BAR.'", MVS);
	CServerPath mvs9(L"'BAR.BAZ'", MVS);
	CServerPath mvs10(L"'BAR.BAZ.'", MVS);
	CServerPath mvs11(L"'BAR.BAZ'", MVS);
	CServerPath mvs12(L"'BAR.BAZ'", MVS);
	CPPUNIT_ASSERT(mvs2.ChangePath(L"BAZ.") && mvs2 == mvs3);
	CPPUNIT_ASSERT(mvs4.ChangePath(L"BAZ.BAR") && mvs4 == mvs5);
	sub = L"BAZ.BAR";
	CPPUNIT_ASSERT(mvs6.ChangePath(sub, true) && mvs6 == mvs3 && sub == L"BAR");
	sub = L"BAZ.BAR.";
	CPPUNIT_ASSERT(!mvs7.ChangePath(sub, true));
	sub = L"BAZ(BAR)";
	CPPUNIT_ASSERT(mvs8.ChangePath(sub, true) && mvs8 == mvs9 && sub == L"BAR");
	CPPUNIT_ASSERT(!mvs5.ChangePath(L"(FOO)"));
	CPPUNIT_ASSERT(!mvs9.ChangePath(L"FOO"));
	sub = L"(FOO)";
	CPPUNIT_ASSERT(!mvs10.ChangePath(sub));
	sub = L"(FOO)";
	CPPUNIT_ASSERT(mvs11.ChangePath(sub, true) && mvs11 == mvs12 && sub == L"FOO");

	const CServerPath vxworks1(L":foo:bar");
	CServerPath vxworks2(L":foo:bar");
	CServerPath vxworks3(L":foo:bar/baz");
	CServerPath vxworks4(L":foo:bar");
	CServerPath vxworks5(L":foo:bar/baz/bar");
	CServerPath vxworks6(L":foo:bar/baz/");
	CServerPath vxworks7(L":bar:bar");
	CServerPath vxworks8(L":foo:bar/baz");
	CServerPath vxworks9(L":foo:bar/baz/bar");
	CPPUNIT_ASSERT(vxworks2.ChangePath(L"baz") && vxworks2 == vxworks3);
	CPPUNIT_ASSERT(vxworks4.ChangePath(L"baz/bar") && vxworks4 == vxworks5);
	CPPUNIT_ASSERT(vxworks6.ChangePath(L"../.././foo/../bar") && vxworks6 == vxworks1);
	CPPUNIT_ASSERT(vxworks7.ChangePath(L":foo:bar") && vxworks7 == vxworks1);
	sub = L"bar/foo";
	CPPUNIT_ASSERT(vxworks8.ChangePath(sub, true) && vxworks8 == vxworks9 && sub == L"foo");

	// ZVM is same as Unix, only makes difference in directory
	// listing parser.
	CServerPath zvm1(L"/foo/bar", ZVM);
	CServerPath zvm2(L"/foo/bar/baz", ZVM);
	CServerPath zvm3(L"/foo/baz", ZVM);
	CServerPath zvm4(L"/foo/bar/baz", ZVM);
	CServerPath zvm5(L"/foo", ZVM);
	CPPUNIT_ASSERT(zvm1.ChangePath(L"baz") && zvm1 == zvm2);
	CPPUNIT_ASSERT(zvm2.ChangePath(L"../../baz") && zvm2 == zvm3);
	CPPUNIT_ASSERT(zvm3.ChangePath(L"/foo/bar/baz") && zvm3 == zvm4);
	CPPUNIT_ASSERT(zvm3.ChangePath(L".") && zvm3 == zvm4);
	sub = L"../../bar";
	CPPUNIT_ASSERT(zvm4.ChangePath(sub, true) && zvm4 == zvm5 && sub == L"bar");
	sub = L"bar/";
	CPPUNIT_ASSERT(!zvm4.ChangePath(sub, true));

	CServerPath hpnonstop1(L"\\mysys.$myvol", HPNONSTOP);
	CServerPath hpnonstop2(L"\\mysys.$myvol.mysubvol", HPNONSTOP);
	CServerPath hpnonstop3(L"\\mysys.$myvol2", HPNONSTOP);
	CServerPath hpnonstop4(L"\\mysys.$myvol.mysubvol", HPNONSTOP);
	CServerPath hpnonstop5(L"\\mysys", HPNONSTOP);
	CPPUNIT_ASSERT(hpnonstop1.ChangePath(L"mysubvol") && hpnonstop1 == hpnonstop2);
	CPPUNIT_ASSERT(hpnonstop3.ChangePath(L"\\mysys.$myvol.mysubvol") && hpnonstop3 == hpnonstop4);
	sub = L"bar";
	CPPUNIT_ASSERT(hpnonstop2.ChangePath(sub, true) && hpnonstop2 == hpnonstop4 && sub == L"bar");
	sub = L"$myvol.mysubvol.bar";
	CPPUNIT_ASSERT(hpnonstop5.ChangePath(sub, true) && hpnonstop5 == hpnonstop4 && sub == L"bar");
	sub = L"bar.";
	CPPUNIT_ASSERT(!hpnonstop4.ChangePath(sub, true));

	CServerPath dos_virtual1(L"\\foo\\bar");
	CServerPath dos_virtual2(L"\\foo\\bar\\baz");
	CServerPath dos_virtual3(L"\\foo\\baz");
	CServerPath dos_virtual4(L"\\foo\\bar\\baz");
	CServerPath dos_virtual5(L"\\foo");
	CPPUNIT_ASSERT(dos_virtual1.ChangePath(L"baz") && dos_virtual1 == dos_virtual2);
	CPPUNIT_ASSERT(dos_virtual2.ChangePath(L"..\\..\\baz") && dos_virtual2 == dos_virtual3);
	CPPUNIT_ASSERT(dos_virtual3.ChangePath(L"\\foo\\bar\\baz") && dos_virtual3 == dos_virtual4);
	CPPUNIT_ASSERT(dos_virtual3.ChangePath(L".") && dos_virtual3 == dos_virtual4);
	sub = L"..\\..\\bar";
	CPPUNIT_ASSERT(dos_virtual4.ChangePath(sub, true) && dos_virtual4 == dos_virtual5 && sub == L"bar");
	sub = L"bar\\";
	CPPUNIT_ASSERT(!dos_virtual4.ChangePath(sub, true));

	CServerPath cygwin1(L"/foo/bar", CYGWIN);
	CServerPath cygwin2(L"/foo/bar/baz", CYGWIN);
	CServerPath cygwin3(L"/foo/baz", CYGWIN);
	CServerPath cygwin4(L"/foo/bar/baz", CYGWIN);
	CServerPath cygwin5(L"/foo", CYGWIN);
	CServerPath cygwin6(L"//foo", CYGWIN);
	CServerPath cygwin7(L"//", CYGWIN);
	CPPUNIT_ASSERT(cygwin1.ChangePath(L"baz") && cygwin1 == cygwin2);
	CPPUNIT_ASSERT(cygwin2.ChangePath(L"../../baz") && cygwin2 == cygwin3);
	CPPUNIT_ASSERT(cygwin3.ChangePath(L"/foo/bar/baz") && cygwin3 == cygwin4);
	CPPUNIT_ASSERT(cygwin3.ChangePath(L".") && cygwin3 == cygwin4);
	sub = L"../../bar";
	CPPUNIT_ASSERT(cygwin4.ChangePath(sub, true) && cygwin4 == cygwin5 && sub == L"bar");
	sub = L"bar/";
	CPPUNIT_ASSERT(!cygwin4.ChangePath(sub, true));
	CPPUNIT_ASSERT(cygwin5.ChangePath(L"//foo") && cygwin5 == cygwin6);
	sub = L"//foo";
	CPPUNIT_ASSERT(cygwin1.ChangePath(sub, true) && cygwin1 == cygwin7 && sub == L"foo");

	{
		const CServerPath dos_slashes1(L"c:\\bar", DOS_FWD_SLASHES);
		CServerPath dos_slashes2(L"c:\\bar", DOS_FWD_SLASHES);
		CServerPath dos_slashes3(L"c:\\bar/baz", DOS_FWD_SLASHES);
		CServerPath dos_slashes4(L"c:\\bar", DOS_FWD_SLASHES);
		CServerPath dos_slashes5(L"c:\\bar\\baz\\bar", DOS_FWD_SLASHES);
		CServerPath dos_slashes6(L"c:\\bar\\baz\\", DOS_FWD_SLASHES);
		CServerPath dos_slashes7(L"d:/bar", DOS_FWD_SLASHES);
		CServerPath dos_slashes8(L"c:\\bar\\baz", DOS_FWD_SLASHES);
		CServerPath dos_slashes9(L"c:\\bar\\", DOS_FWD_SLASHES);
		CServerPath dos_slashes10(L"md:\\bar\\", DOS_FWD_SLASHES);
		CPPUNIT_ASSERT(dos_slashes2.ChangePath(L"baz") && dos_slashes2 == dos_slashes3);
		CPPUNIT_ASSERT(dos_slashes4.ChangePath(L"baz\\bar") && dos_slashes4 == dos_slashes5);
		CPPUNIT_ASSERT(dos_slashes5.ChangePath(L"\\bar\\") && dos_slashes5 == dos_slashes1);
		CPPUNIT_ASSERT(dos_slashes6.ChangePath(L"..\\..\\.\\foo\\..\\bar") && dos_slashes6 == dos_slashes1);
		CPPUNIT_ASSERT(dos_slashes7.ChangePath(L"c:\\bar") && dos_slashes7 == dos_slashes1);
		sub = L"\\bar\\foo";
		CPPUNIT_ASSERT(dos_slashes8.ChangePath(sub, true) && dos_slashes8 == dos_slashes1 && sub == L"foo");
		sub = L"baz\\foo";
		CPPUNIT_ASSERT(dos_slashes9.ChangePath(sub, true) && dos_slashes9 == dos_slashes3 && sub == L"foo");
		CPPUNIT_ASSERT(dos_slashes9.ChangePath(L"md:\\bar\\") && dos_slashes9 == dos_slashes10);
	}

}
