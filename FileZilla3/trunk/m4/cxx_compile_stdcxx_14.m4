# This is based on AX_CXX_COMPILE_STDCXX_11 from the Autoconf archive
#
# SYNOPSIS
#
#   CXX_COMPILE_STDCXX_14([ext|noext],[mandatory|optional])
#
# DESCRIPTION
#
#   Check for baseline language coverage in the compiler for the C++14
#   standard; if necessary, add switches to CXXFLAGS to enable support.
#
#   The first argument, if specified, indicates whether you insist on an
#   extended mode (e.g. -std=gnu++14) or a strict conformance mode (e.g.
#   -std=c++14).  If neither is specified, you get whatever works, with
#   preference for an extended mode.
#
#   The second argument, if specified 'mandatory' or if left unspecified,
#   indicates that baseline C++14 support is required and that the macro
#   should error out if no mode with that support is found.  If specified
#   'optional', then configuration proceeds regardless, after defining
#   HAVE_CXX14 if and only if a supporting mode is found.
#
# LICENSE
#
#   Copyright (c) 2008 Benjamin Kosnik <bkoz@redhat.com>
#   Copyright (c) 2012 Zack Weinberg <zackw@panix.com>
#   Copyright (c) 2013 Roy Stogner <roystgnr@ices.utexas.edu>
#   Copyright (c) 2014, 2015 Google Inc.; contributed by Alexey Sokolov <sokolov@google.com>
#   Copyright (c) 2015 Tim Kosse <tim.kosse@filezilla-project.org>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 10

m4_define([_CXX_COMPILE_STDCXX_14_testbody], [[
  #include <memory>

  bool make_unique_test() {
    auto p = std::make_unique<int>(14);
    return static_cast<bool>(p);
  }

  // Also test some C++11 stuff just to be sure
  
  #include <unordered_map>
  template <typename T>
  struct umt {
    std::unordered_map<int, T> map;
  };    

  template <typename T>
    struct check
    {
      static_assert(sizeof(int) <= sizeof(T), "not big enough");
    };

    struct Base {
    virtual void f() {}
    };
    struct Child : public Base {
    virtual void f() override {}
    };

    typedef check<check<bool>> right_angle_brackets;

    int a;
    decltype(a) b;

    typedef check<int> check_type;
    check_type c;
    check_type&& cr = static_cast<check_type&&>(c);

    auto d = a;
    auto l = [](){};
    // Prevent Clang error: unused variable 'l' [-Werror,-Wunused-variable]
    struct use_l { use_l() { l(); } };

    // http://stackoverflow.com/questions/13728184/template-aliases-and-sfinae
    // Clang 3.1 fails with headers of libstd++ 4.8.3 when using std::function because of this
    namespace test_template_alias_sfinae {
        struct foo {};

        template<typename T>
        using member = typename T::member_type;

        template<typename T>
        void func(...) {}

        template<typename T>
        void func(member<T>*) {}

        void test();

        void test() {
            func<foo>(0);
        }
    }
]])

AC_DEFUN([CXX_COMPILE_STDCXX_14], [dnl
  m4_if([$1], [], [],
        [$1], [ext], [],
        [$1], [noext], [],
        [m4_fatal([invalid argument `$1' to CXX_COMPILE_STDCXX_14])])dnl
  m4_if([$2], [], [cxx_compile_cxx14_required=true],
        [$2], [mandatory], [cxx_compile_cxx14_required=true],
        [$2], [optional], [cxx_compile_cxx14_required=false],
        [m4_fatal([invalid second argument `$2' to CXX_COMPILE_STDCXX_14])])
  AC_LANG_PUSH([C++])dnl
  ac_success=no
  AC_CACHE_CHECK(whether $CXX supports C++14 features by default,
  _cv_cxx_compile_cxx14,
  [AC_COMPILE_IFELSE([AC_LANG_SOURCE([_CXX_COMPILE_STDCXX_14_testbody])],
    [_cv_cxx_compile_cxx14=yes],
    [_cv_cxx_compile_cxx14=no])])
  if test x$_cv_cxx_compile_cxx14 = xyes; then
    ac_success=yes
  fi

  m4_if([$1], [noext], [], [dnl
  if test x$ac_success = xno; then
    for switch in -std=gnu++14 -std=gnu++1y; do
      cachevar=AS_TR_SH([_cv_cxx_compile_cxx14_$switch])
      AC_CACHE_CHECK(whether $CXX supports C++14 features with $switch,
                     $cachevar,
        [ac_save_CXXFLAGS="$CXXFLAGS"
         CXXFLAGS="$CXXFLAGS $switch"
         AC_COMPILE_IFELSE([AC_LANG_SOURCE([_CXX_COMPILE_STDCXX_14_testbody])],
          [eval $cachevar=yes],
          [eval $cachevar=no])
         CXXFLAGS="$ac_save_CXXFLAGS"])
      if eval test x\$$cachevar = xyes; then
        CXXFLAGS="$CXXFLAGS $switch"
        ac_success=yes
        break
      fi
    done
  fi])

  m4_if([$1], [ext], [], [dnl
  if test x$ac_success = xno; then
    for switch in -std=c++14 -std=c++1y; do
      cachevar=AS_TR_SH([_cv_cxx_compile_cxx14_$switch])
      AC_CACHE_CHECK(whether $CXX supports C++14 features with $switch,
                     $cachevar,
        [ac_save_CXXFLAGS="$CXXFLAGS"
         CXXFLAGS="$CXXFLAGS $switch"
         AC_COMPILE_IFELSE([AC_LANG_SOURCE([_CXX_COMPILE_STDCXX_14_testbody])],
          [eval $cachevar=yes],
          [eval $cachevar=no])
         CXXFLAGS="$ac_save_CXXFLAGS"])
      if eval test x\$$cachevar = xyes; then
        CXXFLAGS="$CXXFLAGS $switch"
        ac_success=yes
        break
      fi
    done
  fi])
  AC_LANG_POP([C++])
  if test x$cxx_compile_cxx14_required = xtrue; then
    if test x$ac_success = xno; then
      AC_MSG_ERROR([*** A compiler with support for C++14 language features is required.])
    fi
  else
    if test x$ac_success = xno; then
      HAVE_CXX14=0
      AC_MSG_NOTICE([No compiler with C++14 support was found])
    else
      HAVE_CXX14=1
      AC_DEFINE(HAVE_CXX14,1,
                [define if the compiler supports basic C++14 syntax])
    fi

    AC_SUBST(HAVE_CXX14)
  fi
])
