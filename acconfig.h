#ifndef _CONFIG_H_
#define _CONFIG_H_
@TOP@

#undef XINE_MAJOR
#undef XINE_MINOR
#undef XINE_SUB

/* Define this if you're running x86 architecture */
#undef __i386__

/* Define this if you're running x86 architecture */
#undef ARCH_X86

/* Define this if you're running Alpha architecture */
#undef __alpha__

/* Define this if you're running PowerPC architecture */
#undef __ppc__

/* Define this if you're running PowerPC architecture */
#undef ARCH_PPC

/* Define this if you have the Motorola 74xx CPU */
#undef ENABLE_ALTIVEC

/* Define this if you're running Sparc architecture */
#undef __sparc__ 

/* Define this if you're running Mips architecture */
#undef __mips__ 

/* Define this if you have mlib installed */
#undef HAVE_MLIB

/* Define this if you have mlib installed */
#undef LIBMPEG2_MLIB

/* Define this if you have mlib installed */
#undef LIBA52_MLIB

/* Define this if you have getpwuid_r() function */
#undef HAVE_GETPWUID_R

/* Define this to plugins directory location */
#undef XINE_PLUGINDIR

/* Define this to skins directory location */
#undef XINE_SKINDIR

/* Define this to osd fonts dir location*/
#undef XINE_FONTDIR

/* Path where catalog files will be. */
#undef XINE_LOCALEDIR

/* Define this if you have X11R6 installed */
#undef HAVE_X11

/* Define this if you have libXv installed */
#undef HAVE_XV

/* Define this if you have OpenGL support available */
#undef HAVE_OPENGL

/* Define this if you have GLut support available */
#undef HAVE_GLUT

/* Define this if you have GLU support available */
#undef HAVE_GLU

/* Define this if you have libXinerama installed */
#undef HAVE_XINERAMA

/* Define this if you have libaa installed */
#undef HAVE_AA

/* Define this if you have a usable OSS soundinterface available */
#undef HAVE_OSS

/* Define this if you have Alsa (libasound) installed */
#undef HAVE_ALSA
/* Define this if you have alsa 0.9.x and more installed */
#undef HAVE_ALSA09

/* Define this if you have ESD (libesd) installed */
#undef HAVE_ESD

/* Define this if you have a usable Sun sound interface available */
#undef HAVE_SUNAUDIO

/* Define this if you have a usable IRIX al interface available */
#undef HAVE_IRIXAL

/* Define this if you have ARTS (libartsc) installed */
#undef HAVE_ARTS

/* Define this if you have kernel statistics available via kstat interface */
#undef HAVE_KSTAT

/* Define this if you have CDROM ioctls */
#undef HAVE_CDROM_IOCTLS

/* Define this if you have a suitable version of libdvdnav */
#undef HAVE_DVDNAV

/* Define this if you have ip_mreqn in netinet/in.h */
#undef HAVE_IP_MREQN

/* Define this if you have a dxr3 mpeg encoder card */
#undef HAVE_DXR3

/* Define this if you have libfame mpeg encoder installed (fame.sf.net) */
#undef HAVE_LIBFAME

/* Define this if you have libfame 0.8.10 or above */
#undef HAVE_NEW_LIBFAME

/* Define this if you have librte mpeg encoder installed (zapping.sf.net) */
#undef HAVE_LIBRTE

/* Define one of these to select libmad fixed point arithmetic implementation */
#undef FPM_INTEL
#undef FPM_64BIT
#undef FPM_PPC
#undef FPM_SPARC
#undef FPM_MIPS
#undef FPM_DEFAULT
/* this one isn't implemented: */
#undef FPM_M68K

/* Define this if you have SDL library installed */
#undef HAVE_SDL

@BOTTOM@
/* Disable GCC compiler extensions, if gcc is not in use */
#ifndef	__GNUC__
#define	__attribute__(x)	/*empty*/
#endif

#endif /* _CONFIG_H_ */
