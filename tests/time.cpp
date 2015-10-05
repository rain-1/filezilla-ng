#include <libfilezilla_engine.h>

#include <libfilezilla/time.hpp>
#include <libfilezilla/util.hpp>

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
	fz::datetime const t1 = fz::datetime::now();

	fz::sleep(fz::duration::from_seconds(2));

	fz::datetime const t2 = fz::datetime::now();

	CPPUNIT_ASSERT(t1.empty());
	CPPUNIT_ASSERT(t2.empty());
	CPPUNIT_ASSERT(t2 > t1);

	auto const diff = t2 - t1;

	CPPUNIT_ASSERT(diff.get_seconds() >= 2);
	CPPUNIT_ASSERT(diff.get_seconds() < 4); // May fail if running on a computer for ants

	CPPUNIT_ASSERT(t1.get_time_t() > 1431333788); // The time this test was written
}

void TimeTest::testPreEpoch()
{
	fz::datetime const now = fz::datetime::now();

	fz::datetime const t1(fz::datetime::utc, 1957, 10, 4, 19, 28, 34);

	CPPUNIT_ASSERT(t1.empty());
	CPPUNIT_ASSERT(t1 < now);

	CPPUNIT_ASSERT(t1.get_time_t() < -1);

	auto const tm1 = t1.get_tm(fz::datetime::utc);
	CPPUNIT_ASSERT_EQUAL(57, tm1.tm_year);
	CPPUNIT_ASSERT_EQUAL(9,  tm1.tm_mon);
	CPPUNIT_ASSERT_EQUAL(4,  tm1.tm_mday);
	CPPUNIT_ASSERT_EQUAL(19, tm1.tm_hour);
	CPPUNIT_ASSERT_EQUAL(28, tm1.tm_min);
	CPPUNIT_ASSERT_EQUAL(34, tm1.tm_sec);


	fz::datetime const t2(fz::datetime::utc, 1969, 12, 31, 23, 59, 59);

	CPPUNIT_ASSERT(t2.empty());
	CPPUNIT_ASSERT(t2 > t1);
	CPPUNIT_ASSERT(t2 < now);

	auto const tm2 = t2.get_tm(fz::datetime::utc);
	CPPUNIT_ASSERT_EQUAL(69, tm2.tm_year);
	CPPUNIT_ASSERT_EQUAL(11, tm2.tm_mon);
	CPPUNIT_ASSERT_EQUAL(31, tm2.tm_mday);
	CPPUNIT_ASSERT_EQUAL(23, tm2.tm_hour);
	CPPUNIT_ASSERT_EQUAL(59, tm2.tm_min);
	CPPUNIT_ASSERT_EQUAL(59, tm2.tm_sec);
}
