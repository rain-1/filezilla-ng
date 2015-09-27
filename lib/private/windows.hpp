#ifndef LIBFILEZILLA_PRIVATE_WINDOWS_HEADER
#define LIBFILEZILLA_PRIVATE_WINDOWS_HEADER

#ifndef FZ_WINDOWS
#error "You included a file you should not include"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Don't let Windows headers #define min/max, clashes with std::min/max
#ifndef NOMINMAX
#define NOMINMAX
#endif

// IE 7 or higher
#ifndef _WIN32_IE
#define _WIN32_IE 0x0700
#elif _WIN32_IE <= 0x0700
#undef _WIN32_IE
#define _WIN32_IE 0x0700
#endif

// Windows Vista or higher
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#elif _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

// Windows Vista or higher
#ifndef WINVER
#define WINVER 0x0600
#elif WINVER < 0x0600
#undef WINVER
#define WINVER 0x0600
#endif

#include <windows.h>

#endif