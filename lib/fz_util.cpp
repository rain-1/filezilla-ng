#include "fz_util.hpp"
#include "fz_time.hpp"

#include <time.h>

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

}
