dnl
dnl Author:
dnl   Leif Kornstaedt <kornstae@ps.uni-sb.de>
dnl   Marco Kuhlmann <kuhlmann@ps.uni-sb.de>
dnl
dnl Copyright:
dnl   Leif Kornstaedt, 2003
dnl   Marco Kuhlmann, 2003
dnl

AC_REVISION([$Id$])
AC_PREREQ(2.53)
AC_INIT(SEAM, 1.4, seam@ps.uni-sb.de)
AC_CONFIG_SRCDIR(Seam.hh)
AM_INIT_AUTOMAKE

seamlibdir='${libdir}/seam'
AC_SUBST(seamlibdir)

# checks for programs

: ${CXXFLAGS=""} dnl we do not want autoconf's default

AC_PROG_CXX
AC_LANG(C++)

# checks for libraries

AC_SEAM_CHECK_SOCKET_FLAVOR

AC_ARG_WITH(zlib,
   AC_HELP_STRING([--with-zlib],
     [use the zlib file compression library @<:@default=yes@:>@]))

if test "${with_zlib:-yes}" = "no"; then
   AC_DEFINE(HAVE_ZLIB, 0)
else
   if test "${with_zlib}" != "yes"; then
      CPPFLAGS="${CPPFLAGS}${CPPFLAGS:+ }-I${with_zlib}"
      CPPFLAGS="${CPPFLAGS} -I${with_zlib}/include"
      LDFLAGS="${LDFLAGS}${LDFLAGS:+ }-L${with_zlib}/lib"
   fi
   AC_CHECK_LIB(z, gzopen,
     [LIBS="${LIBS}${LIBS:+ }-lz"
      AC_DEFINE(HAVE_ZLIB, 1)],
     [LIBS="zlib.lib${LIBS:+ }${LIBS}"
      AC_LINK_IFELSE(AC_LANG_PROGRAM([[#include <zlib.h>]],
                                     [[gzFile f = gzopen("foo", "rb");]]),
        [AC_MSG_RESULT(zlib.lib)
         AC_DEFINE(HAVE_ZLIB, 1)],
        [AC_MSG_ERROR(cannot find the zlib file compression library)])])
fi

# checks for header files

AC_SEAM_ENABLE_LIGHTNING

# checks for types

#AC_CHECK_TYPE(u_int,
#  [AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_U_INT=1)
#   AC_DEFINE(HAVE_U_INT, 1)])

# checks for structures

# checks for compiler characteristics

AC_SEAM_CHECK_DECLSPEC

AC_SEAM_CHECK_CXXFLAG(-pipe)
AC_SEAM_CHECK_LDFLAG([-Wl,--export-dynamic])
AC_SEAM_CHECK_CXXFLAG(-fno-coalesce-templates)
AC_SEAM_CHECK_CXXFLAG(-fPIC)
AC_SEAM_CHECK_CXXFLAG_SEAMTOOL(-fPIC)
AC_SEAM_CHECK_CXXFLAG_SEAMTOOL([-falign-functions -falign-jumps -falign-loops -falign-labels])
AC_SEAM_CHECK_LDFLAG(-fPIC)
AC_SEAM_CHECK_LDFLAG_SEAMTOOL(-fPIC)

AC_SEAM_CHECK_CXXFLAG_SEAMTOOL(-ggdb,
   AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-ggdb),
   AC_SEAM_CHECK_CXXFLAG_SEAMTOOL(-g))

AC_SEAM_CHECK_LDFLAG([-shared],
  [SEAMTOOL_SHAREDLIBFLAG="-shared"],
  [AC_SEAM_CHECK_LDFLAG([-dynamic -dynamiclib],
  [SEAMTOOL_SHAREDLIBFLAG="-dynamic -dynamiclib"],
  [AC_MSG_ERROR([don't know how to build shared libraries])]
  )])

AC_SUBST(SEAMTOOL_SHAREDLIBFLAG)

# checks for library functions

AC_CHECK_DECL(VirtualAlloc,
   [AC_DEFINE(HAVE_VIRTUALALLOC, 1)],
   [AC_DEFINE(HAVE_VIRTUALALLOC, 0)],
   [[#include <windows.h>]])

AC_CHECK_DECL(LoadLibrary,
   [AC_DEFINE(HAVE_LOADLIBRARY, 1)
    AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_LOADLIBRARY=1)
    seam_have_loadlibrary="yes"],
   [AC_DEFINE(HAVE_LOADLIBRARY, 0)
    AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_LOADLIBRARY=0)
    seam_have_loadlibrary="no"],
   [[#include <windows.h>]])

# checks for system services

# groups of checks

AC_LINK_IFELSE(AC_LANG_PROGRAM([[#include <signal.h>]],
  [[signal(SIGALRM, 0);]]),
  [AC_DEFINE(HAVE_SIGNAL, 1)
   AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_SIGNAL=1)],
  [AC_DEFINE(HAVE_SIGNAL, 0)
   AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_SIGNAL=0)])

AC_CHECK_TYPE(sig_atomic_t,
  [AC_DEFINE(HAVE_SIG_ATOMIC_T)], , [[#include <signal.h>]])

AC_CHECK_DECL(SetConsoleCtrlHandler,
  [AC_DEFINE(HAVE_CONSOLECTRL, 1)
   AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_CONSOLECTRL=1)],
  [AC_DEFINE(HAVE_CONSOLECTRL, 0)
   AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_CONSOLECTRL=0)],
  [[#include <windows.h>]])  

# libtool

AC_LIBLTDL_CONVENIENCE
AC_SUBST(LTDLINCL)
AC_SUBST(LIBLTDL)
AC_LIBTOOL_DLOPEN
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL
if test "$seam_have_loadlibrary" = "no"; then
AC_CONFIG_SUBDIRS(libltdl)
AC_DEFINE(HAVE_LIBLTDL, 1)
AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_LIBLTDL=1)
else
AC_DEFINE(HAVE_LIBLTDL, 0)
AC_SEAM_ADD_TO_CXXFLAGS_SEAMTOOL(-DHAVE_LIBLTDL=0)
LTDLINCL=
LIBLTDL=
fi
AM_CONDITIONAL(HAVE_LIBLTDL, test "$seam_have_loadlibrary" = "no")

case "$host" in
    *-*-cygwin*)
        EXTRA_LDFLAGS="${EXTRA_LDFLAGS}${EXTRA_LDFLAGS:+ }-no-undefined"
        ;;
esac

AC_SUBST(EXTRA_LDFLAGS)

# -------------------------------------------------------------------------
# File compression
# -------------------------------------------------------------------------


# -------------------------------------------------------------------------
# GNU lightning
# -------------------------------------------------------------------------

AC_SEAM_ARG_ENABLE_CHECKED
AC_SEAM_ARG_ENABLE_DEBUGGER
AC_SEAM_ARG_ENABLE_OPTIMIZED
AC_SEAM_ARG_ENABLE_PROFILER
AC_SEAM_ARG_WITH_WARNINGS

# -------------------------------------------------------------------------
# Output files
# -------------------------------------------------------------------------

AC_SEAM_SEAMTOOL_COMMANDS

AC_CONFIG_FILES([
   Makefile
   adt/Makefile
   generic/Makefile
   store/Makefile
   lightning/Makefile
   debian/Makefile
   seamtool])
AC_OUTPUT
