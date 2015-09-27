#ifndef LIBFILEZILLA_PRIVATE_WINDOWS_HEADER
#define LIBFILEZILLA_PRIVATE_WINDOWS_HEADER

#ifndef FZ_WINDOWS
#error "You included a file you should not include"
#endif

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include <windows.h>

#endif