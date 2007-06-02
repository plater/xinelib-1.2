/*
 * Copyright (C) 2007 the xine project
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
 * $Id: combined_wavpack.c,v 1.3 2007/03/17 07:34:02 dgp85 Exp $
 */

#include "xine_internal.h"

void *decoder_nsf_init_plugin (xine_t *xine, void *data);
void *demux_nsf_init_plugin (xine_t *xine, void *data);

static const demuxer_info_t demux_info_nsf = {
  10                       /* priority */
};

static uint32_t audio_types[] = { 
  BUF_AUDIO_NSF,
  0
};

static const decoder_info_t decoder_info_nsf = {
  audio_types,         /* supported types */
  5                    /* priority        */
};

const plugin_info_t xine_plugin_info[] EXPORTED = {
  { PLUGIN_DEMUX, 26, "nsfdemux", XINE_VERSION_CODE, &demux_info_nsf, demux_nsf_init_plugin },
  { PLUGIN_AUDIO_DECODER, 15, "nsfdec", XINE_VERSION_CODE, &decoder_info_nsf, decoder_nsf_init_plugin },
  { PLUGIN_NONE, 0, NULL, 0, NULL, NULL }
};
