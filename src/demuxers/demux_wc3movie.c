/*
 * Copyright (C) 2000-2002 the xine project
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
 * File Demuxer for Wing Commander III MVE movie files
 *   by Mike Melanson (melanson@pcisys.net)
 * For more information on the MVE file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * $Id: demux_wc3movie.c,v 1.3 2002/09/04 02:43:48 tmmm Exp $
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

#include "xine_internal.h"
#include "xineutils.h"
#include "compat.h"
#include "demux.h"
#include "bswap.h"

#define BE_16(x) (be2me_16(*(unsigned short *)(x)))
#define BE_32(x) (be2me_32(*(unsigned int *)(x)))
#define LE_16(x) (le2me_16(*(unsigned short *)(x)))
#define LE_32(x) (le2me_32(*(unsigned int *)(x)))

#define FOURCC_TAG( ch0, ch1, ch2, ch3 ) \
        ( (long)(unsigned char)(ch3) | \
        ( (long)(unsigned char)(ch2) << 8 ) | \
        ( (long)(unsigned char)(ch1) << 16 ) | \
        ( (long)(unsigned char)(ch0) << 24 ) )

#define FORM_TAG FOURCC_TAG('F', 'O', 'R', 'M')
#define MOVE_TAG FOURCC_TAG('M', 'O', 'V', 'E')
#define PC_TAG   FOURCC_TAG('_', 'P', 'C', '_')
#define SOND_TAG FOURCC_TAG('S', 'O', 'N', 'D')
#define PALT_TAG FOURCC_TAG('P', 'A', 'L', 'T')
#define BRCH_TAG FOURCC_TAG('B', 'R', 'C', 'H')
#define SHOT_TAG FOURCC_TAG('S', 'H', 'O', 'T')
#define VGA_TAG  FOURCC_TAG('V', 'G', 'A', ' ')
#define AUDI_TAG FOURCC_TAG('A', 'U', 'D', 'I')
#define TEXT_TAG FOURCC_TAG('T', 'E', 'X', 'T')

#define PALETTE_SIZE 256
#define PALETTE_CHUNK_SIZE (PALETTE_SIZE * 3)
#define WC3_FRAMERATE 15
#define WC3_PTS_INC (90000 / 15)

#define PREAMBLE_SIZE 8

#define VALID_ENDS   "mve"

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

  int                  status;

  unsigned int         fps;
  unsigned int         frame_pts_inc;

  xine_waveformatex    wave;

  unsigned int         number_of_palettes;
  palette_entry_t     *palettes;

  off_t                data_start;
  off_t                data_size;

} demux_mve_t;

/* bizarre palette lookup table */
unsigned char wc3_pal_lookup[] = {
0x00, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0E, 0x10, 0x12, 0x13, 0x15, 0x16,
0x18, 0x19, 0x1A,
0x1C, 0x1D, 0x1F, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2A, 0x2C,
0x2D, 0x2E, 0x2F,
0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3F,
0x40, 0x41, 0x42,
0x43, 0x44, 0x45, 0x46, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
0x51, 0x52, 0x53,
0x54, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61,
0x62, 0x63, 0x64,
0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71,
0x72, 0x73, 0x74,
0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7D, 0x7E, 0x7F, 0x80,
0x81, 0x82, 0x83,
0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8D, 0x8E, 0x8F,
0x90, 0x91, 0x92,
0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E,
0x9F, 0xA0, 0xA1,
0xA2, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAA, 0xAB, 0xAC,
0xAD, 0xAE, 0xAF,
0xB0, 0xB1, 0xB2, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xB9, 0xBA,
0xBB, 0xBC, 0xBD,
0xBE, 0xBF, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC5, 0xC6, 0xC7, 0xC8,
0xC9, 0xCA, 0xCB,
0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD5,
0xD6, 0xD7, 0xD8,
0xD9, 0xDA, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3,
0xE4, 0xE4, 0xE5,
0xE6, 0xE7, 0xE8, 0xE9, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xED, 0xEE, 0xEF, 0xF0,
0xF1, 0xF1, 0xF2,
0xF3, 0xF4, 0xF5, 0xF6, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFA, 0xFB, 0xFC, 0xFD,
0xFD, 0xFD, 0xFD
};

