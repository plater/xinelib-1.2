/*
 * Copyright (C) 2001-2002 the xine project
 *
 * This file is part of xine, a free video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * Quicktime File Demuxer by Mike Melanson (melanson@pcisys.net)
 *  based on a Quicktime parsing experiment entitled 'lazyqt'
 *
 * Ideally, more documentation is forthcoming, but in the meantime:
 * functional flow:
 *  create_qt_info
 *  open_qt_file
 *   parse_moov_atom
 *    parse_mvhd_atom
 *    parse_minf_atom
 *    build_frame_table
 *  free_qt_info
 *
 * $Id: demux_qt.c,v 1.36 2002/06/03 12:43:22 f1rmb Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <zlib.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"

typedef unsigned int qt_atom;

#define BE_16(x) (be2me_16(*(unsigned short *)(x)))
#define BE_32(x) (be2me_32(*(unsigned int *)(x)))

#define QT_ATOM( ch0, ch1, ch2, ch3 )                                \
        ( (long)(unsigned char)(ch3) | ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | ( (long)(unsigned char)(ch0) << 24 ) )

/* top level atoms */
#define FREE_ATOM QT_ATOM('f', 'r', 'e', 'e')
#define JUNK_ATOM QT_ATOM('j', 'u', 'n', 'k')
#define MDAT_ATOM QT_ATOM('m', 'd', 'a', 't')
#define MOOV_ATOM QT_ATOM('m', 'o', 'o', 'v')
#define PNOT_ATOM QT_ATOM('p', 'n', 'o', 't')
#define SKIP_ATOM QT_ATOM('s', 'k', 'i', 'p')
#define WIDE_ATOM QT_ATOM('w', 'i', 'd', 'e')

#define CMOV_ATOM QT_ATOM('c', 'm', 'o', 'v')

#define MVHD_ATOM QT_ATOM('m', 'v', 'h', 'd')
#define MINF_ATOM QT_ATOM('m', 'i', 'n', 'f')

#define VMHD_ATOM QT_ATOM('v', 'm', 'h', 'd')
#define SMHD_ATOM QT_ATOM('s', 'm', 'h', 'd')

/* atoms in a sample table */
#define STSD_ATOM QT_ATOM('s', 't', 's', 'd')
#define STSZ_ATOM QT_ATOM('s', 't', 's', 'z')
#define STSC_ATOM QT_ATOM('s', 't', 's', 'c')
#define STCO_ATOM QT_ATOM('s', 't', 'c', 'o')
#define STTS_ATOM QT_ATOM('s', 't', 't', 's')
#define STSS_ATOM QT_ATOM('s', 't', 's', 's')
#define CO64_ATOM QT_ATOM('c', 'o', '6', '4')

/* placeholder for cutting and pasting */
#define _ATOM QT_ATOM('', '', '', '')

#define ATOM_PREAMBLE_SIZE 8

#define VALID_ENDS   "mov,mp4,qt"

/* still needed, though it shouldn't be... */
typedef struct {
    long        biSize;
    long        biWidth;
    long        biHeight;
    short       biPlanes;
    short       biBitCount;
    long        biCompression;
    long        biSizeImage;
    long        biXPelsPerMeter;
    long        biYPelsPerMeter;
    long        biClrUsed;
    long        biClrImportant;
} BITMAPINFOHEADER;

/* these are things that can go wrong */
typedef enum {
  QT_OK,
  QT_FILE_READ_ERROR,
  QT_NO_MEMORY,
  QT_NOT_A_VALID_FILE,
  QT_NO_MOOV_ATOM,
  QT_NO_ZLIB,
  QT_ZLIB_ERROR,
  QT_HEADER_TROUBLE
} qt_error;

/* there are other types but these are the ones we usually care about */
typedef enum {

  MEDIA_AUDIO,
  MEDIA_VIDEO,
  MEDIA_OTHER

} media_type;

typedef struct {
  media_type type;
  int64_t offset;
  unsigned int size;
  int64_t pts;
  int keyframe;
} qt_frame;

typedef struct {
  unsigned int first_chunk;
  unsigned int samples_per_chunk;
} sample_to_chunk_table_t;

typedef struct {
  unsigned int count;
  unsigned int duration;
} time_to_sample_table_t;

typedef struct {

  /* trak description */
  media_type type;
  union {

    struct {
      unsigned int codec_format;
      unsigned int width;
      unsigned int height;
    } video;

    struct {
      unsigned int codec_format;
      unsigned int sample_rate;
      unsigned int channels;
      unsigned int bits;
    } audio;

  } media_description;

  /* chunk offsets */
  unsigned int chunk_offset_count;
  int64_t *chunk_offset_table;

  /* sample sizes */
  unsigned int sample_size;
  unsigned int sample_size_count;
  unsigned int *sample_size_table;

  /* sync samples, a.k.a., keyframes */
  unsigned int sync_sample_count;
  unsigned int *sync_sample_table;

  /* sample to chunk table */
  unsigned int sample_to_chunk_count;
  sample_to_chunk_table_t *sample_to_chunk_table;

  /* time to sample table */
  unsigned int time_to_sample_count;
  time_to_sample_table_t *time_to_sample_table;

  /* temporary frame table corresponding to this sample table */
  qt_frame *frames;
  unsigned int frame_count;

} qt_sample_table;

