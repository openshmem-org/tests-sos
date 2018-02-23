# -*- autoconf -*-
#
# Copyright 2011 Sandia Corporation. Under the terms of Contract
# DE-AC04-94AL85000 with Sandia Corporation, the U.S.  Government
# retains certain rights in this software.
#
# Copyright (c) 2017 Intel Corporation. All rights reserved.
# This software is available to you under the BSD license.
#
# This file is part of the Sandia OpenSHMEM software package. For license
# information, see the LICENSE file in the top level directory of the
# distribution.

dnl Init Autoconf/Automake/Libtool

AC_INIT([Sandia OpenSHMEM Tests], [1.4.0], [https://github.com/Sandia-OpenSHMEM/SOS])
AC_PREREQ([2.60])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])

AM_INIT_AUTOMAKE([foreign check-news color-tests parallel-tests dist-bzip2 no-define subdir-objects tar-ustar 1.11 -Wall -Werror])

m4_ifndef([AS_VAR_COPY],
    [m4_define([AS_VAR_COPY],
       [AS_LITERAL_IF([$1[]$2], [$1=$$2], [eval $1=\$$2])])])

dnl Work around change in how the rules were implemented between
dnl 1.11.2 and 1.11.3.
AM_SILENT_RULES([yes])
AS_IF([test -z "$AM_V"],
  [AM_V='$(V)'
   AM_DEFAULT_V='$(AM_DEFAULT_VERBOSITY)'
   AC_SUBST([AM_V])
   AC_SUBST([AM_DEFAULT_V])])

dnl information on the package
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_HOST

AC_ARG_ENABLE([picky],
    [AC_HELP_STRING([--enable-picky],
                    [Enable developer-level compiler pickyness when building (default: disabled)])])

AC_ARG_ENABLE([debug],
    [AC_HELP_STRING([--enable-debug],
                    [Include debugging symbols in the build (default: disabled)])])

AC_ARG_ENABLE([threads],
    [AC_HELP_STRING([--disable-threads],
                    [Disable threading support (default:enabled)])])

AS_IF([test "$enable_threads" != "no"], [
       OPAL_CONFIG_POSIX_THREADS(HAVE_POSIX_THREADS=1, HAVE_POSIX_THREADS=0)
       AC_MSG_CHECKING([for working POSIX threads package])
       if test "$HAVE_POSIX_THREADS" = "1" ; then
           AC_MSG_RESULT([yes])
           AC_SUBST(PTHREAD_CFLAGS)
           AC_SUBST(PTHREAD_LIBS)
       else
           AC_MSG_RESULT([no])
       fi
       AC_DEFINE([ENABLE_THREADS], [1], [Enable threads])
       ])
AM_CONDITIONAL([HAVE_PTHREADS], [test "$HAVE_POSIX_THREADS" = "1"])

AC_ARG_ENABLE([openmp],
    [AC_HELP_STRING([--disable-openmp],
                    [Disable OpenMP support (default: enabled)])])

AS_IF([test "$enable_threads" != "no"], [
       AS_IF([test "$enable_openmp" != "no"], [
             AC_OPENMP
             AC_SUBST(AM_OPENMP_CXXFLAGS,"$OPENMP_CXXFLAGS")
             AC_SUBST(AM_OPENMP_CFLAGS,"$OPENMP_CFLAGS")])
       ])
AM_CONDITIONAL([HAVE_OPENMP], [test "$enable_threads" != "no" -a "$enable_openmp" != "no" -a "$ac_cv_prog_c_openmp" != "unsupported"])

AC_ARG_ENABLE([lengthy-tests],
    [AC_HELP_STRING([--enable-lengthy-tests],
                    [Enable long running tests in the test suite (default: disabled)])])
AM_CONDITIONAL([ENABLE_LENGTHY_TESTS], [test "$enable_lengthy_tests" = "yes"])

AC_ARG_ENABLE([fortran],
    [AC_HELP_STRING([--disable-fortran],
                    [Disable building the Fortran bindings (default: enabled)])])

AC_ARG_ENABLE([cxx],
    [AC_HELP_STRING([--disable-cxx],
                    [Disable building the C++ bindings (default: enabled)])])

