include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(DEFAULT_OCFLAGS) $(VISIBILITY_FLAG)
AM_LDFLAGS = $(xineplug_ldflags)

EXTRA_DIST = diff_from_libmpcdec_1.2.5.patch COPYING

if ENABLE_MUSEPACK
if !WITH_EXTERNAL_LIBMPCDEC
noinst_LTLIBRARIES = libmpcdec.la
endif
endif

libmpcdec_la_SOURCES = \
	huffsv46.c \
	huffsv7.c \
	idtag.c \
	mpc_decoder.c \
	mpc_reader.c \
	requant.c \
	streaminfo.c \
	synth_filter.c \
	mpcdec/config_types.h \
	mpcdec/decoder.h \
	mpcdec/huffman.h \
	mpcdec/internal.h \
	mpcdec/math.h \
	mpcdec/mpcdec.h \
	mpcdec/reader.h \
	mpcdec/requant.h \
	mpcdec/streaminfo.h
