SUBDIRS = blukis classic cyril default flatzilla lone minimal opencrystal sun tango

CLEANFILES = filezilla.o

resourcesdir = $(pkgdatadir)/resources
dist_resources_DATA = \
	defaultfilters.xml \
	dropdown.png \
	finished.wav \
	leds.png

xrcdir = $(pkgdatadir)/resources/xrc
dist_xrc_DATA = \
	xrc/certificate.xrc \
	xrc/dialogs.xrc \
	xrc/inputdialog.xrc \
	xrc/menus.xrc \
	xrc/netconfwizard.xrc \
	xrc/settings.xrc \
	xrc/sitemanager.xrc \
	xrc/update.xrc

resources16dir = $(pkgdatadir)/resources/16x16
dist_resources16_DATA = \
	16x16/filezilla.png \
	16x16/sort_down_dark.png \
	16x16/sort_down_light.png \
	16x16/sort_up_dark.png \
	16x16/sort_up_light.png \
	16x16/throbber.gif \
	16x16/unknown.png

resources20dir = $(pkgdatadir)/resources/20x20
dist_resources20_DATA = 20x20/unknown.png

resources24dir = $(pkgdatadir)/resources/24x24
dist_resources24_DATA = 24x24/unknown.png

resources32dir = $(pkgdatadir)/resources/32x32
dist_resources32_DATA = 32x32/filezilla.png

resources48dir = $(pkgdatadir)/resources/48x48
dist_resources48_DATA = 48x48/filezilla.png

resources480dir = $(pkgdatadir)/resources/480x480
dist_resources480_DATA = 480x480/filezilla.png

if USE_RESOURCEFILE
nodist_noinst_DATA = filezilla.o

filezilla.o: $(srcdir)/filezilla.rc $(srcdir)/windows_manifest.xml version.rc
	@if [ -f $@~ ]; then rm $@~; fi
	@WINDRES@ --include-dir=. --include-dir=$(srcdir) $(WINDRESFLAGS) $(srcdir)/filezilla.rc -D GNU_WINDRES -D HAS_VERSIONRC $@~ || rm $@~
	@if [ -f $@ ]; then rm $@; fi
	mv $@~ $@
endif

dist_noinst_DATA = \
	filezilla.rc \
	FileZilla.ico \
	FileZilla.icns \
	filezilla.entitlements \
	MacInfo.plist.in \
	version.rc.in \
	windows_manifest.xml

if MACAPPBUNDLE
mac-copy-resources: $(dist_resources_DATA)
	cp MacInfo.plist $(top_builddir)/FileZilla.app/Contents/Info.plist
	cp $(srcdir)/FileZilla.icns $(top_builddir)/FileZilla.app/Contents/Resources/FileZilla.icns

	for i in $(dist_resources_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources"; \
	done
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/xrc"
	for i in $(dist_xrc_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/xrc"; \
	done
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/16x16"
	for i in $(dist_resources16_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/16x16"; \
	done
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/20x20"
	for i in $(dist_resources20_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/20x20"; \
	done
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/24x24"
	for i in $(dist_resources24_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/24x24"; \
	done
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/32x32"
	for i in $(dist_resources32_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/32x32"; \
	done
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/48x48"
	for i in $(dist_resources48_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/48x48"; \
	done
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/480x480"
	for i in $(dist_resources480_DATA); \
		do cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/480x480"; \
	done

	$(MAKE) -C classic mac-copy-resources
	$(MAKE) -C cyril mac-copy-resources
	$(MAKE) -C blukis mac-copy-resources
	$(MAKE) -C default mac-copy-resources
	$(MAKE) -C lone mac-copy-resources
	$(MAKE) -C opencrystal mac-copy-resources
	$(MAKE) -C tango mac-copy-resources
endif

pixmapsdir = $(datadir)/pixmaps
pixmaps_DATA = 48x48/filezilla.png

hicolorapps16dir = $(datadir)/icons/hicolor/16x16/apps
hicolorapps16_DATA = 16x16/filezilla.png
hicolorapps32dir = $(datadir)/icons/hicolor/32x32/apps
hicolorapps32_DATA = 32x32/filezilla.png
hicolorapps48dir = $(datadir)/icons/hicolor/48x48/apps
hicolorapps48_DATA = 48x48/filezilla.png
hicolorapps480dir = $(datadir)/icons/hicolor/480x480/apps
hicolorapps480_DATA = 480x480/filezilla.png

scalabledir = $(datadir)/icons/hicolor/scalable/apps
dist_scalable_DATA = filezilla.svg
