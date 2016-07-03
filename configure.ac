AC_INIT([philix], [0.2], [kh@u.northwestern.edu])
AM_INIT_AUTOMAKE
# need a C compiler
AC_PROG_CC

# check for MPSS
AC_PATH_PROG(hasGcc, k1om-mpss-linux-gcc, no)
AC_PATH_PROG(hasMicctrl, micctrl, no)

# check for curses
AC_CHECK_LIB(tinfo, setupterm, [], [AC_MSG_ERROR("Could not find libtinfo")] )
AC_CHECK_LIB(ncurses, endwin, [], [AC_MSG_ERROR("Could not find libcurses")] )

##########################################################################
# debug compilation support
##########################################################################

AC_MSG_CHECKING([whether to build with debug information])
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [enable debug data generation (def=no)])],
    [debugit="$enableval"],
    [debugit=no])
AC_MSG_RESULT([$debugit])

if test x"$debugit" = x"yes"; then
    AC_DEFINE([DEBUG],1)
    AM_CFLAGS="$AM_CFLAGS -g -Wall -Werror -Wno-uninitialized -O0"
else
	AC_DEFINE([DEBUG],0)
	AM_CFLAGS="$AM_CFLAGS"
fi

if test x"$hasGcc" = x"no"; then
	AC_MSG_ERROR("Could not find k1om-mpss-linux-gcc. Is it on your path?")
fi

if test x"$hasMicctrl" = x"no"; then
	AC_MSG_ERROR("Could not find micctrl. Is it on your path?")
fi

AC_CONFIG_FILES([Makefile src/Makefile])


AC_OUTPUT
