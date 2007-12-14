dnl -----------------
dnl Video out plugins
dnl -----------------
AC_DEFUN([XINE_VIDEO_OUT_PLUGINS], [
    dnl Setup defaults for the target operating system.  For example, syncfb is
    dnl only ever available on Linux, so don't bother checking for it unless
    dnl explicitly requested to do so on other operating systems.
    dnl Notes:
    dnl - dha_kmod is Linux only, but disabled by default
    dnl - directx is Windows only
    dnl - dxr3 is Linux only
    dnl - Mac OS X video is Mac OS X only
    dnl - OpenGL requires Xwindows
    dnl - SyncFB is Linux only, but disabled by default
    dnl - Vidix is FreeBSD and Linux only
    dnl - XvMC and xxmc depend on Xv

    default_enable_aalib=enable
    default_enable_dha_kmod=disable
    default_enable_directfb=disable
    default_enable_directx=disable
    default_enable_dxr3=disable
    default_enable_glu=enable
    default_enable_linuxfb=disable
    default_enable_macosx_video=disable
    default_enable_opengl=enable
    default_enable_syncfb=disable
    default_enable_vidix=disable
    default_enable_xinerama=enable
    default_enable_xvmc=enable

    default_with_caca=with
    default_with_libstk=without
    default_with_sdl=with
    default_with_xcb=with

    case "$host_os" in
        cygwin* | mingw*)
            default_enable_directx=enable
            ;;

        darwin*)
            default_enable_macosx_video=enable
            ;;

        freebsd*)
            default_enable_vidix=enable
            ;;

        linux*)
            default_enable_dxr3=enable
            default_enable_linuxfb=enable
            default_enable_vidix=enable
            enable_linux=yes
            ;;
    esac


    dnl Ascii-Art
    AC_ARG_ENABLE([aalib],
                  [AS_HELP_STRING([--enable-aalib], [enable support for AALIB])],
                  [test x"$enableval" != x"no" && enable_aalib="yes"],
                  [test $default_enable_aalib = disable && enable_aalib="no"])
    if test x"$enable_aalib" != x"no"; then
        ACX_PACKAGE_CHECK([AALIB], [1.4], [aalib-config], [have_aalib=yes], [have_aalib=no])
        if test x"$enable_aalib" = x"yes" && test x"$have_aalib" != x"yes"; then
            AC_MSG_ERROR([aalib support requested, but aalib not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_AA], [test x"$have_aalib" = x"yes"])


    dnl Color AsCii Art
    AC_ARG_WITH([caca],
                [AS_HELP_STRING([--with-caca], [enable support for CACA])],
                [test x"$withval" != x"no" && with_caca="yes"],
                [test $default_with_caca = without && with_caca=no])
    if test x"$with_caca" != x"no"; then
        PKG_CHECK_MODULES([CACA], [caca cucul], [have_caca="yes"], [have_caca="no"])
        if test x"$with_caca" = x"yes" && test x"$have_caca" != x"yes"; then
            AC_MSG_ERROR([CACA support requested, but libcaca 0.99 not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_CACA], [test x"$have_caca" = x"yes"])


    dnl dha (Linux only)
    AC_ARG_ENABLE([dha-kmod],
                  [AS_HELP_STRING([--enable-dha-kmod], [build Linux DHA kernel module])],
                  [test x"$enableval" != x"no" && enable_dha_kmod="yes"],
                  [test $default_enable_dha_kmod = disable && enable_dha_kmod="no"])
    if test x"$enable_dha_kmod" != x"no"; then
        AC_ARG_WITH([linux-path],
                    [AS_HELP_STRING([--with-linux-path=PATH], [where the linux sources are located])],
                    [linux_path="$withval"], [linux_path="/usr/src/linux"])
        LINUX_INCLUDE="-I$linux_path/include"
        AC_SUBST(LINUX_INCLUDE)
        AC_CHECK_PROG([MKNOD], [mknod], [mknod], [no])
        AC_CHECK_PROG([DEPMOD], [depmod], [depmod], [no], ["$PATH:/sbin"])
    fi
    AM_CONDITIONAL([HAVE_LINUX], [test x"$enable_linux" = x"yes"])
    AM_CONDITIONAL([BUILD_DHA_KMOD], [test x"$enable_dha_kmod" != x"no"])


    dnl DirectFB
    AC_ARG_ENABLE([directfb],
                  [AS_HELP_STRING([--enable-directfb], [enable use of DirectFB])],
                  [test x"$enableval" != x"no" && enable_directfb="yes"],
                  [test $default_enable_directfb = disable && enable_directfb="no"])
    if test "x$enable_directfb" = "xyes"; then
        PKG_CHECK_MODULES([DIRECTFB], [directfb >= 0.9.22], [have_directfb=yes], [have_directfb=no])
        if test x"$enable_directfb" = x"yes" && test x"$have_directfb" != x"yes"; then
            AC_MSG_ERROR([DirectFB support requested, but DirectFB not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_DIRECTFB], [test x"$have_directfb" = x"yes"])


    dnl DirectX (see directx.m4)
    AM_PATH_DIRECTX


    dnl dxr3 / hollywood plus card
    AC_ARG_ENABLE([dxr3],
                  [AS_HELP_STRING([--enable-dxr3], [enable support for DXR3/HW+])],
                  [test x"$enableval" != x"no" && enable_dxr3="yes"],
                  [test $default_enable_dxr3 = disable && enable_dxr3="no"])
    if test x"$enable_dxr3" != x"no"; then
        have_dxr3=yes
        AC_MSG_RESULT([*** checking for a supported mpeg encoder])
        AC_CHECK_LIB([fame], [fame_open],
                     [AC_CHECK_HEADERS([fame.h], [have_libfame=yes], [have_libfame=no])], [have_libfame=no])
        if test x"$have_libfame" = x"yes"; then
            have_encoder=yes
            AC_DEFINE([HAVE_LIBFAME], 1, [Define this if you have libfame mpeg encoder installed (fame.sf.net)])
            ACX_PACKAGE_CHECK([LIBFAME], [0.8.10], [libfame-config],
                              [AC_DEFINE([HAVE_NEW_LIBFAME], 1, [Define this if you have libfame 0.8.10 or above])])
        fi
        AC_CHECK_LIB([rte], [rte_init],
                     [AC_CHECK_HEADERS([rte.h], [have_librte=yes], [have_librte=no])], [have_librte=no])
        if test x"$have_librte" = x"yes"; then
            have_encoder=yes
            AC_MSG_WARN([this will probably only work with rte version 0.4!])
            AC_DEFINE([HAVE_LIBRTE], 1, [Define this if you have librte mpeg encoder installed (zapping.sf.net)])
        fi
        if test "$have_encoder" = "yes"; then
            AC_MSG_RESULT([*** found one or more external mpeg encoders])
        else
            AC_MSG_RESULT([*** no external mpeg encoder found])
        fi
    else
        have_dxr3=no have_libfame=no have_librte=no have_encoder=no
    fi
    AM_CONDITIONAL([ENABLE_DXR3], [test x"$have_dxr3" = x"yes"])
    AM_CONDITIONAL([HAVE_LIBFAME], [test x"$have_libfame" = x"yes"])
    AM_CONDITIONAL([HAVE_LIBRTE], [test x"$have_librte" = x"yes"])


    dnl LibSTK - http://www.libstk.net (project appears to be dead)
    AC_ARG_WITH([libstk],
                [AS_HELP_STRING([--with-libstk], [Build with STK surface video driver])],
                [test x"$withval" != x"no" && with_libstk="yes"],
                [test $default_with_libstk = without && with_libstk="no"])
    if test x"$with_libstk" != x"no"; then
        PKG_CHECK_MODULES([LIBSTK], [libstk >= 0.2.0], [have_libstk=yes], [have_libstk=no])
        if test x"$with_libstk" = x"yes" && test x"$have_libstk" != x"yes"; then
            AC_MSG_ERROR([libstk support requested, but libstk not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_STK], [test x"$have_libstk" = x"yes"])


    dnl Linux framebuffer device
    AC_ARG_ENABLE([fb],
                  [AS_HELP_STRING([--enable-fb], [enable Linux framebuffer support])],
                  [test x"$enableval" != x"no" && enable_fb="yes"],
                  [test $default_enable_linuxfb = disable && enable_linuxfb="no"])
    if test x"$enable_linuxfb" != x"no"; then
        AC_CHECK_HEADERS([linux/fb.h], [have_fb=yes], [have_fb=no])
        if test x"$enable_fb" = x"yes" && test x"$have_fb" != x"yes"; then
            AC_MSG_ERROR([Linux framebuffer support requested, but required header file(s) not found])
        elif test x"$have_fb" = x"yes"; then
            dnl This define is needed by src/video_out/video_out_vidix.c
            AC_DEFINE([HAVE_FB], 1, [Define this if you have linux framebuffer support])
        fi
    fi
    AM_CONDITIONAL([ENABLE_FB], [test x"$have_fb" = x"yes"])


    dnl Mac OS X OpenGL video output
    AC_ARG_ENABLE([macosx-video],
                  [AS_HELP_STRING([--enable-macosx-video], [enable support for Mac OS X OpenGL video output])],
                  [test x"$enableval" != x"no" && enable_macosx_video="yes"],
                  [test $default_enable_macosx_video = disable && enable_macosx_video="no"])
    if test x"$enable_macosx_video" != x"no"; then
        AC_MSG_CHECKING([for Mac OS X video output frameworks])
        ac_save_LIBS="$LIBS" LIBS="$LIBS -framework Cocoa -framework OpenGL"
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[return 0]])], [have_macosx_video=yes], [have_macosx_video=no])
        LIBS="$ac_save_LIBS"
        AC_MSG_RESULT([$have_macosx_video])
        if test x"$enable_macosx_video" = x"yes" && test x"$have_macosx_video" != x"yes"; then
            AC_MSG_ERROR([Mac OS X OpenGL video output support requested, but required frameworks not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_MACOSX_VIDEO], [test x"$have_macosx_video" = x"yes"])


    dnl OpenGL, including GLut and/or GLU
    AC_ARG_ENABLE([opengl],
                  [AS_HELP_STRING([--enable-opengl], [enable support for X-based OpenGL video output])],
                  [test x"$enableval" != x"no" && enable_opengl="yes"],
                  [test $default_enable_opengl = disable && enable_opengl="no"])
    AC_ARG_ENABLE([glu],
                  [AS_HELP_STRING([--enable-glu], [enable support for GLU in the OpenGL plugin])],
                  [test x"$enableval" != x"no" && enable_glu="yes"],
                  [test $default_enable_glu = disable && enable_glu="no"])
    if test x"$enable_opengl" != x"no"; then
        if test x"$no_x" = x"yes"; then
            if test x"$enable_opengl" = x"yes"; then
                AC_MSG_ERROR([OpenGL support requested, but X support is disabled])
            fi
            enable_opengl=no
        fi
    fi
    if test x"$enable_opengl" != x"no"; then
        ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS $X_CFLAGS"
        AC_CHECK_LIB([GL], [glBegin],
                     [AC_CHECK_HEADERS([GL/gl.h], [have_opengl=yes], [have_opengl=no])], [have_opengl=no],
                     [$X_LIBS -lm])
        if test x"$enable_opengl" = x"yes" && test x"$have_opengl" != x"yes"; then
            AC_MSG_ERROR([OpenGL support requested, but OpenGL not found])
        elif test x"$have_opengl" = x"yes"; then
            OPENGL_LIBS="-lGL -lm"
            if test x"$enable_glu" != x"no"; then
                have_glu=no
                AC_CHECK_LIB([GLU], [gluPerspective],
                             [AC_CHECK_HEADERS([GL/glu.h],
                                               [AC_MSG_CHECKING([if GLU is sane])
                                                ac_save_LIBS="$LIBS" LIBS="-lGLU $X_LIBS $OPENGL_LIBS $LIBS"
                                                AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <GL/gl.h>
                                                                                  #include <GL/glu.h>]],
                                                                                [[gluPerspective(45.0f, 1.33f, 1.0f, 1000.0f);
                                                                                  glBegin(GL_POINTS); glEnd()]])],
                                                               [have_glu=yes], [have_glu=no])
                                                LIBS="$ac_save_LIBS"
                                                AC_MSG_RESULT([$have_glu])], [have_glu=no])], [have_glu=no],
                             [$X_LIBS $OPENGL_LIBS])
                if test x"$enable_glu" = x"yes" && test x"$have_glu" != x"yes"; then
                    AC_MSG_ERROR([OpenGL GLU support requested, but GLU not found])
                elif test x"$have_glu" = x"yes"; then
                    AC_DEFINE([HAVE_GLU], 1, [Define this if you have GLU support available])
                    GLU_LIBS="-lGLU"
                fi
            fi
        fi
        CPPFLAGS="$ac_save_CPPFLAGS"
        AC_SUBST(OPENGL_CFLAGS)
        AC_SUBST(OPENGL_LIBS)
        AC_SUBST(GLU_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_OPENGL], [test x"$have_opengl" = x"yes"])


    dnl SDL
    AC_ARG_WITH([sdl],
                [AS_HELP_STRING([--with-sdl], [Enable support for SDL video output])],
                [test x"$withval" != x"no" && with_sdl="yes"],
                [test $default_with_sdl = without && with_sdl="no"])
    if test x"$with_sdl" != x"no"; then
        PKG_CHECK_MODULES([SDL], [sdl], [have_sdl=yes], [have_sdl=no])
        if test x"$with_sdl" = x"yes" && test x"$have_sdl" != x"yes"; then
            AC_MSG_ERROR([SDL support requested, but SDL not found])
        fi
    fi
    AM_CONDITIONAL([ENABLE_SDL], [test x"$have_sdl" = x"yes"])


    dnl Solaris framebuffer device support (exists for more than just Solaris)
    AC_CHECK_HEADERS([sys/fbio.h], [have_sunfb=yes], [have_sunfb=no])
    if test x"$have_sunfb" = x"yes"; then
        saved_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS -I/usr/openwin/include"
        saved_LDFLAGS="$LDFLAGS" LDFLAGS="$LDFLAGS -L/usr/openwin/lib"
        AC_CHECK_LIB([dga], [XDgaGrabDrawable],
                     [AC_CHECK_HEADER([dga/dga.h],
                                      [SUNDGA_CFLAGS="-I/usr/openwin/include"
                                       SUNDGA_LIBS="-L/usr/openwin/lib -R/usr/openwin/lib -ldga"
                                       have_sundga=yes])])
        CPPFLAGS="$saved_CPPFLAGS" LDFLAGS="$saved_LDFLAGS"
        AC_SUBST(SUNDGA_CPPFLAGS)
        AC_SUBST(SUNDGA_LIBS)
    fi
    AM_CONDITIONAL([ENABLE_SUNDGA], [test x"$have_sundga" = x"yes"])
    AM_CONDITIONAL([ENABLE_SUNFB], [test x"$have_sunfb" = x"yes"])


    dnl syncfb (Linux only)
    AC_ARG_ENABLE([syncfb],
                  [AS_HELP_STRING([--enable-syncfb], [enable support for syncfb (Linux only)])],
                  [test x"$enableval" != x"no" && enable_syncfb="yes"],
                  [test $default_enable_syncfb = disable && enable_syncfb="no"])
    dnl There's no good test for this.  If the user says so, then do it
    if test x"$enable_syncfb" != x"no" && test x"$no_x" != x"yes"; then
        have_syncfb=yes
    fi
    if test x"$enable_syncfb" = x"yes" && test x"$have_syncfb" != x"yes"; then
        AC_MSG_ERROR([Linux syncfb support requested, but required X support is disabled])
    fi
    AM_CONDITIONAL([ENABLE_SYNCFB], [test x"$have_syncfb" = x"yes"])


    dnl xcb
    AC_ARG_WITH([xcb],
                [AS_HELP_STRING([--with-xcb], [Enable support for XCB video out plugins])],
                [test x"$withval" != x"no" && with_xcb="yes"],
                [test $default_with_xcb = without && with_xcb="no"])
    if test x"$with_xcb" != x"no"; then
        PKG_CHECK_MODULES([XCB], [xcb-shape >= 1.0], [have_xcb=yes], [have_xcb=no])
        if test x"$enable_xcb" = x"yes" && test x"$have_xcb" != x"yes"; then
            AC_MSG_ERROR([XCB support requested, but XCB not found])
        elif test x"$have_xcb" = x"yes"; then
            PKG_CHECK_MODULES([XCBSHM], [xcb-shm], [have_xcbshm=yes], [have_xcbshm=no])
            PKG_CHECK_MODULES([XCBXV], [xcb-xv], [have_xcbxv=yes], [have_xcbxv=no])
        fi
    fi
    AM_CONDITIONAL([ENABLE_XCB], [test x"$have_xcb" = x"yes"])
    AM_CONDITIONAL([ENABLE_XCBSHM], [test x"$have_xcbshm" = x"yes"])
    AM_CONDITIONAL([ENABLE_XCBXV], [test x"$have_xcbxv" = x"yes"])


    dnl vidix/libdha
    dnl Requires X11 or Linux framebuffer
    AC_ARG_ENABLE([vidix],
                  [AS_HELP_STRING([--enable-vidix], [enable support for Vidix])],
                  [test x"$enableval" != x"no" && enable_vidix="yes"],
                  [test $default_enable_vidix = disable && enable_vidix="no"])
    if test x"$enable_vidix" != x"no"; then
        have_vidix=yes
        if test x"$ac_cv_prog_AWK" = x"no"; then
            have_vidix=no
        else
            if test x"$no_x" = x"yes" -o x"$have_fb" != x"yes"; then
                have_vidix=no
            else
                case "$host_or_hostalias" in
                    i?86-*-linux* | k?-*-linux* | athlon-*-linux*) ;;
                    i?86-*-freebsd* | k?-*-freebsd* | athlon-*-freebsd* | i?86-*-kfreebsd*) ;;
                    *) have_vidix="no" ;;
                esac
            fi
        fi
        if test x"$enable_vidix" = x"yes" && test x"$have_vidix" != x"yes"; then
            AC_MSG_ERROR([Vidix support requested, but not all requirements are met])
        fi
    fi
    AM_CONDITIONAL([ENABLE_VIDIX], test x"$have_vidix" = x"yes")


    dnl Xinerama
    AC_ARG_ENABLE([xinerama],
                  [AS_HELP_STRING([--enable-xinerama], [enable support for Xinerama])],
                  [test x"$enableval" != x"no" && enable_xinerama="yes"],
                  [test $default_enable_xinerama = disable && enable_xinerama="no"])
    if test x"$enable_xinerama" != x"no"; then
        if test x"$no_x" != x"yes"; then
            PKG_CHECK_MODULES([XINERAMA], [xinerama], [have_xinerama=yes],
                              [AC_CHECK_LIB([Xinerama], [XineramaQueryExtension],
                                            [XINERAMA_LIBS="-lXinerama" have_xinerama="yes"], [],
                                            [$X_LIBS])])
        fi
        if test x"$enable_xinerama" = x"yes" && test x"$have_xinerama" != x"yes"; then
            AC_MSG_ERROR([Xinerama support requested, but Xinerama not found or X disabled])
        elif test x"$have_xinerama" = x"yes"; then
            AC_DEFINE([HAVE_XINERAMA], 1, [Define this if you have libXinerama installed])
            X_LIBS="$X_LIBS $XINERAMA_LIBS"
        fi
    fi
    AM_CONDITIONAL([ENABLE_XINERAMA], [test x"$have_xinerama" = x"yes"])


    dnl xv
    AC_ARG_WITH([xv-path],
                [AS_HELP_STRING([--with-xv-path=path], [where libXv is installed])])
    dnl With recent XFree86 or Xorg, dynamic linking is preferred!
    dnl Only dynamic linking is possible when using libtool < 1.4.0
    AC_ARG_ENABLE([static-xv],
                  [AS_HELP_STRING([--enable-static-xv], [Enable this to force linking against libXv.a])],
                  [test x"$enableval" != x"no" && xv_prefer_static="yes"], [xv_prefer_static="no"])
    case "$host_or_hostalias" in
        hppa*) xv_libexts="$acl_cv_shlibext" ;;
        *)
            if test x"$xv_prefer_static" = x"yes"; then  
                xv_libexts="$acl_cv_libext $acl_cv_shlibext"
            else
                xv_libexts="$acl_cv_shlibext $acl_cv_libext"
            fi
            ;;
    esac
    if test x"$no_x" != x"yes"; then
        PKG_CHECK_MODULES([XV], [xv], [have_xv=yes], [have_xv=no])
        if test x"$have_xv" = x"no"; then
            dnl No Xv package -- search for it
            for xv_libext in $xv_libexts; do
                xv_lib="libXv.$xv_libext"
                AC_MSG_CHECKING([for $xv_lib])
                for xv_try_path in "$with_xv_path" "$x_libraries" /usr/X11R6/lib /usr/lib; do
                    if test x"$xv_try_path" != x"" && test -f "$xv_try_path/$xv_lib"; then
                        case $xv_lib in
                            *.$acl_cv_libext)   have_xv_static=yes xv_try_libs="$xv_try_path/$xv_lib" ;;
                            *.$acl_cv_shlibext) have_xv_static=no  xv_try_libs="${xv_try_path:+-L}$xv_try_path -lXv" ;;
                        esac
                        ac_save_LIBS="$LIBS" LIBS="$xv_try_libs $X_PRE_LIBS $X_LIBS $X_EXTRA_LIBS $LIBS"
                        AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[XvShmCreateImage()]])], [have_xv=yes], [])
                        LIBS="$ac_save_LIBS"
                        if test x"$have_xv" = x"yes"; then
                            AC_MSG_RESULT([$xv_try_path])
                            XV_LIBS="$xv_try_libs"
                            break
                        fi
                    fi
                done
                test x"$have_xv" = x"yes" && break
                AC_MSG_RESULT([no])
            done
        fi
        if test x"$have_xv" = x"yes"; then
            AC_DEFINE([HAVE_XV], 1, [Define this if you have libXv installed])
        fi
    fi
    AM_CONDITIONAL([HAVE_XV], [test x"$have_xv" = x"yes"])


    dnl XvMC
    AC_ARG_ENABLE([xvmc],
                  [AS_HELP_STRING([--enable-xvmc], [Disable xxmc and XvMC outplut plugins])],
                  [test x"$enableval" != x"no" && enable_xvmc="yes"],
                  [test $default_enable_xvmc = disable && enable_xvmc="no"])
    AC_ARG_WITH([xvmc-path],
                [AS_HELP_STRING([--with-xvmc-path=PATH], [where libXvMC for the xvmc plugin are installed])],
                [], [with_xvmc_path="$x_libraries"])
    AC_ARG_WITH([xvmc-lib],
                [AS_HELP_STRING([--with-xvmc-lib=LIBNAME], [The name of the XvMC library libLIBNAME.so for the xvmc plugin])],
                [], [with_xvmc_lib="XvMCW"])
    AC_ARG_WITH([xxmc-path],
                [AS_HELP_STRING([--with-xxmc-path=PATH], [Where libXvMC for the xxmc plugin are installed])],
                [], [with_xxmc_path="$x_libraries"])
    AC_ARG_WITH([xxmc-lib],
                [AS_HELP_STRING([--with-xxmc-lib=LIBNAME], [The name of the XvMC library libLIBNAME.so for the xxmc plugin])],
                [], [with_xxmc_lib="XvMCW"])
    if test x"$enable_xvmc" != x"no"; then
        if test x"$have_xv" != x"yes"; then
            have_xvmc=no have_xxmc=no have_xvmc_or_xxmc=no
        else
            ac_save_CPPFLAGS="$CPPFLAGS" CPPFLAGS="$CPPFLAGS $X_CFLAGS"
            ac_save_LIBS="$LIBS"

            dnl Check for xxmc
            XXMC_LIBS="${with_xxmc_path:+-L}$with_xxmc_path -l$with_xxmc_lib"
            AC_SUBST(XXMC_LIBS)
            AC_MSG_CHECKING([whether to enable the xxmc plugin with VLD extensions])
            AC_MSG_RESULT([])
            LIBS="$XXMC_LIBS $X_LIBS $XV_LIBS $LIBS"
            AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[XvMCPutSlice()]])], [have_xxmc=yes],
                           [LIBS="$XXMC_LIBS -lXvMC $X_LIBS $XV_LIBS $LIBS"
                            AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[XvMCPutSlice()]])],
                                           [have_xxmc=yes XXMC_LIBS="$XXMC_LIBS -lXvMC"])])
            if test x"$have_xxmc" = x"yes"; then
                AC_CHECK_HEADERS([X11/extensions/vldXvMC.h],
                                 [have_vldexts=yes
                                  AC_DEFINE([HAVE_VLDXVMC], 1, [Define if you have vldXvMC.h])],
                                  [have_vldexts=no])
            else
                AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[XvMCCreateContext()]])], [have_xxmc=yes],
                               [LIBS="$XXMC_LIBS -lXvMC $X_LIBS $XV_LIBS $LIBS"
                                AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[XvMCCreateContext()]])],
                                               [have_xxmc=yes XXMC_LIBS="$XXMC_LIBS -lXvMC"])])
            fi
            if test x"$have_xxmc" = x"yes"; then
                AC_CHECK_HEADERS([X11/extensions/XvMC.h], [], [have_xxmc=no])
            fi

            dnl Check for xvmc
            XVMC_LIBS="${with_xvmc_path:+-L}$with_xvmc_path -l$with_xvmc_lib"
            AC_SUBST(XVMC_LIBS)
            AC_MSG_CHECKING([whether to enable the xvmc plugin])
            AC_MSG_RESULT([])
            LIBS="$XVMC_LIBS $X_LIBS $XV_LIBS $LIBS"
            AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[XvMCCreateContext()]])], [have_xvmc=yes],
                           [LIBS="$XVMC_LIBS -lXvMC $X_LIBS $XV_LIBS $LIBS"
                            AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[XvMCCreateContext()]])],
                                           [have_xvmc=yes XVMC_LIBS="$XVMC_LIBS -lXvMC"])])
            if test x"$have_xvmc" = x"yes"; then
                AC_CHECK_HEADERS([X11/extensions/XvMC.h], [], [have_xvmc=no])
            fi
            CPPFLAGS="$ac_save_CPPFLAGS" LIBS="$ac_save_LIBS"
        fi
        have_xvmc_or_xxmc="$have_xvmc"; test x"$have_xxmc" = x"yes" && have_xvmc_or_xxmc=yes
        if test x"$enable_xvmc" = x"yes" && test x"$have_xvmc_or_xxmc" != x"yes"; then
            AC_MSG_ERROR([XvMC support requested, but neither XvMC nor xxmc could be found, or X is disabled])
        else
            if test x"$have_xvmc" = x"yes"; then
                AC_DEFINE([HAVE_XVMC], 1, [Define this if you have an XvMC library and XvMC.h installed.])
                AC_MSG_RESULT([*** Enabling old xvmc plugin.])
            else
                AC_MSG_RESULT([*** Disabling old xvmc plugin.])
            fi
            if test x"$have_xxmc" = x"yes"; then
                if test x"$have_vldexts" = x"yes"; then
                    AC_MSG_RESULT([*** Enabling xxmc plugin with vld extensions.])
                else
                    AC_MSG_RESULT([*** Enabling xxmc plugin for standard XvMC *only*.])
                fi
            else
                AC_MSG_RESULT([*** Disabling xxmc plugin.])
            fi
        fi
    fi
    AM_CONDITIONAL([ENABLE_XVMC], [test x"$have_xvmc" = x"yes"])
    AM_CONDITIONAL([ENABLE_XXMC], [test x"$have_xxmc" = x"yes"])
])dnl XINE_VIDEO_OUT_PLUGINS