typedef struct {
  FILE *qt_file;
  int compressed_header;  /* 1 if there was a compressed moov; just FYI */

  unsigned int creation_time;  /* in ms since Jan-01-1904 */
  unsigned int modification_time;
  unsigned int time_scale;  /* base clock frequency is Hz */
  unsigned int duration;

  int64_t moov_first_offset;
  int64_t moov_last_offset;
  int64_t mdat_first_offset;
  int64_t mdat_last_offset;

  qt_atom audio_codec;
  unsigned int audio_type;
  unsigned int audio_sample_rate;
  unsigned int audio_channels;
  unsigned int audio_bits;

  qt_atom video_codec;
  unsigned int video_type;
  unsigned int video_width;
  unsigned int video_height;

  qt_frame *frames;
  unsigned int frame_count;

  qt_error last_error;
} qt_info;



typedef struct {

  demux_plugin_t       demux_plugin;

  xine_t              *xine;

  config_values_t     *config;

  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;

  input_plugin_t      *input;

  pthread_t            thread;
  int                  thread_running;
  pthread_mutex_t      mutex;
  int                  send_end_buffers;

  off_t                start;
  int                  status;

  qt_info             *qt;
  BITMAPINFOHEADER     bih;
  unsigned int         current_frame;
  unsigned int         last_frame;

} demux_qt_t;

/**********************************************************************
 * lazyqt functions
 **********************************************************************/

/* create a qt_info structure or return NULL if no memory */
qt_info *create_qt_info(void) {
  qt_info *info;

  info = (qt_info *)malloc(sizeof(qt_info));

  if (!info)
    return NULL;

  info->qt_file = NULL;
  info->compressed_header = 0;

  info->creation_time = 0;
  info->modification_time = 0;
  info->time_scale = 0;
  info->duration = 0;

  info->audio_codec = 0;
  info->audio_sample_rate = 0;
  info->audio_channels = 0;
  info->audio_bits = 0;

  info->video_codec = 0;

  info->last_error = QT_OK;

  return info;
}

/* release a qt_info structure and associated data */
void free_qt_info(qt_info *info) {

  if(info) {
    if(info->frames)
      free(info->frames);
    free(info);
    info = NULL;
  }
}

/* returns 1 if the file is determined to be a QT file, 0 otherwise */
static int is_qt_file(input_plugin_t *qt_file) {
  qt_atom atom = 0;

  if (qt_file->seek(qt_file, 4, SEEK_SET) != 4)
    return DEMUX_CANNOT_HANDLE;

  if (qt_file->read(qt_file, (unsigned char *)&atom, 4) != 4)
    return DEMUX_CANNOT_HANDLE;

  atom = be2me_32(atom);

  /* check the first atom against all known top-level atoms */
  if (
    (atom == FREE_ATOM) ||
    (atom == JUNK_ATOM) ||
    (atom == MDAT_ATOM) ||
    (atom == MOOV_ATOM) ||
    (atom == PNOT_ATOM) ||
    (atom == SKIP_ATOM) ||
    (atom == WIDE_ATOM)
  )
    return 1;
  else
    return 0;
}

/* fetch interesting information from the movie header atom */
static void parse_mvhd_atom(qt_info *info, unsigned char *mvhd_atom) {

  info->creation_time = BE_32(&mvhd_atom[0x0C]);
  info->modification_time = BE_32(&mvhd_atom[0x10]);
  info->time_scale = BE_32(&mvhd_atom[0x14]);
  info->duration = BE_32(&mvhd_atom[0x18]);

}

/*
 * This function traverses through a minf atom searching for the sample
 * table atoms, which it loads into an internal sample table structure.
 */
