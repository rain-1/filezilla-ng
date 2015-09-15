if MINGW
  MAYBE_FZSHELLEXT = fzshellext/32 fzshellext/64
endif

if HAVE_LIBPUGIXML
else
  MAYBE_PUGIXML = pugixml
endif

if HAVE_DBUS
  MAYBE_DBUS = dbus
endif

SUBDIRS = include engine $(MAYBE_PUGIXML) $(MAYBE_DBUS) interface putty $(MAYBE_FZSHELLEXT) .
DIST_SUBDIRS = include engine pugixml dbus interface putty fzshellext/64 .

dist_noinst_DATA = FileZilla.sln Dependencies.props.example

dist-hook:
	mkdir -p $(distdir)/fzshellext/32
	mv -f $(distdir)/fzshellext/64/* $(distdir)/fzshellext
	rm -rf $(distdir)/config
	rm -rf $(distdir)/m4
