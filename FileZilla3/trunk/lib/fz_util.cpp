#include "fz_util.hpp"
#include "fz_time.hpp"

#include <cassert>
#include <random>

#include <time.h>

#if defined(FZ_WINDOWS) && !defined(_MSC_VER)
#include "private/windows.hpp"
#include <wincrypt.h>
#endif

namespace fz {

void sleep(duration const& d)
{
#ifdef FZ_WINDOWS
	Sleep(static_cast<DWORD>(d.get_milliseconds()));
#else
	timespec ts{};
	ts.tv_sec = 2;
	nanosleep(&ts, 0);
#endif
}

namespace {
#if defined(FZ_WINDOWS) && !defined(_MSC_VER)
// Unfortunately MiNGW does not have a working random_device
// Implement our own in terms of CryptGenRandom.
// Fall back to time-seeded mersenne twister on error
struct provider
{
	provider()
	{
		if (!CryptAcquireContextW(&h_, 0, 0, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
			h_ = 0;
		}
		mt_.seed(fz::datetime::now().get_time_t());
	}
	~provider()
	{
		if (h_) {
			CryptReleaseContext(h_, 0);
		}
	}

	HCRYPTPROV h_{};
	std::mt19937_64 mt_;
};

struct working_random_device
{
	typedef uint64_t result_type;

	constexpr static result_type min() { return std::numeric_limits<result_type>::min(); }
	constexpr static result_type max() { return std::numeric_limits<result_type>::max(); }

	result_type operator()()
	{
		thread_local provider prov;

		result_type ret{};
		if (!prov.h_ || !CryptGenRandom(prov.h_, sizeof(ret), reinterpret_cast<BYTE*>(&ret))) {
			ret = prov.mt_();
		}

		return ret;
	}
};

static_assert(working_random_device::max() == std::mt19937_64::max(), "Unsupported std::mt19937_64::max()");
static_assert(working_random_device::min() == std::mt19937_64::min(), "Unsupported std::mt19937_64::min()");
#else
typedef std::random_device working_random_device;
#endif
}

int64_t random_number(int64_t min, int64_t max)
{
	assert(min <= max);
	if (min >= max)
		return min;

	std::uniform_int_distribution<int64_t> dist(min, max);
	working_random_device rd;
	return dist(rd);
}

}
