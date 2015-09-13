#include <libfilezilla.h>

#include <timeex.h>

#include <cppunit/extensions/HelperMacros.h>

#include <unistd.h>

class TimeTest final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(TimeTest);
	CPPUNIT_TEST(testNow);
	CPPUNIT_TEST(testPreEpoch);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void testNow();
	void testPreEpoch();
};

CPPUNIT_TEST_SUITE_REGISTRATION(TimeTest);

void TimeTest::testNow()
{
	CDateTime const t1 = CDateTime::Now();

#ifdef __WXMSW__
	Sleep(2000);
#else
	timespec ts{};
	ts.tv_sec = 2;
	nanosleep(&ts, 0);
	usleep(2000000);
#endif

	CDateTime const t2 = CDateTime::Now();

	CPPUNIT_ASSERT(t1.IsValid());
	CPPUNIT_ASSERT(t2.IsValid());
	CPPUNIT_ASSERT(t2 > t1);

	auto const diff = t2 - t1;

	CPPUNIT_ASSERT(diff.get_seconds() >= 2);
	CPPUNIT_ASSERT(diff.get_seconds() < 4); // May fail if running on a computer for ants

	CPPUNIT_ASSERT(t1.GetTimeT() > 1431333788); // The time this test was written
}

void TimeTest::testPreEpoch()
{
	CDateTime const now = CDateTime::Now();

	CDateTime const t1(CDateTime::utc, 1957, 10, 4, 19, 28, 34);

	CPPUNIT_ASSERT(t1.IsValid());
	CPPUNIT_ASSERT(t1 < now);

	CPPUNIT_ASSERT(t1.GetTimeT() < -1);

	auto const tm1 = t1.GetTm(CDateTime::utc);
	CPPUNIT_ASSERT_EQUAL(tm1.tm_year, 57);
	CPPUNIT_ASSERT_EQUAL(tm1.tm_mon, 9);
	CPPUNIT_ASSERT_EQUAL(tm1.tm_mday, 4);
	CPPUNIT_ASSERT_EQUAL(tm1.tm_hour, 19);
	CPPUNIT_ASSERT_EQUAL(tm1.tm_min, 28);
	CPPUNIT_ASSERT_EQUAL(tm1.tm_sec, 34);


	CDateTime const t2(CDateTime::utc, 1969, 12, 31, 23, 59, 59);

	CPPUNIT_ASSERT(t2.IsValid());
	CPPUNIT_ASSERT(t2 > t1);
	CPPUNIT_ASSERT(t2 < now);

	auto const tm2 = t2.GetTm(CDateTime::utc);
	CPPUNIT_ASSERT_EQUAL(tm2.tm_year, 69);
	CPPUNIT_ASSERT_EQUAL(tm2.tm_mon, 11);
	CPPUNIT_ASSERT_EQUAL(tm2.tm_mday, 31);
	CPPUNIT_ASSERT_EQUAL(tm2.tm_hour, 23);
	CPPUNIT_ASSERT_EQUAL(tm2.tm_min, 59);
	CPPUNIT_ASSERT_EQUAL(tm2.tm_sec, 59);
}