static void *demux_mve_loop (void *this_gen) {

  demux_mve_t *this = (demux_mve_t *) this_gen;
  buf_element_t *buf = NULL;
  int64_t audio_pts = 0;
  int64_t video_pts = 0;
  unsigned char preamble[PREAMBLE_SIZE];
  unsigned int chunk_tag;
  unsigned int chunk_size;
  unsigned int audio_frames;
  uint64_t total_frames = 0;
  off_t current_file_pos;
  unsigned int palette_number;

  pthread_mutex_lock( &this->mutex );

  /* do-while needed to seek after demux finished */
  do {
    /* main demuxer loop */
    while (this->status == DEMUX_OK) {

      /* someone may want to interrupt us */
      pthread_mutex_unlock( &this->mutex );
      pthread_mutex_lock( &this->mutex );

      /* compensate for the initial data in the file */
      current_file_pos = this->input->get_current_pos(this->input) -
        this->data_start;

      if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
        PREAMBLE_SIZE)
        this->status = DEMUX_FINISHED;
      else {
        chunk_tag = BE_32(&preamble[0]);
        /* round up to the nearest even size */
        chunk_size = (BE_32(&preamble[4]) + 1) & (~1);

        if (chunk_tag == BRCH_TAG) {

          /* empty chunk; do nothing */

        } else if (chunk_tag == SHOT_TAG) {

          /* this is the start of a new shot; send a new palette */
          if (this->input->read(this->input, preamble, 4) != 4) {
            this->status = DEMUX_FINISHED;
            break;
          }
          palette_number = LE_32(&preamble[0]);

          if (palette_number >= this->number_of_palettes) {
            xine_log(this->xine, XINE_LOG_FORMAT,
              _("demux_wc3movie: SHOT chunk referenced invalid palette (%d >= %d)\n"),
              palette_number, this->number_of_palettes);
            this->status = DEMUX_FINISHED;
            break;
          }

          buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
          buf->decoder_flags = BUF_FLAG_SPECIAL;
          buf->decoder_info[1] = BUF_SPECIAL_PALETTE;
          buf->decoder_info[2] = PALETTE_SIZE;
          buf->decoder_info[3] = 
            (unsigned int)&this->palettes[PALETTE_SIZE * palette_number];
          buf->size = 0;
          buf->type = BUF_VIDEO_WC3;
          this->video_fifo->put (this->video_fifo, buf);

        } else if (chunk_tag == AUDI_TAG) {

          audio_frames = 
            chunk_size * 8 / this->wave.wBitsPerSample / 
            this->wave.nChannels;
          total_frames += audio_frames;
          audio_pts = total_frames;
          audio_pts *= 90000;
          audio_pts /= this->wave.nSamplesPerSec;

          while (chunk_size) {
            buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
            buf->type = BUF_AUDIO_LPCM_LE;
            buf->input_pos = current_file_pos;
            buf->input_length = this->data_size;
            buf->input_time = audio_pts / 90000;
            buf->pts = audio_pts;

            if (chunk_size > buf->max_size)
              buf->size = buf->max_size;
            else
              buf->size = chunk_size;
            chunk_size -= buf->size;

            if (this->input->read(this->input, buf->content, buf->size) !=
              buf->size) {
              buf->free_buffer(buf);
              this->status = DEMUX_FINISHED;
              break;
            }

            if (!chunk_size)
              buf->decoder_flags |= BUF_FLAG_FRAME_END;

            this->audio_fifo->put (this->audio_fifo, buf);
          }

        } else if (chunk_tag == VGA_TAG) {

          while (chunk_size) {
            buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
            buf->type = BUF_VIDEO_WC3;
            buf->input_pos = current_file_pos;
            buf->input_length = this->data_size;
            buf->input_time = video_pts / 90000;
            buf->pts = video_pts;

            if (chunk_size > buf->max_size)
              buf->size = buf->max_size;
            else
              buf->size = chunk_size;
            chunk_size -= buf->size;

            if (this->input->read(this->input, buf->content, buf->size) !=
              buf->size) {
              buf->free_buffer(buf);
              this->status = DEMUX_FINISHED;
              break;
            }

            if (!chunk_size)
              buf->decoder_flags |= BUF_FLAG_FRAME_END;

            this->video_fifo->put (this->video_fifo, buf);
          }
          video_pts += WC3_PTS_INC;

        } else if (chunk_tag == TEXT_TAG) {

          /* unhandled thus far */
          this->input->seek(this->input, chunk_size, SEEK_CUR);

        } else {

          /* report an unknown chunk and skip it */
          printf (_("demux_wc3movie: encountered unknown chunk: %c%c%c%c\n"),
            (chunk_tag >> 24) & 0xFF,
            (chunk_tag >> 16) & 0xFF,
            (chunk_tag >>  8) & 0xFF,
            (chunk_tag >>  0) & 0xFF);
          this->input->seek(this->input, chunk_size, SEEK_CUR);
        }
      }
    }

    /* wait before sending end buffers: user might want to do a new seek */
    while(this->send_end_buffers && this->audio_fifo->size(this->audio_fifo) &&
          this->status != DEMUX_OK){
      pthread_mutex_unlock( &this->mutex );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &this->mutex );
    }

  } while (this->status == DEMUX_OK);

  printf ("demux_wc3movie: demux loop finished (status: %d)\n",
          this->status);

  /* seek back to the beginning of the data in preparation for another
   * start */
  this->input->seek(this->input, this->data_start, SEEK_SET);

  this->status = DEMUX_FINISHED;

  if (this->send_end_buffers) {
    xine_demux_control_end(this->xine, BUF_FLAG_END_STREAM);
  }

  this->thread_running = 0;
  pthread_mutex_unlock(&this->mutex);
  return NULL;
}

