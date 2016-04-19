noinst_LIBRARIES = libfzsftp_ux.a libfzputtycommon_ux.a

AM_CPPFLAGS = -I$(srcdir)/.. -I../../include

libfzputtycommon_ux_a_SOURCES = uxcons.c \
			     uxmisc.c \
			     uxnoise.c \
			     uxstore.c

libfzputtycommon_ux_a_CPPFLAGS = $(AM_CPPFLAGS) -DNO_GSSAPI -D_FILE_OFFSET_BITS=64 $(NETTLE_CFLAGS)

libfzsftp_ux_a_SOURCES = \
			uxsftp.c \
			uxnoise.c \
			uxagentc.c \
			uxsel.c \
			uxnet.c \
			uxpeer.c \
			uxproxy.c \
			uxshare.c

libfzsftp_ux_a_CPPFLAGS = $(AM_CPPFLAGS) -DNO_GSSAPI -D_FILE_OFFSET_BITS=64 $(NETTLE_CFLAGS)

noinst_HEADERS = unix.h
