#ifndef LIBFILEZILLA_ENGINE_HEADER
#define LIBFILEZILLA_ENGINE_HEADER

#ifdef HAVE_CONFIG_H
  #include <config.h>
#endif

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "FileZilla 3"
#endif

#include <libfilezilla/libfilezilla.hpp>

#include "setup.h"

#ifdef FZ_WINDOWS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef STRICT
#define STRICT 1
#endif
#include <windows.h>
#endif

#include <list>
#include <vector>
#include <map>

#include "optionsbase.h"
#include "logging.h"
#include "server.h"
#include "serverpath.h"
#include "commands.h"
#include "notification.h"
#include "FileZillaEngine.h"
#include "directorylisting.h"

#include "misc.h"

#endif
