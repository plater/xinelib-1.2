dnl Configure paths for XINE
dnl
dnl Copyright (C) 2001 Daniel Caujolle-Bert <segfault@club-internet.fr>
dnl  
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2 of the License, or
dnl (at your option) any later version.
dnl  
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl  
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
dnl  
dnl  
dnl As a special exception to the GNU General Public License, if you
dnl distribute this file as part of a program that contains a configuration
dnl script generated by Autoconf, you may include it under the same
dnl distribution terms that you use for the rest of that program.
dnl  

dnl _XINE_VERSION_PARSE(version)
AC_DEFUN([_XINE_VERSION_PARSE], [`echo $1 | perl -e 'my $v = <>; chomp $v;
my @v = split(" ", $v); $v = $v[[@S|@#v]]; $v =~ s/[[^0-9.]].*$//; @v = split (/\./, $v);
push @v, 0 while $[#v] < 2; print $v[[0]] * 10000 + $v[[1]] * 100 + $v[[2]], "\n"'`])


dnl _XINE_VERSION_CHECK(required, actual)
AC_DEFUN([_XINE_VERSION_CHECK], [
    required_version=ifelse([$1], , [0.0.0], [$1])
    required_version_parsed=_XINE_VERSION_PARSE([$required_version])
    actual_version=ifelse([$2], , [0.0.0], [$2])
    actual_version_parsed=_XINE_VERSION_PARSE([$actual_version])
    if test $required_version_parsed -le $actual_version_parsed; then
        ifelse([$3], , [:], [$3])
    else
        ifelse([$4], , [:], [$4])
    fi
])


dnl AM_PATH_XINE([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND ]]])
dnl Test for XINE, and define XINE_CFLAGS and XINE_LIBS
dnl
AC_DEFUN([AM_PATH_XINE], [
    AC_ARG_VAR([XINE_CONFIG], [Full path to xine-config])
    AC_ARG_WITH([xine-prefix],
                [AS_HELP_STRING([--with-xine-prefix], [prefix where xine-lib is installed (optional)])])
    AC_ARG_WITH([xine-exec-prefix],
                [AS_HELP_STRING([--with-xine-exec-prefix], [exec prefix where xine-lib is installed (optional)])])

    xine_config_args=""
    if test x"$with_xine_exec_prefix" != x""; then
        xine_config_args="$xine_config_args --exec-prefix=$with_xine_exec_prefix"
        test x"$XINE_CONFIG" != x"" && XINE_CONFIG="$with_xine_exec_prefix/bin/xine-config"
    fi
    if test x"$with_xine_prefix" != x""; then
        xine_config_args="$xine_config_args --prefix=$with_xine_prefix"
        test x"$XINE_CONFIG" = x"" && XINE_CONFIG="$with_xine_prefix/bin/xine-config"
    fi

    min_xine_version=ifelse([$1], , [0.5.0], [$1])
    AC_PATH_TOOL([XINE_CONFIG], [xine-config], [no])
    AC_MSG_CHECKING([for XINE-LIB version >= $min_xine_version])
    if test x"$XINE_CONFIG" = x"no"; then
        AC_MSG_RESULT([unknown])
        AC_MSG_NOTICE([
*** If xine-lib was installed in PREFIX, make sure PREFIX/bin is in your path,
*** or set the XINE_CONFIG environment variable to the full path to the
*** xine-config shell script.
        ])
    else
        XINE_CFLAGS="`$XINE_CONFIG $xine_config_args --cflags`"
        XINE_LIBS="`$XINE_CONFIG $xine_config_args --libs`"
        XINE_VERSION="`$XINE_CONFIG $xine_config_args --version`"
        XINE_ACFLAGS="`$XINE_CONFIG $xine_config_args --acflags`"
        xine_data_dir="`$XINE_CONFIG $xine_config_args --datadir`"
        xine_script_dir="`$XINE_CONFIG $xine_config_args --scriptdir`"
        xine_plugin_dir="`$XINE_CONFIG $xine_config_args --plugindir`"
        xine_locale_dir="`$XINE_CONFIG $xine_config_args --localedir`"
        _XINE_VERSION_CHECK([$min_xine_version], [$XINE_VERSION],
                            [xine_version_ok=yes; AC_MSG_RESULT([yes, $XINE_VERSION])],
                            [xine_version_ok=no;  AC_MSG_RESULT([no, $XINE_VERSION])])
        if test x"$xine_version_ok" != x"yes"; then
            AC_MSG_NOTICE([
*** You need a version of xine-lib newer than $XINE_VERSION.
*** The latest version of xine-lib is always available from:
***        http://www.xinehq.de
***
*** If you have already installed a sufficiently new version, this error
*** probably means that the wrong copy of the xine-config shell script is
*** being found. The easiest way to fix this is to remove the old version
*** of xine-lib, but you can also set the XINE_CONFIG environment variable
*** to point to the correct copy of xine-config. In this case, you will have
*** to modify your LD_LIBRARY_PATH enviroment variable, or edit
*** /etc/ld.so.conf so that the correct libraries are found at run-time.
            ])
        fi
    fi
    AC_SUBST(XINE_CFLAGS)
    AC_SUBST(XINE_LIBS)
    AC_SUBST(XINE_ACFLAGS)

    if test x"$xine_version_ok" = x"yes"; then
        ifelse([$2], , [:], [$2])
    else
        ifelse([$3], , [:], [$3])
    fi
])
