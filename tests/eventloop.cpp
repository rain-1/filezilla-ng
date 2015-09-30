#include <libfilezilla_engine.h>

#include <event_handler.h>
#include <event_loop.h>

#include <cppunit/extensions/HelperMacros.h>

class EventloopTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(EventloopTest);
	CPPUNIT_TEST(testSimple);
	CPPUNIT_TEST(testFilter);
	CPPUNIT_TEST(testCondition);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void testSimple();
	void testFilter();
	void testCondition();
};

CPPUNIT_TEST_SUITE_REGISTRATION(EventloopTest);

namespace {
struct type1;
typedef fz::simple_event<type1> T1;

struct type2;
typedef fz::simple_event<type2, int> T2;

struct type3;
typedef fz::simple_event<type3> T3;

struct type4;
typedef fz::simple_event<type4> T4;

class target : public fz::event_handler
{
public:
	target(fz::event_loop & l)
	: fz::event_handler(l)
	{}

	virtual ~target()
	{
		remove_handler();
	}

	void a()
	{
		++a_;
		send_event<T2>(5);
	}

	void b(int v)
	{
		++b_;

		CPPUNIT_ASSERT_EQUAL(v, 5);
	}

	void c()
	{
		send_event<T4>();
	}

	void d()
	{
		fz::scoped_lock l(m_);
		cond_.signal(l);
	}

	virtual void operator()(fz::event_base const& ev) override {
		CPPUNIT_ASSERT((fz::dispatch<T1, T2, T3, T4>(ev, this, &target::a, &target::b, &target::c, &target::d)));
	}

	int a_{};
	int b_{};


	fz::mutex m_;
	fz::condition cond_;
};
}

void EventloopTest::testSimple()
{
	fz::event_loop loop;

	target t(loop);

	for (int i = 0; i < 1000; ++i) {
		t.send_event<T1>();
	}

	t.send_event<T3>();

	fz::scoped_lock l(t.m_);
	CPPUNIT_ASSERT(t.cond_.wait(l, 1000));

	CPPUNIT_ASSERT_EQUAL(t.a_, 1000);
	CPPUNIT_ASSERT_EQUAL(t.b_, 1000);
}

namespace {
class target2 : public fz::event_handler
{
public:
	target2(fz::event_loop & l)
	: fz::event_handler(l)
	{}

	virtual ~target2()
	{
		remove_handler();
	}

	void a()
	{
		{
			fz::scoped_lock l(m_);
			CPPUNIT_ASSERT(cond2_.wait(l, 1000));
		}

		auto f = [&](fz::event_loop::Events::value_type& ev) -> bool {
			if (ev.second->derived_type() == T1::type()) {
				++c_;
				return true;
			}

			if (ev.second->derived_type() == T2::type()) {
				++d_;
				std::get<0>(static_cast<T2&>(*ev.second).v_) += 4;
			}
			return false;

		};
		event_loop_.filter_events(f);
		++a_;
	}

	void b(int v)
	{
		b_ += v;
	}

	void c()
	{
		fz::scoped_lock l(m_);
		cond_.signal(l);
	}

	virtual void operator()(fz::event_base const& ev) override {
		CPPUNIT_ASSERT((fz::dispatch<T1, T2, T3>(ev, this, &target2::a, &target2::b, &target2::c)));
	}

	int a_{};
	int b_{};
	int c_{};
	int d_{};

	fz::mutex m_;
	fz::condition cond_;
	fz::condition cond2_;
};
}

void EventloopTest::testFilter()
{
	fz::event_loop loop;

	target2 t(loop);

	for (int i = 0; i < 10; ++i) {
		t.send_event<T1>();
	}
	t.send_event<T2>(3);
	t.send_event<T2>(5);

	t.send_event<T3>();

	fz::scoped_lock l(t.m_);
	t.cond2_.signal(l);

	CPPUNIT_ASSERT(t.cond_.wait(l, 1000));

	CPPUNIT_ASSERT_EQUAL(t.a_, 1);
	CPPUNIT_ASSERT_EQUAL(t.b_, 16);
	CPPUNIT_ASSERT_EQUAL(t.c_, 9);
	CPPUNIT_ASSERT_EQUAL(t.d_, 2);
}

void EventloopTest::testCondition()
{
	// Make sure condition::wait works correctly.

	auto const t1 = fz::monotonic_clock::now();

	fz::mutex m;
	fz::condition c;

	fz::scoped_lock l(m);
	CPPUNIT_ASSERT(!c.wait(l, 200));

	auto const t2 = fz::monotonic_clock::now();

	// Due to rounding errors things can be off for one millisecond, allow it.
	CPPUNIT_ASSERT((t2 - t1) >= fz::duration::from_milliseconds(199));
}
