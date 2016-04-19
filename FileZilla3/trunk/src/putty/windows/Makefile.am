noinst_LIBRARIES = libfzsftp_win.a libfzputtycommon_win.a

AM_CPPFLAGS = -I$(srcdir)/..

COMMON_CFLAGS = $(AM_CFLAGS) -D_WINDOWS -DSECURITY_WIN32 \
		-D_WIN32_IE=0x0700 \
		-DWINVER=0x0600 -D_WIN32_WINNT=0x0600 $(NETTLE_CFLAGS)

libfzputtycommon_win_a_CFLAGS = $(COMMON_CFLAGS)

libfzputtycommon_win_a_SOURCES = wincons.c \
				 winmisc.c \
				 winnoise.c \
				 winstore.c

libfzsftp_win_a_CFLAGS = $(COMMON_CFLAGS)

libfzsftp_win_a_SOURCES = \
		wincapi.c \
		windefs.c \
                winhandl.c \
		winhsock.c \
		winnet.c \
		winnpc.c \
		winnps.c \
		winpgntc.c \
		winsecur.c \
		winsftp.c \
		winshare.c \
		wintime.c

noinst_HEADERS = wincapi.h winsecur.h winstuff.h rcstuff.h

dist_noinst_DATA = psftp.rc version.rc2 pscp.ico windows_manifest.xml

CLEANFILES = pscp.o

if USE_RESOURCEFILE

nodist_noinst_DATA = psftp.o

psftp.o: $(srcdir)/psftp.rc $(srcdir)/windows_manifest.xml
	@if [ -f $@~ ]; then rm $@~; fi
	@WINDRES@ --include-dir=$(srcdir) $(WINDRESFLAGS) -D GNU_WINDRES $(srcdir)/psftp.rc $@~
	@if [ -f $@ ]; then rm $@; fi
	mv $@~ $@
endif
