#ifndef LIBFILEZILLA_ENGINE_HEADER
#define LIBFILEZILLA_ENGINE_HEADER

#ifdef HAVE_CONFIG_H
  #include <config.h>
#endif

#ifndef PACKAGE_STRING
#define PACKAGE_STRING "FileZilla 3"
#endif

#include <libfilezilla/libfilezilla.hpp>

#include <wx/defs.h>

#include "setup.h"

#ifdef FZ_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef STRICT
#define STRICT 1
#endif
#include <windows.h>
#endif

#include <wx/string.h>

#include <list>
#include <vector>
#include <map>

#include <libfilezilla/glue/wx.hpp>

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
