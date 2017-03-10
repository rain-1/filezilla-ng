dnl Checks for thread_local support

AC_DEFUN([CHECK_THREAD_LOCAL], [

  AC_LANG_PUSH(C++)

  AC_MSG_CHECKING([for thread_local])

  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
    ]], [[
      thread_local static int foo = 0;
      (void)foo;
      return 0;
    ]])
  ], [
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Compiler support for the thread_local keyword is required.])
  ])

  AC_LANG_POP(C++)
])
