# Configure paths for libspeex
# Jack Moffitt <jack@icecast.org> 10-21-2000
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl AM_PATH_SPEEX([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libspeex, and define SPEEX_CFLAGS and SPEEX_LIBS
dnl
AC_DEFUN([AM_PATH_SPEEX],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(speex-prefix, AC_HELP_STRING([--with-speex-prefix=DIR], [prefix where libspeex is installed (optional)]), speex_prefix="$withval", speex_prefix="")
AC_ARG_ENABLE(speextest, AC_HELP_STRING([--disable-speextest], [do not try to compile and run a test Speex program]), enable_speextest=$enableval, enable_speextest=yes)

  if test x$speex_prefix != x ; then
    speex_args="$speex_args --prefix=$speex_prefix"
    SPEEX_CFLAGS="-I$speex_prefix/include"
    SPEEX_LIBDIR="-L$speex_prefix/$XINE_LIBNAME"
  fi

  SPEEX_LIBS="$SPEEX_LIBDIR -lspeex -lm"

  AC_MSG_CHECKING(for Speex)
  no_speex=""


  if test "x$enable_speextest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $SPEEX_CFLAGS"
    LIBS="$LIBS $SPEEX_LIBS $OGG_LIBS"
dnl
dnl Now check if the installed Speex is sufficiently new.
dnl
      rm -f conf.speextest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <speex.h>

int main ()
{
  system("touch conf.speextest");
  return 0;
}

],, no_speex=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_speex" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.speextest ; then
       :
     else
       echo "*** Could not run Speex test program, checking why..."
       CFLAGS="$CFLAGS $SPEEX_CFLAGS"
       LIBS="$LIBS $SPEEX_LIBS $OGG_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <speex/codec.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding Speex or finding the wrong"
       echo "*** version of Speex. If it is not finding Speex, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means Speex was incorrectly installed"
       echo "*** or that you have moved Speex since it was installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     SPEEX_CFLAGS=""
     SPEEX_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(SPEEX_CFLAGS)
  AC_SUBST(SPEEX_LIBS)
  rm -f conf.speextest
])
