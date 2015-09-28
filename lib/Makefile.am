noinst_LIBRARIES = libfilezilla.a

libfilezilla_a_CPPFLAGS = -I$(srcdir)/../src/include

libfilezilla_a_SOURCES = \
	fz_mutex.cpp \
	fz_string.cpp \
	fz_thread.cpp \
	fz_time.cpp

noinst_HEADERS = \
	fz_config.hpp \
	fz_mutex.hpp \
	fz_string.hpp \
	fz_thread.hpp \
	fz_time.hpp \
	libfilezilla.hpp \
	private/windows.hpp

dist_noinst_DATA = libfilezilla.vcxproj
