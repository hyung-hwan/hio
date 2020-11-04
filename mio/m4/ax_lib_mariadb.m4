##### BASED ON http://autoconf-archive.cryp.to/ax_lib_mariadb.html
#
# SYNOPSIS
#
#   AX_LIB_MARIADB([MINIMUM-VERSION])
#
# DESCRIPTION
#
#   This macro provides tests of availability of MariaDB client library
#   of particular version or newer.
#
#   AX_LIB_MARIADB macro takes only one argument which is optional. If
#   there is no required version passed, then macro does not run
#   version test.
#
#   The --with-mariadb-config option takes one of three possible values:
#
#   path - complete path to mariadb_config utility, use this option if
#   mariadb_config can't be found in the PATH
#
#   This macro calls:
#
#     AC_SUBST(MARIADB_CFLAGS)
#     AC_SUBST(MARIADB_LDFLAGS)
#     AC_SUBST(MARIADB_LIBS)
#     AC_SUBST(MARIADB_VERSION)
#
#   And sets:
#
#     HAVE_MARIADB
#
# LAST MODIFICATION
#
#   2020-11-04 Chung, Hyung-Hwan
#   2006-07-16 Mateusz Loskot
#
# COPYLEFT
#
#   Copyright (c) 2006 Mateusz Loskot <mateusz@loskot.net>
#
#   Copying and distribution of this file, with or without
#   modification, are permitted in any medium without royalty provided
#   the copyright notice and this notice are preserved.

AC_DEFUN([AX_LIB_MARIADB],
[
    MARIADB_CONFIG=""

    AC_ARG_WITH([mariadb],
        AC_HELP_STRING([--with-mariadb-config@<:@=PATH@:>@], [specify path to the mariadb_config utility]),
        [ MARIADB_CONFIG="$withval" ],
        [MARIADB_CONFIG=""]
    )

    MARIADB_CFLAGS=""
    MARIADB_LDFLAGS=""
    MARIADB_LIBS=""
    MARIADB_VERSION=""

    dnl
    dnl Check MariaDB libraries
    dnl

    AC_PATH_PROGS(MARIADB_CONFIG, mariadb_config)

    if test -x "$MARIADB_CONFIG"; then
        MARIADB_CFLAGS="`$MARIADB_CONFIG --cflags`"
        _full_libmariadb_libs="`$MARIADB_CONFIG --libs`"

         _save_mariadb_ldflags="${LDFLAGS}"
        _save_mariadb_cflags="${CFLAGS}"
        LDFLAGS="${LDFLAGS} ${_full_libmariadb_libs}"
        CFLAGS="${CFLAGS} ${MARIADB_CFLAGS}"

        for i in $_full_libmariadb_libs; do
            case $i in
                -lmariadb|-lperconaserverclient)

                    _lib_name="`echo "$i" | cut -b3-`"
                    AC_CHECK_LIB($_lib_name, main, [
                    	MARIADB_LIBS="-l${_lib_name} ${MARIADB_LIBS}"
                    	],[
                    	AC_MSG_ERROR([Not found $_lib_name library])
                    	])
            ;;
                -L*)

                    MARIADB_LDFLAGS="${MARIADB_LDFLAGS} $i"
            ;;
                -R*)

                    MARIADB_LDFLAGS="${MARIADB_LDFLAGS} -Wl,$i"
            ;;
                -l*)

                    _lib_name="`echo "$i" | cut -b3-`"
                    AC_CHECK_LIB($_lib_name, main, [
                    	MARIADB_LIBS="${MARIADB_LIBS} ${i}"
                    	],[
                    	AC_MSG_ERROR([Not found $i library])
                    	])
            ;;
            esac
        done

        LDFLAGS="${_save_mariadb_ldflags}"
        CFLAGS="${_save_mariadb_cflags}"
        unset _save_mariadb_ldflags
        unset _save_mariadb_cflags

        MARIADB_VERSION=`$MARIADB_CONFIG --version`

        AC_DEFINE([HAVE_MARIADB], [1],
            [Define to 1 if MariaDB libraries are available])

        found_mariadb="yes"
    else
        found_mariadb="no"
    fi

    dnl
    dnl Check if required version of MariaDB is available
    dnl

    mariadb_version_req=ifelse([$1], [], [], [$1])

    if test "$found_mariadb" = "yes" -a -n "$mariadb_version_req"; then

        AC_MSG_CHECKING([if MariaDB version is >= $mariadb_version_req])

        dnl Decompose required version string of MariaDB
        dnl and calculate its number representation
        mariadb_version_req_major=`expr $mariadb_version_req : '\([[0-9]]*\)'`
        mariadb_version_req_minor=`expr $mariadb_version_req : '[[0-9]]*\.\([[0-9]]*\)'`
        mariadb_version_req_micro=`expr $mariadb_version_req : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
        if test "x$mariadb_version_req_micro" = "x"; then
            mariadb_version_req_micro="0"
        fi

        mariadb_version_req_number=`expr $mariadb_version_req_major \* 1000000 \
                                   \+ $mariadb_version_req_minor \* 1000 \
                                   \+ $mariadb_version_req_micro`

        dnl Decompose version string of installed MariaDB
        dnl and calculate its number representation
        mariadb_version_major=`expr $MARIADB_VERSION : '\([[0-9]]*\)'`
        mariadb_version_minor=`expr $MARIADB_VERSION : '[[0-9]]*\.\([[0-9]]*\)'`
        mariadb_version_micro=`expr $MARIADB_VERSION : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
        if test "x$mariadb_version_micro" = "x"; then
            mariadb_version_micro="0"
        fi

        mariadb_version_number=`expr $mariadb_version_major \* 1000000 \
                                   \+ $mariadb_version_minor \* 1000 \
                                   \+ $mariadb_version_micro`

        mariadb_version_check=`expr $mariadb_version_number \>\= $mariadb_version_req_number`
        if test "$mariadb_version_check" = "1"; then
            AC_MSG_RESULT([yes])
        else
            AC_MSG_RESULT([no])
        fi
    fi

    AC_SUBST([MARIADB_VERSION])
    AC_SUBST([MARIADB_CFLAGS])
    AC_SUBST([MARIADB_LDFLAGS])
    AC_SUBST([MARIADB_LIBS])
])