static int demux_mve_open(demux_plugin_t *this_gen, input_plugin_t *input,
                          int stage) {
  demux_mve_t *this = (demux_mve_t *) this_gen;
  char header[16];

  this->input = input;

  switch(stage) {
  case STAGE_BY_CONTENT: {
    if ((input->get_capabilities(input) & INPUT_CAP_SEEKABLE) == 0)
      return DEMUX_CANNOT_HANDLE;

    input->seek(input, 0, SEEK_SET);
    if (input->read(input, header, 16) != 16)
      return DEMUX_CANNOT_HANDLE;

    if ((BE_32(&header[0]) == FORM_TAG) &&
        (BE_32(&header[8]) == MOVE_TAG) &&
        (BE_32(&header[12]) == PC_TAG))
      return DEMUX_CAN_HANDLE;

    return DEMUX_CANNOT_HANDLE;
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
                                                            "mrl.ends_mve", VALID_ENDS,
                                                            _("valid mrls ending for mve demuxer"),
                                                            NULL, NULL, NULL)));    while((m = xine_strsep(&valid_ends, ",")) != NULL) {

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

static int demux_mve_start (demux_plugin_t *this_gen,
                            fifo_buffer_t *video_fifo,
                            fifo_buffer_t *audio_fifo,
                            off_t start_pos, int start_time) {

  demux_mve_t *this = (demux_mve_t *) this_gen;
  buf_element_t *buf;
  int err;
  unsigned char preamble[PREAMBLE_SIZE];
  unsigned char disk_palette[PALETTE_CHUNK_SIZE];
  int i, j;
  unsigned char r, g, b;
  int temp;

  /* if thread is not running, initialize demuxer */
  if (!this->thread_running) {
    this->video_fifo = video_fifo;
    this->audio_fifo = audio_fifo;

    /* load the number of palettes, the only interesting piece of information
     * in the _PC_ chunk; take it for granted that it will always appear at
     * position 0x1C */
    this->input->seek(this->input, 0x1C, SEEK_SET);
    if (this->input->read(this->input, preamble, 4) != 4) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }
    this->number_of_palettes = LE_32(&preamble[0]);

    /* skip the SOND chunk */
    this->input->seek(this->input, 12, SEEK_CUR);

    /* load the palette chunks */
    this->palettes = xine_xmalloc(this->number_of_palettes * PALETTE_SIZE *
      sizeof(palette_entry_t));
    for (i = 0; i < this->number_of_palettes; i++) {
      /* make sure there was a valid palette chunk preamble */
      if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
        PREAMBLE_SIZE) {
        this->status = DEMUX_FINISHED;
        pthread_mutex_unlock(&this->mutex);
        return DEMUX_FINISHED;
      }

      if ((BE_32(&preamble[0]) != PALT_TAG) || 
          (BE_32(&preamble[4]) != PALETTE_CHUNK_SIZE)) {
        xine_log(this->xine, XINE_LOG_FORMAT,
          _("demux_wc3movie: There was a problem while loading palette chunks\n"));
        this->status = DEMUX_FINISHED;
        pthread_mutex_unlock(&this->mutex);
        return DEMUX_FINISHED;
      }

      /* load the palette chunk */
      if (this->input->read(this->input, disk_palette, PALETTE_CHUNK_SIZE) !=
        PALETTE_CHUNK_SIZE) {
        this->status = DEMUX_FINISHED;
        pthread_mutex_unlock(&this->mutex);
        return DEMUX_FINISHED;
      }

      /* convert and store the palette */
      for (j = 0; j < PALETTE_SIZE; j++) {
        r = disk_palette[j * 3 + 0];
        g = disk_palette[j * 3 + 1];
        b = disk_palette[j * 3 + 2];
        /* rotate each component left by 2 */
        temp = r << 2; r = (temp & 0xff) | (temp >> 8);
        r = wc3_pal_lookup[r];
        temp = g << 2; g = (temp & 0xff) | (temp >> 8);
        g = wc3_pal_lookup[g];
        temp = b << 2; b = (temp & 0xff) | (temp >> 8);
        b = wc3_pal_lookup[b];
        this->palettes[i * 256 + j].r = r;
        this->palettes[i * 256 + j].g = g;
        this->palettes[i * 256 + j].b = b;
      }
    }

    /* next should be the INDX chunk; skip it */
    if (this->input->read(this->input, preamble, PREAMBLE_SIZE) !=
      PREAMBLE_SIZE) {
      this->status = DEMUX_FINISHED;
      pthread_mutex_unlock(&this->mutex);
      return DEMUX_FINISHED;
    }
    this->input->seek(this->input, BE_32(&preamble[4]), SEEK_CUR);

    /* note the data start offset right after the INDEX chunks */
    this->data_start = this->input->get_current_pos(this->input);

    this->data_size = this->input->get_length(this->input) - this->data_start;

    /* send start buffers */
    xine_demux_control_start(this->xine);

    /* send new pts */
    xine_demux_control_newpts(this->xine, 0, 0);

    /* send init info to decoders */
    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = WC3_PTS_INC;  /* initial video_step */
    /* You know what? Since WC3 movies are hardcoded to a resolution of
     * 320x165 and the video decoder knows that, I won't even bother
     * transmitting the video resolution. (Ordinarily, the video
     * resolution is transmitted to the video decoder at this stage.) */
    buf->size = 0;
    buf->type = BUF_VIDEO_WC3;
    this->video_fifo->put (this->video_fifo, buf);

    if (this->audio_fifo) {
      this->wave.wFormatTag = 1;
      this->wave.nChannels = 1;
      this->wave.nSamplesPerSec = 22050;
      this->wave.wBitsPerSample = 16;
      this->wave.nBlockAlign = (this->wave.wBitsPerSample / 8) * this->wave.nChannels;
      this->wave.nAvgBytesPerSec = this->wave.nBlockAlign * this->wave.nSamplesPerSec;

      buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
      buf->type = BUF_AUDIO_LPCM_LE;
      buf->decoder_flags = BUF_FLAG_HEADER;
      buf->decoder_info[0] = 0;
      buf->decoder_info[1] = this->wave.nSamplesPerSec;
      buf->decoder_info[2] = this->wave.wBitsPerSample;
      buf->decoder_info[3] = this->wave.nChannels;
      buf->content = (void *)&this->wave;
      buf->size = sizeof(this->wave);
      this->audio_fifo->put (this->audio_fifo, buf);
    }

    this->status = DEMUX_OK;
    this->send_end_buffers = 1;
    this->thread_running = 1;

    if ((err = pthread_create (&this->thread, NULL, demux_mve_loop, this)) != 0) {
      printf ("demux_wc3movie: can't create new thread (%s)\n", strerror(err));
      abort();
    }
  }

  pthread_mutex_unlock(&this->mutex);

  return DEMUX_OK;
}

