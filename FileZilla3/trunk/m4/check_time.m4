dnl We need the threadsafe variants of localtime
AC_DEFUN([CHECK_THREADSAFE_LOCALTIME],
[
  AC_CHECK_FUNCS(localtime_r, [], [
    AC_MSG_CHECKING([for localtime_s])
    dnl Checking for localtime_s is a bit more complex as it is a macro
    AC_LINK_IFELSE([
      AC_LANG_PROGRAM([[
       #include <time.h>
       ]], [[
         time_t t;
         struct tm m;
         localtime_s(&m, &t);
         return 0;
      ]])
    ], [
      AC_MSG_RESULT([yes])
      AC_DEFINE([HAVE_LOCALTIME_S], [1], [localtime_s can be used])
    ], [
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([No threadsafe variant of localtime found])
    ])
  ])
])

dnl We need the threadsafe variants of gmtime
AC_DEFUN([CHECK_THREADSAFE_GMTIME], [
  AC_CHECK_FUNCS(gmtime_r, [], [
    AC_MSG_CHECKING([for gmtime_s])
    dnl Checking for gmtime_s is a bit more complex as it is a macro
    AC_LINK_IFELSE([
      AC_LANG_PROGRAM([[
       #include <time.h>
       ]], [[
         time_t t;
         struct tm m;
         gmtime_s(&m, &t);
         return 0;
      ]])
    ], [
      AC_MSG_RESULT([yes])
      AC_DEFINE([HAVE_GMTIME_S], [1], [gmtime_s can be used])
    ], [
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([No threadsafe variant of gmtime found])
    ])
  ])
])

dnl We need an inverse for gmtime, either timegm or _mkgmtime
AC_DEFUN([CHECK_INVERSE_GMTIME], [
  # We need an inverse for gmtime, either timegm or _mkgmtime
  AC_CHECK_FUNCS(timegm, [], [
    if ! echo "${WX_CPPFLAGS}" | grep __WXMSW__ > /dev/null 2>&1; then
      AC_MSG_ERROR([No inverse function for gmtime was found])
    fi
  ])
])