AC_ARG_ENABLE([libtool-wrapper],
  [AS_HELP_STRING([--disable-libtool-wrapper],
    [Disable use of libtool executable wrappers for tests (default: enabled)])])
AS_IF([test "$enable_libtool_wrapper" = "no"], [LIBTOOL_WRAPPER_LDFLAGS="-no-install"], [LIBTOOL_WRAPPER_LDFLAGS=])
AC_SUBST([LIBTOOL_WRAPPER_LDFLAGS])

AC_ARG_ENABLE([profiling],
    [AC_HELP_STRING([--enable-profiling],
                    [Enable profiling (pshmem) interface (default:disabled)])])
AS_IF([test "$enable_profiling" = "yes"],
      [AC_DEFINE([ENABLE_PROFILING], [1], [Enable profiling (pshmem) interface])]
      ])

AC_ARG_ENABLE([long-fortran-header],
    [AC_HELP_STRING([--enable-long-fortran-header],
                    [Enable long Fortran header, including all function declarations (default:disabled)])])
AS_IF([test "$enable_long_fortran_header" = "yes"], [FORTRAN_LONG_HEADER=" "], [FORTRAN_LONG_HEADER="!"])
AC_SUBST([FORTRAN_LONG_HEADER])
AM_CONDITIONAL([HAVE_LONG_FORTRAN_HEADER], [test "$enable_long_fortran_header" = "yes"])

AC_ARG_WITH([oshrun-launcher],
    [AC_HELP_STRING([--with-oshrun-launcher],
                    [Set launcher to be used by oshrun launcher wrapper. (default: auto)])])

dnl check for programs

AC_PROG_CC
AM_PROG_CC_C_O
AC_C_INLINE
AC_PROG_CXX dnl required even with --disable-cxx due to automake conditionals
m4_ifdef([AM_PROG_AR],[AM_PROG_AR])
AC_PATH_PROG([PERL],[perl])

if test "$enable_fortran" != "no" ; then
  AC_PROG_FC
else
  FC=
fi
if test "$FC" != "" ; then
  AC_FC_WRAPPERS
fi

if test "$enable_picky" = "yes" -a "$GCC" = "yes" ; then
  CFLAGS="$CFLAGS -Wall -Wno-long-long -Wmissing-prototypes -Wstrict-prototypes -Wcomment -pedantic"
else
  CFLAGS="$CFLAGS -Wall"
fi

if test "$enable_debug" = "yes" ; then
  CFLAGS="$CFLAGS -g"
fi

AC_CACHE_SAVE

AC_SEARCH_LIBS([clock_gettime], [rt],
               [AC_DEFINE([HAVE_CLOCK_GETTIME], [1], [define if clock_gettime is available]) ])

dnl check for libraries

AM_CONDITIONAL([HAVE_FORTRAN], [test "$FC" != ""])
AM_CONDITIONAL([HAVE_CXX], [test "$enable_cxx" != "no" ])
AM_CONDITIONAL([ENABLE_PROFILING], [test "$enable_profiling" = "yes" ])

dnl check for compiler characteristics

# disable aslr, if possible at link time
aslr_LDFLAGS=
case $host_os in
darwin*)
    aslr_LDFLAGS="-Wl,-no_pie"
    ;;
esac
AC_SUBST(aslr_LDFLAGS)

dnl make tests work in standalone mode
AM_CONDITIONAL([USE_PMI_SIMPLE], [false])
AM_CONDITIONAL([USE_PORTALS4], [false])
AM_CONDITIONAL([EXTERNAL_TESTS], [true])

dnl final output

LT_INIT

# Need the libtool binary before setting up rpath

LT_OUTPUT

AC_CONFIG_FILES([Makefile
  test/Makefile
  test/unit/Makefile
  test/performance/Makefile
  test/performance/shmem_perf_suite/Makefile
  test/performance/tests/Makefile
  test/apps/Makefile])

AC_OUTPUT

FORT="$FC"
if test "$FORT" = "" ; then
  FORT="none"
fi

if test "$enable_cxx" == "no"; then
  CXX="none"
fi

echo ""
echo "Compilers:"
echo "  C:              $CC"
echo "  C++:            $CXX"
echo "  Fortran:        $FORT"
echo ""
