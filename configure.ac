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

AC_INIT([Sandia OpenSHMEM Test Suite], [1.4.3], [https://github.com/Sandia-OpenSHMEM/SOS])
AC_PREREQ([2.60])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])

AM_INIT_AUTOMAKE([foreign check-news color-tests parallel-tests dist-bzip2 no-define subdir-objects tar-ustar 1.11 -Wall -Werror])

dnl Work around change in how the rules were implemented between
dnl 1.11.2 and 1.11.3.
AM_SILENT_RULES([yes])
AS_IF([test -z "$AM_V"],
  [AM_V='$(V)'
   AM_DEFAULT_V='$(AM_DEFAULT_VERBOSITY)'
   AC_SUBST([AM_V])
   AC_SUBST([AM_DEFAULT_V])])

dnl check for programs
AC_PROG_CC([oshcc])
AM_PROG_CC_C_O
AC_C_INLINE
AC_PROG_CXX([oshc++]) dnl required even with --disable-cxx due to automake conditionals

dnl information on the package
AC_ARG_ENABLE([picky],
    [AC_HELP_STRING([--enable-picky],
                    [Enable developer-level compiler pickyness when building (default: disabled)])])

AC_ARG_ENABLE([debug],
    [AC_HELP_STRING([--enable-debug],
                    [Include debugging symbols (default: disabled)])])

AC_ARG_ENABLE([deprecated-tests],
    [AC_HELP_STRING([--enable-deprecated-tests],
                    [Enable deprecated SHMEM API calls in tests (default:disabled)])])
AM_CONDITIONAL([ENABLE_DEPRECATED_TESTS], [test "$enable_deprecated_tests" = "yes"])

AC_ARG_WITH([test-runner],
    [AC_HELP_STRING([--with-test-runner],
                    [Command used to run tests (default: 'oshrun -np $(NPROCS)')])])

if test -n "$with_test_runner" ; then
    TEST_RUNNER="$with_test_runner"
else
    TEST_RUNNER='oshrun -np $(NPROCS)'
fi
AC_SUBST(TEST_RUNNER)

AC_ARG_ENABLE([threads],
    [AC_HELP_STRING([--disable-threads],
                    [Disable multithreaded tests (default:enabled)])])

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
       ])
AM_CONDITIONAL([HAVE_PTHREADS], [test "$HAVE_POSIX_THREADS" = "1"])

AS_IF([test "$enable_threads" != "no"], [
       AS_IF([test "$enable_openmp" != "no"], [
             AC_OPENMP
             AC_SUBST(AM_OPENMP_CXXFLAGS,"$OPENMP_CXXFLAGS")
             AC_SUBST(AM_OPENMP_CFLAGS,"$OPENMP_CFLAGS")])
       ])
AM_CONDITIONAL([HAVE_OPENMP], [test "$enable_threads" != "no" -a "$enable_openmp" != "no" -a "$ac_cv_prog_c_openmp" != "unsupported"])

AC_ARG_ENABLE([lengthy-tests],
    [AC_HELP_STRING([--enable-lengthy-tests],
                    [Execute long running tests as part of "make check" (default: disabled)])])
AM_CONDITIONAL([ENABLE_LENGTHY_TESTS], [test "$enable_lengthy_tests" = "yes"])

AC_ARG_ENABLE([fortran],
    [AC_HELP_STRING([--disable-fortran],
                    [Disable Fortran tests (default: enabled)])])

AC_ARG_ENABLE([cxx],
    [AC_HELP_STRING([--disable-cxx],
                    [Disable C++ tests (default: enabled)])])

AC_ARG_ENABLE([c11-checks],
    [AC_HELP_STRING([--disable-c11-checks],
                    [Disable build-time type checks for C11 bindings (default: enabled)])])

AC_ARG_ENABLE([profiling],
    [AC_HELP_STRING([--enable-profiling],
                    [Enable profiling (pshmem) interface tests (default:disabled)])])

AC_ARG_ENABLE([shmemx],
    [AC_HELP_STRING([--enable-shmemx],
                    [Enable SHMEM extensions tests (default:disabled)])])
AM_CONDITIONAL([SHMEMX_TESTS], [test "$enable_shmemx" = "yes"])

if test "$enable_fortran" != "no" ; then
  AC_PROG_FC([oshfort])
else
  FC=
fi
if test "$FC" != "" ; then
  AC_FC_WRAPPERS
fi