static qt_error parse_minf_atom(qt_sample_table *sample_table,
  unsigned char *minf_atom) {

  int i, j;
  unsigned int minf_atom_size = BE_32(&minf_atom[0]);
  qt_atom header_atom;
  qt_atom current_atom;
  qt_error last_error = QT_OK;

  /* initialize sample table structure */
  sample_table->chunk_offset_table = NULL;
  sample_table->sample_size_table = NULL;
  sample_table->sync_sample_table = NULL;
  sample_table->sample_to_chunk_table = NULL;
  sample_table->time_to_sample_table = NULL;

  /* fetch the media type contained in this minf atom */
  header_atom = BE_32(&minf_atom[0x0C]);
  if (header_atom == VMHD_ATOM)
    sample_table->type = MEDIA_VIDEO;
  else if (header_atom == SMHD_ATOM)
    sample_table->type = MEDIA_AUDIO;
  else
    sample_table->type = MEDIA_OTHER;

  /* search for the useful atoms */
  for (i = ATOM_PREAMBLE_SIZE; i < minf_atom_size - 4; i++) {
    current_atom = BE_32(&minf_atom[i]);

    if (current_atom == STSD_ATOM) {

      if (sample_table->type == MEDIA_VIDEO) {

        /* fetch video parameters */
        sample_table->media_description.video.width =
          BE_16(&minf_atom[i + 0x2C]);
        sample_table->media_description.video.height =
          BE_16(&minf_atom[i + 0x2E]);
        sample_table->media_description.video.codec_format =
          BE_32(&minf_atom[i + 0x10]);

      } else if (sample_table->type == MEDIA_AUDIO) {

        /* fetch audio parameters */
        sample_table->media_description.audio.codec_format =
          BE_32(&minf_atom[i + 0x10]);
        sample_table->media_description.audio.sample_rate =
          BE_16(&minf_atom[i + 0x2C]);
        sample_table->media_description.audio.channels = minf_atom[i + 0x25];
        sample_table->media_description.audio.bits = minf_atom[i + 0x27];

      }

    } else if (current_atom == STSZ_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->sample_size_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->sample_size = BE_32(&minf_atom[i + 8]);
      sample_table->sample_size_count = BE_32(&minf_atom[i + 12]);

      /* allocate space and load table only if sample size is 0 */
      if (sample_table->sample_size == 0) {
        sample_table->sample_size_table = (unsigned int *)malloc(
          sample_table->sample_size_count * sizeof(unsigned int));
        if (!sample_table->sample_size_table) {
          last_error = QT_NO_MEMORY;
          goto free_sample_table;
        }

        /* load the sample size table */
        for (j = 0; j < sample_table->sample_size_count; j++)
          sample_table->sample_size_table[j] =
            BE_32(&minf_atom[i + 16 + j * 4]);
      } else
        /* set the pointer to non-NULL to indicate that the atom type has
         * already been seen for this minf atom */
        sample_table->sample_size_table = (void *)-1;

    } else if (current_atom == STSS_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->sync_sample_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->sync_sample_count = BE_32(&minf_atom[i + 8]);

      sample_table->sync_sample_table = (unsigned int *)malloc(
        sample_table->sync_sample_count * sizeof(unsigned int));
      if (!sample_table->sync_sample_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the sync sample table */
      for (j = 0; j < sample_table->sync_sample_count; j++)
        sample_table->sync_sample_table[j] =
          BE_32(&minf_atom[i + 12 + j * 4]);

    } else if (current_atom == STCO_ATOM) {

      /* there should only be one of either stco or co64 */
      if (sample_table->chunk_offset_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->chunk_offset_count = BE_32(&minf_atom[i + 8]);

      sample_table->chunk_offset_table = (int64_t *)malloc(
        sample_table->chunk_offset_count * sizeof(int64_t));
      if (!sample_table->chunk_offset_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the chunk offset table */
      for (j = 0; j < sample_table->chunk_offset_count; j++)
        sample_table->chunk_offset_table[j] =
          BE_32(&minf_atom[i + 12 + j * 4]);

    } else if (current_atom == CO64_ATOM) {

      /* there should only be one of either stco or co64 */
      if (sample_table->chunk_offset_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->chunk_offset_count = BE_32(&minf_atom[i + 8]);

      sample_table->chunk_offset_table = (int64_t *)malloc(
        sample_table->chunk_offset_count * sizeof(int64_t));
      if (!sample_table->chunk_offset_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the 64-bit chunk offset table */
      for (j = 0; j < sample_table->chunk_offset_count; j++) {
        sample_table->chunk_offset_table[j] =
          BE_32(&minf_atom[i + 12 + j * 8 + 0]);
        sample_table->chunk_offset_table[j] <<= 32;
        sample_table->chunk_offset_table[j] |=
          BE_32(&minf_atom[i + 12 + j * 8 + 4]);
      }

    } else if (current_atom == STSC_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->sample_to_chunk_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->sample_to_chunk_count = BE_32(&minf_atom[i + 8]);

      sample_table->sample_to_chunk_table = (sample_to_chunk_table_t *)malloc(
        sample_table->sample_to_chunk_count * sizeof(sample_to_chunk_table_t));
      if (!sample_table->sample_to_chunk_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the sample to chunk table */
      for (j = 0; j < sample_table->sample_to_chunk_count; j++) {
        sample_table->sample_to_chunk_table[j].first_chunk =
          BE_32(&minf_atom[i + 12 + j * 12 + 0]);
        sample_table->sample_to_chunk_table[j].samples_per_chunk =
          BE_32(&minf_atom[i + 12 + j * 12 + 4]);
      }

    } else if (current_atom == STTS_ATOM) {

      /* there should only be one of these atoms */
      if (sample_table->time_to_sample_table) {
        last_error = QT_HEADER_TROUBLE;
        goto free_sample_table;
      }

      sample_table->time_to_sample_count = BE_32(&minf_atom[i + 8]);

      sample_table->time_to_sample_table = (time_to_sample_table_t *)malloc(
        sample_table->time_to_sample_count * sizeof(time_to_sample_table_t));
      if (!sample_table->time_to_sample_table) {
        last_error = QT_NO_MEMORY;
        goto free_sample_table;
      }

      /* load the time to sample table */
      for (j = 0; j < sample_table->time_to_sample_count; j++) {
        sample_table->time_to_sample_table[j].count =
          BE_32(&minf_atom[i + 12 + j * 8 + 0]);
        sample_table->time_to_sample_table[j].duration =
          BE_32(&minf_atom[i + 12 + j * 8 + 4]);
      }
    }
  }

  return QT_OK;

  /* jump here to make sure everything is free'd and avoid leaking memory */
free_sample_table:
  free(sample_table->chunk_offset_table);
  free(sample_table->sample_size_table);
  free(sample_table->sync_sample_table);
  free(sample_table->sample_to_chunk_table);
  free(sample_table->time_to_sample_table);

  return last_error;
}

static qt_error build_frame_table(qt_sample_table *sample_table) {

  int i, j;
  unsigned int frame_counter;
  unsigned int next_keyframe;
  unsigned int keyframe_index;
  unsigned int chunk_start, chunk_end;
  unsigned int samples_per_chunk;
  unsigned int current_offset;
  int64_t current_pts;
  unsigned int pts_index;
  unsigned int pts_index_countdown;

  /* AUDIO and OTHER frame types follow the same rules; VIDEO follows a
   * different set */
  if (sample_table->type == MEDIA_VIDEO) {

    /* in this case, the total number of frames is equal to the number of
     * entries in the sample size table */
    sample_table->frame_count = sample_table->sample_size_count;
    sample_table->frames = (qt_frame *)malloc(
      sample_table->frame_count * sizeof(qt_frame));
    if (!sample_table->frames)
      return QT_NO_MEMORY;

    /* initialize keyframe management */
    keyframe_index = 0;
    if (sample_table->sync_sample_table) {
      next_keyframe = sample_table->sync_sample_table[keyframe_index++] - 1;
    } else {
      next_keyframe = 0xFFFFFFFF;  /* this means all frames are key */
    }

    /* initialize more accounting variables */
    frame_counter = 0;
    current_pts = 0;
    pts_index = 0;
    pts_index_countdown =
      sample_table->time_to_sample_table[pts_index].count;

    /* iterate through each start chunk in the stsc table */
    for (i = 0; i < sample_table->sample_to_chunk_count; i++) {
      /* iterate from the first chunk of the current table entry to
       * the first chunk of the next table entry */
      chunk_start = sample_table->sample_to_chunk_table[i].first_chunk;
      if (i < sample_table->sample_to_chunk_count - 1)
        chunk_end =
          sample_table->sample_to_chunk_table[i + 1].first_chunk;
      else
        /* if the first chunk is in the last table entry, iterate to the
           final chunk number (the number of offsets in stco table) */
        chunk_end = sample_table->chunk_offset_count + 1;

      /* iterate through each sample in a chunk */
      for (j = chunk_start - 1; j < chunk_end - 1; j++) {

        samples_per_chunk =
          sample_table->sample_to_chunk_table[i].samples_per_chunk;
        current_offset = sample_table->chunk_offset_table[j];
        while (samples_per_chunk > 0) {
          sample_table->frames[frame_counter].type = MEDIA_VIDEO;

          /* figure out the offset and size */
          sample_table->frames[frame_counter].offset = current_offset;
          if (sample_table->sample_size) {
            sample_table->frames[frame_counter].size =
              sample_table->sample_size;
            current_offset += sample_table->sample_size;
          } else {
            sample_table->frames[frame_counter].size =
              sample_table->sample_size_table[frame_counter];
            current_offset +=
              sample_table->sample_size_table[frame_counter];
          }

          /* figure out the keyframe situation for this frame */
          /* if the next_keyframe is all F's, every frame is a keyframe */
          if (next_keyframe == 0xFFFFFFFF)
            sample_table->frames[frame_counter].keyframe = 1;
          else if (next_keyframe == frame_counter) {
            sample_table->frames[frame_counter].keyframe = 1;
            if (keyframe_index < sample_table->sync_sample_count)
              next_keyframe =
                sample_table->sync_sample_table[keyframe_index++] - 1;
            else
              /* this frame number will hopefully never be reached */
              next_keyframe = 0xFFFFFFFE;
          }
          else
            sample_table->frames[frame_counter].keyframe = 0;

          /* figure out the pts situation */
          sample_table->frames[frame_counter].pts = current_pts;
          current_pts +=
            sample_table->time_to_sample_table[pts_index].duration;
          pts_index_countdown--;
          /* time to refresh countdown? */
          if (!pts_index_countdown) {
            pts_index++;
            pts_index_countdown =
              sample_table->time_to_sample_table[pts_index].count;
          }

          samples_per_chunk--;
          frame_counter++;
        }
      }
    }

  } else {

    /* in this case, the total number of frames is equal to the number of
     * chunks */
    sample_table->frame_count = sample_table->chunk_offset_count;
    sample_table->frames = (qt_frame *)malloc(
      sample_table->frame_count * sizeof(qt_frame));
    if (!sample_table->frames)
      return QT_NO_MEMORY;

    for (i = 0; i < sample_table->frame_count; i++) {
      sample_table->frames[i].type = sample_table->type;
      sample_table->frames[i].offset = sample_table->chunk_offset_table[i];
      sample_table->frames[i].size = 0;  /* temporary, of course */
      sample_table->frames[i].keyframe = 0;
      if (sample_table->type == MEDIA_AUDIO)
        sample_table->frames[i].pts =
        sample_table->sample_size_count;   /* stash away for audio pts calc */
      else
        sample_table->frames[i].pts = 0;
    }
  }

  return QT_OK;
}


/*
 * This function takes a pointer to a qt_info structure and a pointer to
 * a buffer containing an uncompressed moov atom. When the function
 * finishes successfully, qt_info will have a list of qt_frame objects,
 * ordered by offset.
 */
static void parse_moov_atom(qt_info *info, unsigned char *moov_atom) {
  int i, j;
  unsigned int moov_atom_size = BE_32(&moov_atom[0]);
  unsigned int sample_table_count = 0;
  qt_sample_table *sample_tables = NULL;
  qt_atom current_atom;
  unsigned int *sample_table_indices;
  unsigned int min_offset_table;
  int64_t min_offset;
  unsigned int audio_byte_counter;
  unsigned int total_audio_bytes;
  int64_t audio_pts_multiplier;

  /* make sure this is actually a moov atom */
  if (BE_32(&moov_atom[4]) != MOOV_ATOM) {
    info->last_error = QT_NO_MOOV_ATOM;
    return;
  }

  /* prowl through the moov atom looking for very specific targets */
  for (i = ATOM_PREAMBLE_SIZE; i < moov_atom_size - 4; i++) {
    current_atom = BE_32(&moov_atom[i]);

    if (current_atom == MVHD_ATOM) {
      parse_mvhd_atom(info, &moov_atom[i - 4]);
      if (info->last_error != QT_OK)
        return;
      i += BE_32(&moov_atom[i - 4]) - 4;
    } else if (current_atom == MINF_ATOM) {

      /* make a new sample temporary sample table */
      sample_table_count++;
      sample_tables = (qt_sample_table *)realloc(sample_tables,
        sample_table_count * sizeof(qt_sample_table));

      parse_minf_atom(&sample_tables[sample_table_count - 1],
        &moov_atom[i - 4]);
      if (info->last_error != QT_OK)
        return;
      i += BE_32(&moov_atom[i - 4]) - 4;
    }
  }

  /* build frame tables corresponding to each sample table */
  info->frame_count = 0;
  for (i = 0; i < sample_table_count; i++) {

    build_frame_table(&sample_tables[i]);
    info->frame_count += sample_tables[i].frame_count;

    /* while traversing tables, look for A/V information */
    if (sample_tables[i].type == MEDIA_VIDEO) {

      info->video_width = sample_tables[i].media_description.video.width;
      info->video_height = sample_tables[i].media_description.video.height;
      info->video_codec =
        sample_tables[i].media_description.video.codec_format;

    } else if (sample_tables[i].type == MEDIA_AUDIO) {

      info->audio_sample_rate =
        sample_tables[i].media_description.audio.sample_rate;
      info->audio_channels =
        sample_tables[i].media_description.audio.channels;
      info->audio_bits =
        sample_tables[i].media_description.audio.bits;
      info->audio_codec =
        sample_tables[i].media_description.audio.codec_format;

    }
  }

  /* allocate the master frame index */
  info->frames = (qt_frame *)malloc(info->frame_count * sizeof(qt_frame));
  if (!info->frames) {
    info->last_error = QT_NO_MEMORY;
    return;
  }

  /* allocate and zero out space for table indices */
  sample_table_indices = (unsigned int *)malloc(
    sample_table_count * sizeof(unsigned int));
  if (!sample_table_indices) {
    info->last_error = QT_NO_MEMORY;
    return;
  }
  for (i = 0; i < sample_table_count; i++) {
    if (sample_tables[i].frame_count == 0)
      sample_table_indices[i] = 0x7FFFFFF;
    else
      sample_table_indices[i] = 0;
  }

  /* merge the tables, order by frame offset */
  for (i = 0; i < info->frame_count; i++) {

    /* get the table number of the smallest frame offset */
    min_offset_table = 0;
    min_offset = sample_tables[0].frames[sample_table_indices[0]].offset;
    for (j = 1; j < sample_table_count; j++) {
      if ((sample_table_indices[j] != info->frame_count) &&
        (sample_tables[j].frames[sample_table_indices[j]].offset <
        min_offset)) {
        min_offset_table = j;
        min_offset = sample_tables[j].frames[sample_table_indices[j]].offset;
      }
    }

    info->frames[i] =
      sample_tables[min_offset_table].frames[sample_table_indices[min_offset_table]];
    sample_table_indices[min_offset_table]++;
    if (sample_table_indices[min_offset_table] >=
      sample_tables[min_offset_table].frame_count)
        sample_table_indices[min_offset_table] = info->frame_count;
  }

  /* fill in the missing and incomplete information (pts and frame sizes) */
  audio_byte_counter = 0;
  audio_pts_multiplier = 90000;
  audio_pts_multiplier *= info->duration;
  audio_pts_multiplier /= info->time_scale;
  for (i = 0; i < info->frame_count; i++) {

    if (info->frames[i].type == MEDIA_VIDEO) {

      /* finish the pts calculation for this video frame */
      info->frames[i].pts *= 90000;
      info->frames[i].pts /= info->time_scale;

    } else if (info->frames[i].type == MEDIA_AUDIO) {

      /* finish the pts calculation for this audio frame */
      total_audio_bytes = info->frames[i].pts;
      info->frames[i].pts = audio_pts_multiplier;
      info->frames[i].pts *= audio_byte_counter;
      info->frames[i].pts /= total_audio_bytes;

      /* figure out the audio frame size */
      if (i < info->frame_count - 1)
        info->frames[i].size =
          info->frames[i + 1].offset - info->frames[i].offset;
      else
        info->frames[i].size =
          info->moov_last_offset - info->frames[i].offset;

      audio_byte_counter += info->frames[i].size;

    }
  }

  /* free the temporary tables on the way out */
/*
  for (j = 0; j < sample_table_count; j++) {
    free(sample_tables[i].chunk_offset_table);
    free(sample_tables[i].sample_size_table);
    free(sample_tables[i].time_to_sample_table);
    free(sample_tables[i].sample_to_chunk_table);
    free(sample_tables[i].sync_sample_table);
  }
*/
  free(sample_tables);
}

static qt_error open_qt_file(qt_info *info, input_plugin_t *input) {

  unsigned char atom_preamble[ATOM_PREAMBLE_SIZE];
  int64_t top_level_atom_size;
  qt_atom top_level_atom;
  unsigned char *moov_atom = NULL;
  int64_t preseek_pos;

  /* zlib stuff */
  z_stream z_state;
  int z_ret_code;
  unsigned char *unzip_buffer;

  /* reset the file */
  if (input->seek(input, 0, SEEK_SET) != 0) {
    info->last_error = QT_FILE_READ_ERROR;
    return info->last_error;
  }

  /* traverse through the file looking for the moov and mdat atoms */
  info->moov_first_offset = info->mdat_first_offset = -1;

  while ((info->moov_first_offset == -1) ||
    (info->mdat_first_offset == -1)) {

    if (input->read(input, atom_preamble, ATOM_PREAMBLE_SIZE) != 
      ATOM_PREAMBLE_SIZE) {
      info->last_error = QT_FILE_READ_ERROR;
      return info->last_error;
    }

    top_level_atom_size = BE_32(&atom_preamble[0]);
    top_level_atom = BE_32(&atom_preamble[4]);
    /* 64-bit length special case */
    if (top_level_atom_size == 1) {
      preseek_pos = input->get_current_pos(input);
      if (input->read(input, atom_preamble, 8) != 8) {
        info->last_error = QT_FILE_READ_ERROR;
        return info->last_error;
      }
      top_level_atom_size = BE_32(&atom_preamble[0]);
      top_level_atom_size <<= 32;
      top_level_atom_size = BE_32(&atom_preamble[4]);

      /* rewind 8 bytes */
      if (input->seek(input, preseek_pos, SEEK_SET) != preseek_pos) {
        info->last_error = QT_FILE_READ_ERROR;
        return info->last_error;
      }
    }

    if (top_level_atom == MDAT_ATOM) {
      info->mdat_first_offset = input->get_current_pos(input) -
        ATOM_PREAMBLE_SIZE;
      info->mdat_last_offset =
        info->mdat_first_offset + top_level_atom_size;

      /* skip to the next atom */
      input->seek(input, top_level_atom_size - ATOM_PREAMBLE_SIZE, SEEK_CUR);
    } else if (top_level_atom == MOOV_ATOM) {
      info->moov_first_offset = input->get_current_pos(input) - 
        ATOM_PREAMBLE_SIZE;
      info->moov_last_offset =
        info->moov_first_offset + top_level_atom_size;

      moov_atom = (unsigned char *)malloc(top_level_atom_size);
      if (moov_atom == NULL) {
        info->last_error = QT_NO_MEMORY;
        return info->last_error;
      }
      /* rewind to start of moov atom */
      if (input->seek(input, info->moov_first_offset, SEEK_SET) != 
        info->moov_first_offset) {
        info->last_error = QT_FILE_READ_ERROR;
        return info->last_error;
      }
      if (input->read(input, moov_atom, top_level_atom_size) != 
        top_level_atom_size) {
        free(moov_atom);
        info->last_error = QT_FILE_READ_ERROR;
        return info->last_error;
      }

      /* check if moov is compressed */
      if (BE_32(&moov_atom[12]) == CMOV_ATOM) {

        info->compressed_header = 1;

        if (BE_32(&moov_atom[12]) == CMOV_ATOM) {
          z_state.next_in = &moov_atom[0x28];
          z_state.avail_in = top_level_atom_size - 0x28;
          z_state.avail_out = BE_32(&moov_atom[0x24]);
          unzip_buffer = (unsigned char *)malloc(BE_32(&moov_atom[0x24]));
          if (!unzip_buffer) {
            info->last_error = QT_NO_MEMORY;
            return info->last_error;
          }
        } else {
          z_state.next_in = &moov_atom[ATOM_PREAMBLE_SIZE];
          z_state.avail_in = top_level_atom_size - ATOM_PREAMBLE_SIZE;
          z_state.avail_out = 65536;
          unzip_buffer = (unsigned char *)malloc(65536);
          if (!unzip_buffer) {
            info->last_error = QT_NO_MEMORY;
            return info->last_error;
          }
        }

        z_state.next_out = unzip_buffer;
        z_state.zalloc = (alloc_func)0;
        z_state.zfree = (free_func)0;
        z_state.opaque = (voidpf)0;

        z_ret_code = inflateInit (&z_state);
        if (Z_OK != z_ret_code) {
          info->last_error = QT_ZLIB_ERROR;
          return info->last_error;
        }

        z_ret_code = inflate(&z_state, Z_NO_FLUSH);
        if ((z_ret_code != Z_OK) && (z_ret_code != Z_STREAM_END)) {
          info->last_error = QT_ZLIB_ERROR;
          return info->last_error;
        }

        z_ret_code = inflateEnd(&z_state);
        if (Z_OK != z_ret_code) {
          info->last_error = QT_ZLIB_ERROR;
          return info->last_error;
        }

        /* replace the compressed moov atom with the decompressed atom */
        free (moov_atom);
        moov_atom = unzip_buffer;
        top_level_atom_size = BE_32 (&moov_atom[0]);
      }
    } else {
      input->seek(input, top_level_atom_size - ATOM_PREAMBLE_SIZE, SEEK_CUR);
    }
  }

  /* take apart the moov atom */
  if ((info->moov_first_offset != -1) &&
    (info->mdat_first_offset != -1))
    parse_moov_atom(info, moov_atom);

  if (!moov_atom) {
    info->last_error = QT_NO_MOOV_ATOM;
    return info->last_error;
  }

  return QT_OK;
}

/**********************************************************************
 * xine demuxer functions
 **********************************************************************/

static void *demux_qt_loop (void *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf = NULL;
  int64_t last_frame_pts = 0;
  unsigned int i;
  unsigned int remaining_sample_bytes;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      i = this->current_frame;

      /* if there is an incongruency between last and current sample, it
       * must be time to send a new pts */
      if (this->last_frame + 1 != this->current_frame) {
        xine_flush_engine(this->xine);

        /* send new pts */
        buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
        buf->type = BUF_CONTROL_NEWPTS;
        buf->disc_off = this->qt->frames[i].pts;
        this->video_fifo->put (this->video_fifo, buf);

        if (this->audio_fifo) {
          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
          buf->type = BUF_CONTROL_NEWPTS;
          buf->disc_off = this->qt->frames[i].pts;
          this->audio_fifo->put (this->audio_fifo, buf);
        }

        /* set last_frame_pts to some sane value on seek */
        last_frame_pts = this->qt->frames[i].pts - 3000;
      }

      /* check if all the samples have been sent */
      if (i >= this->qt->frame_count) {
        this->status = DEMUX_FINISHED;
        break;
      }

      if (this->qt->frames[i].type == MEDIA_VIDEO) {
        remaining_sample_bytes = this->qt->frames[i].size;
        this->input->seek(this->input, this->qt->frames[i].offset,
          SEEK_SET);

        while (remaining_sample_bytes) {
          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->content = buf->mem;
          buf->type = this->qt->video_type;
          buf->input_pos = this->qt->frames[i].offset;
          buf->pts = this->qt->frames[i].pts;
          buf->decoder_flags = 0;

          if (last_frame_pts) {
            buf->decoder_flags |= BUF_FLAG_FRAMERATE;
            buf->decoder_info[0] = buf->pts - last_frame_pts;
          }

          if (remaining_sample_bytes > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = remaining_sample_bytes;
          remaining_sample_bytes -= buf->size;

          if (this->input->read(this->input, buf->content, buf->size) !=
            buf->size) {
            this->status = DEMUX_FINISHED;
            break;
          }

          if (this->qt->frames[i].keyframe)
            buf->decoder_flags |= BUF_FLAG_KEYFRAME;
          if (!remaining_sample_bytes)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;

          this->video_fifo->put(this->video_fifo, buf);
        }
        last_frame_pts = buf->pts;
      } else if ((this->qt->frames[i].type == MEDIA_AUDIO) && 
          this->audio_fifo) {
        /* load an audio sample and packetize it */
        remaining_sample_bytes = this->qt->frames[i].size;
        this->input->seek(this->input, this->qt->frames[i].offset,
          SEEK_SET);

        while (remaining_sample_bytes) {
          buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
          buf->content = buf->mem;
          buf->type = this->qt->audio_type;
          buf->input_pos = this->qt->frames[i].offset;
          buf->pts = this->qt->frames[i].pts;
          buf->decoder_flags = 0;

          if (remaining_sample_bytes > buf->max_size)
            buf->size = buf->max_size;
          else
            buf->size = remaining_sample_bytes;
          remaining_sample_bytes -= buf->size;

          if (this->input->read(this->input, buf->content, buf->size) !=
            buf->size) {
            this->status = DEMUX_FINISHED;
            break;
          }

          if (!remaining_sample_bytes)
            buf->decoder_flags |= BUF_FLAG_FRAME_END;
          this->audio_fifo->put(this->audio_fifo, buf);
        }
      }


      this->last_frame = this->current_frame;
      this->current_frame++;

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );

    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->video_fifo->size(this->video_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while (this->status == DEMUX_OK);

  printf ("demux_qt: demux loop finished (status: %d)\n",
          this->status);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_flags   = BUF_FLAG_END_STREAM; /* stream finished */
    this->video_fifo->put (this->video_fifo, buf);

    if(this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type            = BUF_CONTROL_END;
      buf->decoder_flags   = BUF_FLAG_END_STREAM; /* stream finished */
      this->audio_fifo->put (this->audio_fifo, buf);
    }
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);
  return NULL;
}

static int demux_qt_open(demux_plugin_t *this_gen,
                         input_plugin_t *input, int stage) {

  demux_qt_t *this = (demux_qt_t *) this_gen;

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    return is_qt_file(input);
  }
  break;

  case STAGE_BY_EXTENSION: {
    char *suffix;
    char *MRL;
    char *m, *valid_ends;

    MRL = input->get_mrl (input);

    suffix = strrchr(MRL, '.');

    if(!suffix)
      return DEMUX_CANNOT_HANDLE;

    xine_strdupa(valid_ends, (this->config->register_string(this->config,
                 "mrl.ends_qt", VALID_ENDS,
                 "valid mrls ending for qt demuxer",
                 NULL, NULL, NULL)));
    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

      while(*m == ' ' || *m == '\t') m++;

      if(!strcasecmp((suffix + 1), m)) {
        this->input = input;
        return DEMUX_CAN_HANDLE;
      }
    }
    return DEMUX_CANNOT_HANDLE;
  }
  break;

  default:
    return DEMUX_CANNOT_HANDLE;
    break;

  }

  return DEMUX_CANNOT_HANDLE;
}

static int demux_qt_start (demux_plugin_t *this_gen,
                            fifo_buffer_t *video_fifo,
                            fifo_buffer_t *audio_fifo,
                            off_t start_pos, int start_time) {
  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf;
  int err;
  unsigned int le_fourcc;

  pthread_mutex_lock(&this->mutex);

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    /* create the QT structure */
    if ((this->qt = create_qt_info()) == NULL) {
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }

    /* open the QT file */
    if (open_qt_file(this->qt, this->input) != QT_OK) {
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }

    this->bih.biWidth = this->qt->video_width;
    this->bih.biHeight = this->qt->video_height;

    /* fourcc was stored in opposite byte order that mapping routine wants */
    le_fourcc =
      ((this->qt->video_codec & 0xFF000000) >> 24) |
      ((this->qt->video_codec & 0x00FF0000) >>  8) |
      ((this->qt->video_codec & 0x0000FF00) <<  8) |
      ((this->qt->video_codec & 0x000000FF) << 24);
    this->qt->video_type = fourcc_to_buf_video(&le_fourcc);

    /* fourcc was stored in opposite byte order that mapping routine wants */
    le_fourcc =
      ((this->qt->audio_codec & 0xFF000000) >> 24) |
      ((this->qt->audio_codec & 0x00FF0000) >>  8) |
      ((this->qt->audio_codec & 0x0000FF00) <<  8) |
      ((this->qt->audio_codec & 0x000000FF) << 24);
    this->qt->audio_type = formattag_to_buf_audio(le_fourcc);

    /* print vital stats */
    xine_log (this->xine, XINE_LOG_FORMAT,
      _("demux_qt: Apple Quicktime file, %srunning time: %d min, %d sec\n"),
      (this->qt->compressed_header) ? "compressed header, " : "",
      this->qt->duration / this->qt->time_scale / 60,
      this->qt->duration / this->qt->time_scale % 60);
    if (this->qt->video_codec)
      xine_log (this->xine, XINE_LOG_FORMAT,
        _("demux_qt: '%c%c%c%c' video @ %dx%d, %d Hz playback clock\n"),
        (this->qt->video_codec >> 24) & 0xFF,
        (this->qt->video_codec >> 16) & 0xFF,
        (this->qt->video_codec >>  8) & 0xFF,
        (this->qt->video_codec >>  0) & 0xFF,
        this->bih.biWidth,
        this->bih.biHeight,
        this->qt->time_scale);
    if (this->qt->audio_codec)
      xine_log (this->xine, XINE_LOG_FORMAT,
        _("demux_qt: '%c%c%c%c' audio @ %d Hz, %d bits, %d channel%c\n"),
        (this->qt->audio_codec >> 24) & 0xFF,
        (this->qt->audio_codec >> 16) & 0xFF,
        (this->qt->audio_codec >>  8) & 0xFF,
        (this->qt->audio_codec >>  0) & 0xFF,
        this->qt->audio_sample_rate,
        this->qt->audio_bits,
        this->qt->audio_channels,
        (this->qt->audio_channels == 1) ? 0 : 's');

    /* send start buffers */
    buf = this->video_fifo->buffer_pool_alloc(this->video_fifo);
    buf->type = BUF_CONTROL_START;
    this->video_fifo->put(this->video_fifo, buf);

    if (this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc(this->audio_fifo);
      buf->type = BUF_CONTROL_START;
      this->audio_fifo->put(this->audio_fifo, buf);
    }

    /* send init info to decoders */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->content = buf->mem;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = 3000;  /* initial video_step */
    memcpy(buf->content, &this->bih, sizeof(this->bih));
    buf->size = sizeof(this->bih);
    buf->type = this->qt->video_type;
    this->video_fifo->put (this->video_fifo, buf);

    if (this->audio_fifo) {
      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->content = buf->mem;
      buf->type = this->qt->audio_type;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->qt->audio_sample_rate;
      buf->decoder_info[2] = this->qt->audio_bits;
      buf->decoder_info[3] = this->qt->audio_channels;
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    this->current_frame = 0;
    this->last_frame = 0;

    if ((err = pthread_create (&this->thread, NULL, demux_qt_loop, this)) != 0) {
      printf ("demux_qt: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);
return 0;
}


static int demux_qt_seek (demux_plugin_t *this_gen,
                             off_t start_pos, int start_time) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  int i;
  int best_index;

  pthread_mutex_lock(&this->mutex);

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return this->status;
  }

  /* perform a linear search through the table to get the closest offset */
  best_index = this->qt->frame_count - 1;
  for (i = 0; i < this->qt->frame_count; i++) {
    if (this->qt->frames[i].offset > start_pos) {
      best_index = i;
      break;
    }
  }

  /* search back in the table for the nearest keyframe */
  while (best_index--) {
    if (this->qt->frames[best_index].keyframe) {
      this->current_frame = best_index;
      break;
    }
  }

  this->status = DEMUX_OK;
  pthread_mutex_unlock( &this->mutex );

  return this->status;
}

static void demux_qt_stop (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;
  buf_element_t *buf;
  void *p;

  pthread_mutex_lock( &this->mutex );

  if (!this->thread_running) {
    pthread_mutex_unlock( &this->mutex );
    return;
  }

  this->send_end_buffers = 0;
  this->status = DEMUX_FINISHED;

  pthread_mutex_unlock( &this->mutex );
  pthread_join (this->thread, &p);

  xine_flush_engine(this->xine);

  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
  buf->type            = BUF_CONTROL_END;
  buf->decoder_flags   = BUF_FLAG_END_USER; /* user finished */
  this->video_fifo->put (this->video_fifo, buf);

  if(this->audio_fifo) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type            = BUF_CONTROL_END;
    buf->decoder_flags   = BUF_FLAG_END_USER; /* user finished */
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static void demux_qt_close (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  free_qt_info(this->qt);
  free(this);
  pthread_mutex_destroy (&this->mutex);
}

static int demux_qt_get_status (demux_plugin_t *this_gen) {
  demux_qt_t *this = (demux_qt_t *) this_gen;

  return this->status;
}

static char *demux_qt_get_id(void) {
  return "QUICKTIME";
}

static char *demux_qt_get_mimetypes(void) {
  return "video/quicktime: mov,qt: Quicktime animation;"
         "video/x-quicktime: mov,qt: Quicktime animation;";
}

/* return the approximate length in seconds */
static int demux_qt_get_stream_length (demux_plugin_t *this_gen) {

  demux_qt_t *this = (demux_qt_t *) this_gen;

  return this->qt->duration;
}

demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {

  demux_qt_t      *this;

  if (iface != 8) {
    printf ("demux_qt: plugin doesn't support plugin API version %d.\n"
            "          this means there's a version mismatch between xine and this "
            "          demuxer plugin.\nInstalling current demux plugins should
help.\n",
            iface);
    return NULL;
  }

  this         = xine_xmalloc (sizeof (demux_qt_t));
  this->config = xine->config;
  this->xine   = xine;

  (void*) this->config->register_string(this->config,
                                        "mrl.ends_qt", VALID_ENDS,
                                        "valid mrls ending for qt demuxer",
                                        NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_qt_open;
  this->demux_plugin.start             = demux_qt_start;
  this->demux_plugin.seek              = demux_qt_seek;
  this->demux_plugin.stop              = demux_qt_stop;
  this->demux_plugin.close             = demux_qt_close;
  this->demux_plugin.get_status        = demux_qt_get_status;
  this->demux_plugin.get_identifier    = demux_qt_get_id;
  this->demux_plugin.get_stream_length = demux_qt_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_qt_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init( &this->mutex, NULL );

  return (demux_plugin_t *) this;
}
