#include <libfilezilla.h>

#include <timeex.h>

#include <cppunit/extensions/HelperMacros.h>

#include <unistd.h>

class TimeTest final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(TimeTest);
	CPPUNIT_TEST(testNow);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void testNow();
};

CPPUNIT_TEST_SUITE_REGISTRATION(TimeTest);

void TimeTest::testNow()
{
	CDateTime const t1 = CDateTime::Now();

	usleep(2000000);

	CDateTime const t2 = CDateTime::Now();

	CPPUNIT_ASSERT(t1.IsValid());
	CPPUNIT_ASSERT(t2.IsValid());
	CPPUNIT_ASSERT(t2 > t1);

	auto const diff = t2 - t1;

	CPPUNIT_ASSERT(diff.get_seconds() >= 2);
	CPPUNIT_ASSERT(diff.get_seconds() < 4); // May fail if running on a computer for ants

	CPPUNIT_ASSERT(t1.GetTimeT() > 1431333788); // The time this test was written
}
