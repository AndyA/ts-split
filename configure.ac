AC_PREREQ([2.67])
AC_INIT([tssplit], [1.0.0], [andy@hexten.net])
LT_INIT([disable-static])
AM_INIT_AUTOMAKE
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile tssplit.pc])
AC_CONFIG_SRCDIR([src/tssplit.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_ARG_VAR([PERL], [PROVE], [Test::Harness runner])

AM_CFLAGS="-Wall -Werror -Wno-unused-function"
AC_SUBST(AM_CFLAGS)

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_SED
AC_PATH_PROG([PERL], [perl])
AC_PATH_PROG([PROVE], [prove])

# Checks for libraries.
PKG_CHECK_MODULES([DEPS], [libavcodec >= 52.112.1 \
                           libavformat >= 52.99.0 \
                           libavutil >= 50.38.0 \
                           x264 >= 0.116.1913])
#mp3lame
#vpx
#faac
#swscale


# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.

AC_OUTPUT
