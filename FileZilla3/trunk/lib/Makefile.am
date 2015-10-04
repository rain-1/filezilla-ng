noinst_LIBRARIES = libfilezilla.a

libfilezilla_a_CPPFLAGS = -I$(srcdir)/../src/include

libfilezilla_a_SOURCES = \
	fz_event_handler.cpp \
	fz_event_loop.cpp \
	fz_file.cpp \
	fz_mutex.cpp \
	fz_process.cpp \
	fz_string.cpp \
	fz_thread.cpp \
	fz_time.cpp \
	fz_util.cpp

noinst_HEADERS = \
	fz_apply.hpp \
	fz_config.hpp \
	fz_event.hpp \
	fz_event_handler.hpp \
	fz_event_loop.hpp \
	fz_file.hpp \
	fz_mutex.hpp \
	fz_process.hpp \
	fz_string.hpp \
	fz_thread.hpp \
	fz_time.hpp \
	fz_util.hpp \
	libfilezilla.hpp \
	private/windows.hpp

dist_noinst_DATA = libfilezilla.vcxproj
