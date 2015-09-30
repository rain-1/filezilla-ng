#ifndef LIBFILEZILLA_UTIL_HEADER
#define LIBFILEZILLA_UTIL_HEADER

namespace fz {

class duration;

// While there is std::this_thread::sleep_for, we can't use it due to MinGW not
// implementing thread.
void sleep(duration const& d);

}

#endif