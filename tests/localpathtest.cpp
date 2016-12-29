#include <filezilla.h>
#include <cppunit/extensions/HelperMacros.h>
#include "local_path.h"

/*
 * This testsuite asserts the correctness of the CLocalPathTest class.
 */

class CLocalPathTest final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(CLocalPathTest);
	CPPUNIT_TEST(testSetPath);
	CPPUNIT_TEST(testChangePath);
	CPPUNIT_TEST(testHasParent);
#ifdef __WXMSW__
	CPPUNIT_TEST(testHasLogicalParent);
#endif
	CPPUNIT_TEST(testAddSegment);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void testSetPath();
	void testChangePath();
	void testHasParent();
#ifdef __WXMSW__
	void testHasLogicalParent();
#endif
	void testAddSegment();

protected:
};

CPPUNIT_TEST_SUITE_REGISTRATION(CLocalPathTest);

void CLocalPathTest::testSetPath()
{
#ifdef __WXMSW__
	CPPUNIT_ASSERT(CLocalPath(L"\\").GetPath() == L"\\");

	CPPUNIT_ASSERT(CLocalPath(L"C:").GetPath() == L"C:\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\").GetPath() == L"C:\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\.").GetPath() == L"C:\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\.\\").GetPath() == L"C:\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\.").GetPath() == L"C:\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\..").GetPath() == L"C:\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\..\\").GetPath() == L"C:\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\foo").GetPath() == L"C:\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\..\\foo\\").GetPath() == L"C:\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"C:\\foo\\..\\bar").GetPath() == L"C:\\bar\\");

	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo").GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo\\").GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo/").GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo/..").GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo\\.").GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo\\.\\").GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo\\bar\\").GetPath() == L"\\\\foo\\bar\\");
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo\\bar\\.\\..").GetPath() == L"\\\\foo\\");
#else
	CPPUNIT_ASSERT(CLocalPath(L"/").GetPath() == L"/");
	CPPUNIT_ASSERT(CLocalPath(L"/foo").GetPath() == L"/foo/");
	CPPUNIT_ASSERT(CLocalPath(L"//foo//").GetPath() == L"/foo/");
	CPPUNIT_ASSERT(CLocalPath(L"/foo/../foo").GetPath() == L"/foo/");
	CPPUNIT_ASSERT(CLocalPath(L"/foo/..").GetPath() == L"/");
	CPPUNIT_ASSERT(CLocalPath(L"/..").GetPath() == L"/");
	CPPUNIT_ASSERT(CLocalPath(L"/foo/.").GetPath() == L"/foo/");
	CPPUNIT_ASSERT(CLocalPath(L"/foo/./").GetPath() == L"/foo/");
	CPPUNIT_ASSERT(CLocalPath(L"/foo/bar/").GetPath() == L"/foo/bar/");
	CPPUNIT_ASSERT(CLocalPath(L"/foo/bar/./..").GetPath() == L"/foo/");
#endif
}

void CLocalPathTest::testChangePath()
{
#ifdef __WXMSW__
	CLocalPath p1(L"C:\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"\\") && p1.GetPath() == L"\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"C:") && p1.GetPath() == L"C:\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"C:\\.") && p1.GetPath() == L"C:\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"C:\\..") && p1.GetPath() == L"C:\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"foo") && p1.GetPath() == L"C:\\foo\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"..") && p1.GetPath() == L"C:\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"..") && p1.GetPath() == L"C:\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"C:\\foo") && p1.GetPath() == L"C:\\foo\\");
	CPPUNIT_ASSERT(p1.ChangePath(L".") && p1.GetPath() == L"C:\\foo\\");
	CPPUNIT_ASSERT(p1.ChangePath(L"..\\bar") && p1.GetPath() == L"C:\\bar\\");

	CLocalPath p2;
	CPPUNIT_ASSERT(p2.ChangePath(L"\\\\foo") && p2.GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(p2.ChangePath(L".") && p2.GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(p2.ChangePath(L"..") && p2.GetPath() == L"\\\\foo\\");
	CPPUNIT_ASSERT(p2.ChangePath(L"..\\bar\\.\\baz\\..") && p2.GetPath() == L"\\\\foo\\bar\\");
#else
#endif
}

void CLocalPathTest::testHasParent()
{
#ifdef __WXMSW__
	CPPUNIT_ASSERT(!CLocalPath(L"\\").HasParent());

	CPPUNIT_ASSERT(!CLocalPath(L"C:\\").HasParent());
	CPPUNIT_ASSERT(CLocalPath(L"C:\\foo").HasParent());
	CPPUNIT_ASSERT(CLocalPath(L"c:\\foo\\bar\\").HasParent());

	CPPUNIT_ASSERT(!CLocalPath(L"\\\\foo").HasParent());
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo\\bar").HasParent());
#else
	CPPUNIT_ASSERT(!CLocalPath(L"/").HasParent());
	CPPUNIT_ASSERT(CLocalPath(L"/foo").HasParent());
	CPPUNIT_ASSERT(CLocalPath(L"/foo/bar").HasParent());
#endif
}

#ifdef __WXMSW__
void CLocalPathTest::testHasLogicalParent()
{
	CPPUNIT_ASSERT(!CLocalPath(L"\\").HasLogicalParent());

	CPPUNIT_ASSERT(CLocalPath(L"C:\\").HasLogicalParent()); // This one's only difference
	CPPUNIT_ASSERT(CLocalPath(L"C:\\foo").HasLogicalParent());
	CPPUNIT_ASSERT(CLocalPath(L"c:\\foo\\bar\\").HasLogicalParent());

	CPPUNIT_ASSERT(!CLocalPath(L"\\\\foo").HasLogicalParent());
	CPPUNIT_ASSERT(CLocalPath(L"\\\\foo\\bar").HasLogicalParent());
}
#endif

void CLocalPathTest::testAddSegment()
{
#ifdef __WXMSW__
	CLocalPath a(L"c:\\foo");
#else
	CLocalPath a(L"/foo");
#endif
	CLocalPath b(a);

	a.AddSegment(L"");
	CPPUNIT_ASSERT(a == b);
	CPPUNIT_ASSERT(a.GetPath() == b.GetPath());
}

