# Checks whether clock_gettime is available.
# Defines HAVE_CLOCK_GETTIME if it is.	
#
# CHECK_CLOCK_GETTIME([ACTION-SUCCESS], [ACTION-FAILURE])

AC_DEFUN([CHECK_CLOCK_GETTIME],
[
    AC_MSG_CHECKING([for clock_gettime])
    AC_LINK_IFELSE([
      AC_LANG_PROGRAM([[
         #include <time.h>
       ]], [[
	 (void)clock_gettime;
         return 0;
      ]])
    ], [
      AC_MSG_RESULT([yes])
      AC_DEFINE([HAVE_CLOCK_GETTIME], [1], [clock_gettime can be used])
      m4_default([$1], :)
    ], [
      AC_MSG_RESULT([no])
      m4_default([$2], :)
    ])
  ])
])
