bin_PROGRAMS = fzstorj

fzstorj_SOURCES = \
		fzstorj.cpp

fzstorj_LDADD = $(LIBFILEZILLA_LIBS) $(LIBSTORJ_LIBS)

fzstorj_CPPFLAGS = $(LIBFILEZILLA_CFLAGS) $(LIBSTORJ_CFLAGS)

noinst_HEADERS = events.hpp

if MACAPPBUNDLE
noinst_DATA = $(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj$(EXEEXT)
endif

$(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj$(EXEEXT): fzstorj
	mkdir -p $(top_builddir)/FileZilla.app/Contents/MacOS
	cp -f fzstorj $(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj
