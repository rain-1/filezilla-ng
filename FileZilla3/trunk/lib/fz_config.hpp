#ifndef LIBFILEZILLA_CONFIG_HEADER
#define LIBFILEZILLA_CONFIG_HEADER

// Set some mandatory defines which are used to select platform implementations
#if !defined(FZ_WINDOWS) && !defined(FZ_MAC) && !defined(FZ_UNIX)
	#if defined(_WIN32) || defined(_WIN64)
		#define FZ_WINDOWS 1
	#elif defined(__APPLE__)
		#define FZ_MAC 1
	#else
		#define FZ_UNIX 1
	#endif
#endif

#ifdef HAVE_CONFIG_H
	#include <config.h>
#endif

#endif
