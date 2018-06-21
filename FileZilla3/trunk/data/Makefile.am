dist_noinst_DATA = install.nsi.in \
		dllcopy.sh \
		makezip.sh.in \
		nsis_appid.dll \
		nsis_appid.txt \
		process_running.nsh \
		UAC.nsh \
		UAC.dll \
		UAC_unicode.dll.txt \
		uninstall.ico

if MAKENSISSCRIPT

if LOCALES
  MAYBE_LANGFILES = installlangfiles.nsh uninstalllangfiles.nsh
endif

noinst_DATA = $(MAYBE_LANGFILES) libtoolexecutablesubdir.nsh dll_install.nsh

dll_install.nsh: Makefile
	@rm -f dll_install.nsh
	@rm -f dll_uninstall.nsh
	@rm -rf dlls
	@mkdir -p dlls
	"$(srcdir)/dllcopy.sh" "$(top_builddir)/src/interface" filezilla.exe $(OBJDUMP) $(CXX) $$PATH
	"$(srcdir)/dllcopy.sh" "$(top_builddir)/src/putty" fzsftp.exe $(OBJDUMP) $(CXX) $$PATH
	"$(srcdir)/dllcopy.sh" "$(top_builddir)/src/putty" fzputtygen.exe $(OBJDUMP) $(CXX) $$PATH
	if test -f "${top_builddir}/src/storj/fzstorj.exe"; then \
		"$(srcdir)/dllcopy.sh" "$(top_builddir)/src/storj" fzstorj.exe $(OBJDUMP) $(CXX) $$PATH; \
	fi

installlangfiles.nsh: Makefile
	rm -f $@
	for i in @FILEZILLA_LINGUAS@; do\
	  echo !insertmacro INSTALLLANGFILE $$i >> $@; \
	done

uninstalllangfiles.nsh: Makefile
	rm -f $@
	for i in @FILEZILLA_LINGUAS@; do\
	  echo !insertmacro UNINSTALLLANGFILE $$i >> $@; \
	done

# Some versions of libtool create executables as foo.exe
# while otheres create executables as .libs/foo.exe
# NSIS needs to know the actual path.

libtoolexecutablesubdir.nsh: Makefile
	rm -f $@
	touch test_libtool_exedir.c
	$(CC) -c -o test_libtool_exedir.o test_libtool_exedir.c
	exedir=`$(LIBTOOL) -n --mode=link $(CC) -o test_libtool_exedir$(EXEEXT) test_libtool_exedir.o`; \
	if echo "$$exedir" | grep -- '-o \.libs' > /dev/null; then \
	  echo "!define LT_EXEDIR \".libs\\\"" > libtoolexecutablesubdir.nsh; \
	else \
	  touch libtoolexecutablesubdir.nsh; \
	fi
	rm -f test_libtool_exedir.o test_libtool_exedir.c

CLEANFILES = installlangfiles.nsh uninstalllangfiles.nsh libtoolexecutablesubdir.nsh dll_install.nsh dll_uninstall.nsh

endif

desktopentrydir = $(datadir)/applications
dist_desktopentry_DATA = filezilla.desktop

appdatadir = $(datadir)/appdata
dist_appdata_DATA = filezilla.appdata.xml

clean-local:
	rm -rf dlls

