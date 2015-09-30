#ifndef LIBFILEZILLA_UTIL_HEADER
#define LIBFILEZILLA_UTIL_HEADER

#include <cstdint>

namespace fz {

class duration;

// While there is std::this_thread::sleep_for, we can't use it due to MinGW not
// implementing thread.
void sleep(duration const& d);

// Get a secure random integer uniformly distributed in the closed interval [min, max]
int64_t random_number(int64_t min, int64_t max);

}

#endif