static int demux_mve_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  /* MVE files are not meant to be seekable; don't even bother */

  return 0;
}

static void demux_mve_stop (demux_plugin_t *this_gen) {

  demux_mve_t *this = (demux_mve_t *) this_gen;
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

  xine_demux_flush_engine(this->xine);

  xine_demux_control_end(this->xine, BUF_FLAG_END_USER);
}

static void demux_mve_close (demux_plugin_t *this_gen) {
  demux_mve_t *this = (demux_mve_t *) this_gen;

  free(this->palettes);
  free(this);
}

static int demux_mve_get_status (demux_plugin_t *this_gen) {
  demux_mve_t *this = (demux_mve_t *) this_gen;

  return this->status;
}

static char *demux_mve_get_id(void) {
  return "WC3 MOVIE";
}

static int demux_mve_get_stream_length (demux_plugin_t *this_gen) {

  return 0;
}

static char *demux_mve_get_mimetypes(void) {
  return NULL;
}


demux_plugin_t *init_demuxer_plugin(int iface, xine_t *xine) {
  demux_mve_t *this;

  if (iface != 10) {
    printf (_("demux_wc3movie: plugin doesn't support plugin API version %d.\n"
              "                this means there's a version mismatch between xine and this "
              "                demuxer plugin.\nInstalling current demux plugins should help.\n"),
            iface);
    return NULL;
  }

  this         = (demux_mve_t *) xine_xmalloc(sizeof(demux_mve_t));
  this->config = xine->config;
  this->xine   = xine;

  (void *) this->config->register_string(this->config,
                                         "mrl.ends_mve", VALID_ENDS,
                                         _("valid mrls ending for mve demuxer"),                                         NULL, NULL, NULL);

  this->demux_plugin.interface_version = DEMUXER_PLUGIN_IFACE_VERSION;
  this->demux_plugin.open              = demux_mve_open;
  this->demux_plugin.start             = demux_mve_start;
  this->demux_plugin.seek              = demux_mve_seek;
  this->demux_plugin.stop              = demux_mve_stop;
  this->demux_plugin.close             = demux_mve_close;
  this->demux_plugin.get_status        = demux_mve_get_status;
  this->demux_plugin.get_identifier    = demux_mve_get_id;
  this->demux_plugin.get_stream_length = demux_mve_get_stream_length;
  this->demux_plugin.get_mimetypes     = demux_mve_get_mimetypes;

  this->status = DEMUX_FINISHED;
  pthread_mutex_init(&this->mutex, NULL);

  return &this->demux_plugin;
}
