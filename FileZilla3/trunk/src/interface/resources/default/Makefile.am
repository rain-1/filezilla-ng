resourcesdir = $(pkgdatadir)/resources/default
resources480dir = $(pkgdatadir)/resources/default/480x480

dist_resources_DATA = theme.xml

dist_resources480_DATA = \
		480x480/ascii.png \
		480x480/auto.png \
		480x480/binary.png \
		480x480/bookmark.png \
		480x480/bookmarks.png \
		480x480/cancel.png \
		480x480/compare.png \
		480x480/disconnect.png \
		480x480/download.png \
		480x480/downloadadd.png \
		480x480/file.png \
		480x480/filter.png \
		480x480/find.png \
		480x480/folder.png \
		480x480/folderback.png \
		480x480/folderclosed.png \
		480x480/folderup.png \
		480x480/help.png \
		480x480/localtreeview.png \
		480x480/lock.png \
		480x480/logview.png \
		480x480/processqueue.png \
		480x480/queueview.png \
		480x480/reconnect.png \
		480x480/refresh.png \
		480x480/remotetreeview.png \
		480x480/server.png \
		480x480/showhidden.png \
		480x480/sitemanager.png \
		480x480/speedlimits.png \
		480x480/symlink.png \
		480x480/synchronize.png \
		480x480/synctransfer.png \
		480x480/upload.png \
		480x480/uploadadd.png

if MACAPPBUNDLE
mac-copy-resources: $(dist_resources_DATA)
	mkdir -p "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/default/480x480";
	for i in $(dist_resources480_DATA); do \
	  cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/default/480x480"; \
	done
	for i in $(dist_resources_DATA); do \
	  cp -f "$(srcdir)/$$i" "$(top_builddir)/FileZilla.app/Contents/SharedSupport/resources/default"; \
	done
endif