dnl check that c/c++/fortran compilers can compile SHMEM code
AC_MSG_CHECKING([C compiler can compile SHMEM code])
AC_LANG_PUSH([C])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
                   #include <shmem.h>
                   long data;
                   ]], [[
                        shmem_init();
                        shmem_long_p(&data, 0, 0);
                        shmem_finalize();
                        ]]
                   )],
                   [AC_MSG_RESULT([yes])],
                   [AC_MSG_RESULT([no])
                    AC_MSG_ERROR([C compiler cannot compile SHMEM code])])
AC_LANG_POP([C])

if test "$enable_c11_checks" != "no" ; then
    ORIG_CFLAGS=$CFLAGS
    AC_MSG_CHECKING([if -std=gnu11 works])

    AC_LANG_PUSH([C])
    CFLAGS="-pedantic-errors -std=gnu11"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
                       void g(int i){}
                       #define f(X) _Generic((X), default: g)(X)
                       ]], [[
                            f(1);
                            ]]
                       )],
                       [shmem_cv_c11_works="yes"], [shmem_cv_c11_works="no"])
    AS_IF([test "$shmem_cv_c11_works" = "no"],
          [AC_MSG_RESULT([no])
          CFLAGS="$ORIG_CFLAGS"
          AC_PROG_CC_C99
          ],
          [AC_MSG_RESULT([yes])
          CFLAGS="-std=gnu11 $ORIG_CFLAGS"]
          )
    AC_LANG_POP([C])
fi
AM_CONDITIONAL([HAVE_C11], [test "$shmem_cv_c11_works" = "yes" -a "$enable_c11_checks" != "no"])


if test "$enable_cxx" != "no" ; then
    AC_MSG_CHECKING([C++ compiler can compile SHMEM code])
    AC_LANG_PUSH([C++])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
                       #include <shmem.h>
                       long data;
                       ]], [[
                            shmem_init();
                            shmem_long_p(&data, 0, 0);
                            shmem_finalize();
                            ]]
                       )],
                       [AC_MSG_RESULT([yes])],
                       [AC_MSG_RESULT([no])
                       AC_MSG_ERROR([C++ compiler cannot compile SHMEM code])])
    AC_LANG_POP([C++])
fi

if test "$FC" != "" ; then
    AC_MSG_CHECKING([Fortran compiler can compile SHMEM code])
    AC_LANG_PUSH([Fortran])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [[
                            include 'shmem.fh'

                            integer npes, me

                            call shmem_init()
                            npes = num_pes()
                            me = my_pe()

                            print *,'I am ', me, ' of ', npes
                            call shmem_finalize()
                            ]]
                       )],
                       [AC_MSG_RESULT([yes])],
                       [AC_MSG_RESULT([no])
                       AC_MSG_ERROR([Fortran compiler cannot compile SHMEM code])])
    AC_LANG_POP([Fortran])
fi

if test "$enable_picky" = "yes" ; then
  if test "$GCC" = "yes" ; then
    CFLAGS="$CFLAGS -Wall -Wno-long-long -Wmissing-prototypes -Wstrict-prototypes -Wcomment -pedantic"
  else
    CFLAGS="$CFLAGS -Wall"
  fi
fi

if test "$enable_debug" = "yes" ; then
  CFLAGS="$CFLAGS -g"
fi

AC_SEARCH_LIBS([ceil], [m], [], AC_MSG_ERROR([unable to find ceil() function]))

AM_CONDITIONAL([HAVE_FORTRAN], [test "$FC" != ""])
AM_CONDITIONAL([HAVE_CXX], [test "$enable_cxx" != "no" ])
AM_CONDITIONAL([ENABLE_PROFILING], [test "$enable_profiling" = "yes" ])

dnl make tests work in standalone mode
AM_CONDITIONAL([USE_PMI_MPI], [false])
AM_CONDITIONAL([USE_PMI_SIMPLE], [false])
AM_CONDITIONAL([USE_PORTALS4], [false])
AM_CONDITIONAL([HAVE_LONG_FORTRAN_HEADER], [false])
AM_CONDITIONAL([EXTERNAL_TESTS], [true])

dnl final output
LT_INIT
LT_OUTPUT

AC_CONFIG_FILES([Makefile
  test/Makefile
  test/unit/Makefile
  test/shmemx/Makefile
  test/include/Makefile
  test/performance/Makefile
  test/performance/shmem_perf_suite/Makefile
  test/performance/tests/Makefile
  test/apps/Makefile
  test/spec-example/Makefile])

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
