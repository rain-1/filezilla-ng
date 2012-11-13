dnl Checks C++0x support, in particular we look for unordered_map

dnl Internal function, don't call CHECK_CXX_VER_SPECIFIC directly
AC_DEFUN([CHECK_CXX_VER_SPECIFIC], [

  AC_MSG_CHECKING([whether compiler supports -std=$1])
  if test "X${cxx_has_cxx0x}" = "X"; then

    old_cxxflags="$CXXFLAGS"
    CXXFLAGS="$CXXFLAGS -std=$1"

    AC_COMPILE_IFELSE([
      AC_LANG_PROGRAM([[
      ]], [[
        return 0;
      ]])
    ], [
      AC_MSG_RESULT([yes])
      cxx_has_cxx0x="-std=$1"
    ], [
      AC_MSG_RESULT([no])
      CXXFLAGS="$old_cxxflags"
    ])
  else
    AC_MSG_RESULT([skipped])
  fi
])


AC_DEFUN([CHECK_CXX0X], [

  AC_LANG_PUSH(C++)

  cxx_has_cxx0x=""
  if test "X$GCC" = "Xyes"; then

    CHECK_CXX_VER_SPECIFIC([gnu++11])
    CHECK_CXX_VER_SPECIFIC([c++11])
    CHECK_CXX_VER_SPECIFIC([gnu++0x])
    CHECK_CXX_VER_SPECIFIC([c++11])
  fi

  AC_MSG_CHECKING([for whether we can include <unordered_map>])

  # This sucks: -std=C++0x is only for C++, yet it is needed by the preprocessor already.
  # Unfortunately cannot globably add it to CPPFLAGS due to the C compiler not liking it.
  old_cppflags="$CPPFLAGS"
  if test "$cxx_has_cxx0x" != ""; then
    CPPFLAGS="$CPPFLAGS $cxx_has_cxx0x"
  fi

  has_unordered_map=""
  AC_PREPROC_IFELSE([
      AC_LANG_PROGRAM([[
        #include <unordered_map>
      ]], [[
        return 0;
      ]])
    ], [
      has_unordered_map=std
      AC_MSG_RESULT([yes])
    ], [
      AC_MSG_RESULT([no])

      AC_MSG_CHECKING([for whether we can include <tr1/unordered_map>])

      AC_PREPROC_IFELSE([
          AC_LANG_PROGRAM([[
           #include <tr1/unordered_map>
        ]], [[
          return 0;
        ]])
      ], [
        has_unordered_map=tr1
        AC_MSG_RESULT([yes])
      ], [
        AC_MSG_FAILURE([No unordered_map implementation found.])
      ])
    ])

  AH_TEMPLATE(HAVE_TR1_UNORDERED_MAP, [Define HAVE_TR1_UNORDERED_MAP on systems where unordered_map is in tr1/ subdirectory.])
  if test "$has_unordered_map" = "tr1"; then
    AC_DEFINE(HAVE_TR1_UNORDERED_MAP)
  fi

  CPPFLAGS="$old_cppflags"

  AC_LANG_POP(C++)

])
