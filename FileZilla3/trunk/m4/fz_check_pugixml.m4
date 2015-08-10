dnl Checks whether system's pugixml library exists

AC_DEFUN([FZ_CHECK_PUGIXML], [
  AC_ARG_WITH(pugixml, AC_HELP_STRING([--with-pugixml=type], [Selects which version of pugixml to use. Type has to be either system or builtin]),
    [
      if test "x$with_pugixml" != "xbuiltin"; then
        if test "x$with_pugixml" != "xsystem"; then
          if test "x$with_pugixml" != "xauto"; then
            AC_MSG_ERROR([--with-pugixml has to be set to either system (the default), builtin or auto])
          fi
        fi
      fi
    ],
    [
      if echo $host_os | grep -i "cygwin\|mingw\|mac\|apple" > /dev/null 2>&1 ; then
        with_pugixml=auto
      else
        with_pugixml=system
      fi
    ])

  AC_LANG_PUSH(C++)

  dnl Check pugixml.hpp header
  if test "x$with_pugixml" != "xbuiltin"; then
    AC_CHECK_HEADER(
      [pugixml.hpp],
      [],
      [
        if test "x$with_pugixml" = "xsystem"; then
          AC_MSG_ERROR([pugixml.hpp not found. If you do not have pugixml installed as system library, you can use the copy of pugixml distributed with FileZilla by passing --with-pugixml=builtin as argument to configure.])
        else
          with_pugixml=builtin
        fi
      ])
  fi

  dnl Check for shared library

  if test "x$with_pugixml" != "xbuiltin"; then
    dnl Oddity: in AC_CHECK_HEADER I can leave the true case empty, but not in AC_HAVE_LIBRARY
    AC_HAVE_LIBRARY(pugixml,
      [true],
      [
        if test "x$with_pugixml" = "xsystem"; then
          AC_MSG_ERROR([pugixml sytem library not found but requested. If you do not have pugixml installed as system library, you can use the copy of pugixml distributed with FileZilla by passing --with-pugixml=builtin as argument to configure.])
        else
          with_pugixml=builtin
        fi
      ])
  fi

  AC_LANG_POP

  if test "x$with_pugixml" = "xsystem"; then
    AC_MSG_NOTICE([Using system pugixml])
    AC_DEFINE(HAVE_LIBPUGIXML, 1, [Define to 1 if your system has the `pugixml' library (-lpugixml).])
    PUGIXML_LIBS="-lpugixml"
  else
    AC_MSG_NOTICE([Using builtin pugixml])
    PUGIXML_LIBS="../pugixml/libpugixml.a"
  fi

  AC_SUBST(PUGIXML_LIBS)
])
