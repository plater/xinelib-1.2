/*
 * Copyright (C) 2000-2001 the xine project
 * 
 * Copyright (C) James Courtier-Dutton James@superbug.demon.co.uk - July 2001
 *
 * This file is part of xine, a unix video player.
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
 * $Id: xine_decoder.c,v 1.85 2002/11/15 00:20:33 miguelfreitas Exp $
 *
 * stuff needed to turn libspu into a xine decoder plugin
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "xine_internal.h"
#include "buffer.h"
#include "video_out/alphablend.h" /* For clut_t */
#include "xine-engine/bswap.h"
#include "xineutils.h"
#include "spu.h"
#include "nav_types.h"
#include "nav_read.h"
#include "nav_print.h"

/*
#define LOG_DEBUG 1
#define LOG_BUTTON 1
*/

static clut_t __default_clut[] = {
  CLUT_Y_CR_CB_INIT(0x00, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x10, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0x51, 0xef, 0x5a),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x36, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x51, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x10, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef),
  CLUT_Y_CR_CB_INIT(0x5c, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0xbf, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x1c, 0x80, 0x80),
  CLUT_Y_CR_CB_INIT(0x28, 0x6d, 0xef)
};

static void spudec_decode_data (spu_decoder_t *this_gen, buf_element_t *buf) {
  uint32_t stream_id;
  spudec_seq_t       *cur_seq;
  spudec_decoder_t *this = (spudec_decoder_t *) this_gen;
  stream_id = buf->type & 0x1f ;
  cur_seq = &this->spudec_stream_state[stream_id].ra_seq;

#ifdef LOG_DEBUG
  printf("libspudec:got buffer type = %x\n", buf->type);
#endif
  if ( (buf->type & 0xffff0000) != BUF_SPU_DVD ||
       !(buf->decoder_flags & BUF_FLAG_SPECIAL) || 
       buf->decoder_info[1] != BUF_SPECIAL_SPU_DVD_SUBTYPE )
    return;

  if ( buf->decoder_info[2] == SPU_DVD_SUBTYPE_CLUT ) {
    printf("libspudec: SPU CLUT\n");
    if (buf->content[0]) { /* cheap endianess detection */
      xine_fast_memcpy(this->state.clut, buf->content, sizeof(uint32_t)*16);
    } else {
      int i;
      uint32_t *clut = (uint32_t*) buf->content;
      for (i = 0; i < 16; i++)
        this->state.clut[i] = bswap_32(clut[i]);
    }
    this->state.need_clut = 0;
    return;
  }
 
  if ( buf->decoder_info[2] == SPU_DVD_SUBTYPE_SUBP_CONTROL ) {
    /* FIXME: I don't think SUBP_CONTROL is used any more */
    int i;
    uint32_t *subp_control = (uint32_t*) buf->content;
    for (i = 0; i < 32; i++) {
      this->spudec_stream_state[i].stream_filter = subp_control[i]; 
    }
    return;
  }
  if ( buf->decoder_info[2] == SPU_DVD_SUBTYPE_NAV ) {
#ifdef LOG_DEBUG
    printf("libspudec:got nav packet 1\n");
#endif
    spudec_decode_nav(this,buf);
    return;
  }

#ifdef LOG_DEBUG
  printf("libspudec:got buffer type = %x\n", buf->type);
#endif
  if (buf->decoder_flags & BUF_FLAG_PREVIEW)  /* skip preview data */
    return;

  if ( this->spudec_stream_state[stream_id].stream_filter == 0) 
    return;

  if (buf->pts) {
    metronom_t *metronom = this->stream->metronom;
    int64_t vpts = metronom->got_spu_packet(metronom, buf->pts);
    
    this->spudec_stream_state[stream_id].vpts = vpts; /* Show timer */
    this->spudec_stream_state[stream_id].pts = buf->pts; /* Required to match up with NAV packets */
  }

  spudec_reassembly(&this->spudec_stream_state[stream_id].ra_seq,
                     buf->content,
                     buf->size);
  if(this->spudec_stream_state[stream_id].ra_seq.complete == 1) { 
    spudec_process(this,stream_id);
  }
}

static void spudec_reset (spu_decoder_t *this_gen) {
}

static void spudec_event_listener(void *this_gen, xine_event_t *event_gen) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
#if 0
/* FIXME: get events working again. */

  xine_spu_event_t *event = (xine_spu_event_t *) event_gen;

  video_overlay_instance_t *ovl_instance;

  if((!this) || (!event)) {
    return;
  }

  switch (event->event.type) {
  case XINE_EVENT_SPU_BUTTON:
    {
      /* This function will move to video_overlay 
       * when video_overlay does menus */

      video_overlay_event_t *overlay_event = NULL;
      vo_overlay_t        *overlay = NULL;
      spu_button_t        *but = event->data;
      overlay_event = xine_xmalloc (sizeof(video_overlay_event_t));

      overlay = xine_xmalloc (sizeof(vo_overlay_t));

#ifdef LOG_DEBUG
      printf ("BUTTON\n");
      printf ("\tshow=%d\n",but->show);
      printf ("\tclut [%x %x %x %x]\n",
	   but->color[0], but->color[1], but->color[2], but->color[3]);
      printf ("\ttrans [%d %d %d %d]\n",
	   but->trans[0], but->trans[1], but->trans[2], but->trans[3]);
      printf ("\tleft = %u right = %u top = %u bottom = %u\n",
	   but->left, but->right, but->top, but->bottom );
      printf ("\tpts = %lli\n",
	   but->pts );
#endif
      /* FIXME: Watch out for threads. We should really put a lock on this 
       * because events is a different thread than decode_data */
      //if (!this->state.forced_display) return;

#ifdef LOG_DEBUG
      printf ("libspudec:xine_decoder.c:spudec_event_listener:this->menu_handle=%u\n",this->menu_handle);
#endif
      
      if (but->show > 0) {
#ifdef LOG_NAV
        fprintf (stderr,"libspudec:xine_decoder.c:spudec_event_listener:buttonN = %u show=%d\n",
          but->buttonN,
          but->show);
#endif
        this->buttonN = but->buttonN;
        if (this->button_filter != 1) {
#ifdef LOG_NAV
          fprintf (stdout,"libspudec:xine_decoder.c:spudec_event_listener:buttonN updates not allowed\n");
#endif
          /* Only update highlight is the menu will let us */
          free(overlay_event);
          free(overlay);
          break;
        }
        if (but->show == 2) {
          this->button_filter = 2;
        }
        pthread_mutex_lock(&this->nav_pci_lock);
        overlay_event->object.handle = this->menu_handle;
        overlay_event->object.pts = this->pci.hli.hl_gi.hli_s_ptm;
        overlay_event->object.overlay=overlay;
        overlay_event->event_type = EVENT_MENU_BUTTON;
#ifdef LOG_NAV
        fprintf(stderr, "libspudec:Button Overlay\n");
#endif
        spudec_copy_nav_to_overlay(&this->pci, this->state.clut, this->buttonN, but->show-1,
	                           overlay, &this->overlay );
        pthread_mutex_unlock(&this->nav_pci_lock);
      } else {
        fprintf (stderr,"libspudec:xine_decoder.c:spudec_event_listener:HIDE ????\n");
        assert(0);
        overlay_event->object.handle = this->menu_handle;
        overlay_event->event_type = EVENT_HIDE_MENU;
      }
      overlay_event->vpts = 0; 
      if (this->vo_out) {
        ovl_instance = this->vo_out->get_overlay_instance (this->vo_out);
#ifdef LOG_NAV
        fprintf(stderr, "libspudec: add_event type=%d : current time=%lld, spu vpts=%lli\n",
          overlay_event->event_type,
          this->stream->metronom->get_current_time(this->stream->metronom),
          overlay_event->vpts);
#endif
        ovl_instance->add_event (ovl_instance, (void *)overlay_event);
        free(overlay_event);
        free(overlay);
      } else {
        free(overlay_event);
        free(overlay);
      }
    }
    break;
  case XINE_EVENT_SPU_CLUT:
    {
    /* FIXME: This function will need checking before it works. */
      spudec_clut_table_t *clut = event->data;
      if (clut) {
        xine_fast_memcpy(this->state.clut, clut->clut, sizeof(uint32_t)*16);
        this->state.need_clut = 0;
      }
    }
    break;
  }
#endif
}

static void spudec_dispose (spu_decoder_t *this_gen) {

  spudec_decoder_t         *this = (spudec_decoder_t *) this_gen;
  int                       i;
  video_overlay_instance_t *ovl_instance = this->vo_out->get_overlay_instance (this->vo_out);
  
  if( this->menu_handle >= 0 )
    ovl_instance->free_handle(ovl_instance,
			      this->menu_handle);
  this->menu_handle = -1;


  for (i=0; i < MAX_STREAMS; i++) {
    if( this->spudec_stream_state[i].overlay_handle >= 0 )
      ovl_instance->free_handle(ovl_instance,
				this->spudec_stream_state[i].overlay_handle);
    this->spudec_stream_state[i].overlay_handle = -1;
  }
  pthread_mutex_destroy(&this->nav_pci_lock);
  /* FIXME: get events working. */
  /*xine_remove_event_listener (this->stream, spudec_event_listener);*/

  free (this->event.object.overlay);
  free (this);
}

/* gets the current already correctly processed nav_pci info */
/* This is not perfectly in sync with the display, but all the same, */
/* much closer than doing it at the input stage. */
/* returns a bool for error/success.*/
static int spudec_get_nav_pci (spu_decoder_t *this_gen, pci_t *pci) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  /*printf("get_nav_pci() called\n");*/
  if (!this || !pci) 
    return 0;
 
  /*printf("get_nav_pci() coping nav_pci\n");*/
  pthread_mutex_lock(&this->nav_pci_lock);
  memcpy(pci, &this->pci, sizeof(pci_t) );
  pthread_mutex_unlock(&this->nav_pci_lock);
  return 1;

}

static void spudec_set_button (spu_decoder_t *this_gen, int32_t button, int32_t show) {
  spudec_decoder_t *this  = (spudec_decoder_t *) this_gen;
  /* This function will move to video_overlay
  * when video_overlay does menus */

  video_overlay_instance_t *ovl_instance;
  video_overlay_event_t *overlay_event = NULL;
  vo_overlay_t        *overlay = NULL;
  overlay_event = xine_xmalloc (sizeof(video_overlay_event_t));

  overlay = xine_xmalloc (sizeof(vo_overlay_t));
  /* FIXME: Watch out for threads. We should really put a lock on this
   * because events is a different thread than decode_data */

#ifdef LOG_DEBUG
  printf ("libspudec:xine_decoder.c:spudec_event_listener:this->menu_handle=%u\n",this->menu_handle);
#endif

  if (show > 0) {
#ifdef LOG_NAV
    fprintf (stderr,"libspudec:xine_decoder.c:spudec_event_listener:buttonN = %u show=%d\n",
             button,
             show);
#endif
    this->buttonN = button;
    if (this->button_filter != 1) {
#ifdef LOG_NAV
      fprintf (stdout,"libspudec:xine_decoder.c:spudec_event_listener:buttonN updates not allowed\n");
#endif
      /* Only update highlight is the menu will let us */
      free(overlay_event);
      free(overlay);
      return;
    }
    if (show == 2) {
      this->button_filter = 2;
    }
    pthread_mutex_lock(&this->nav_pci_lock);
    overlay_event->object.handle = this->menu_handle;
    overlay_event->object.pts = this->pci.hli.hl_gi.hli_s_ptm;
    overlay_event->object.overlay=overlay;
    overlay_event->event_type = EVENT_MENU_BUTTON;
#ifdef LOG_NAV
    fprintf(stderr, "libspudec:Button Overlay\n");
#endif
    spudec_copy_nav_to_overlay(&this->pci, this->state.clut, this->buttonN, show-1,
                               overlay, &this->overlay );
    pthread_mutex_unlock(&this->nav_pci_lock);
  } else {
  fprintf (stderr,"libspudec:xine_decoder.c:spudec_event_listener:HIDE ????\n");
  assert(0);
  overlay_event->object.handle = this->menu_handle;
  overlay_event->event_type = EVENT_HIDE_MENU;
  }
  overlay_event->vpts = 0;
  if (this->vo_out) {
    ovl_instance = this->vo_out->get_overlay_instance (this->vo_out);
#ifdef LOG_NAV
    fprintf(stderr, "libspudec: add_event type=%d : current time=%lld, spu vpts=%lli\n",
            overlay_event->event_type,
            this->stream->metronom->get_current_time(this->stream->metronom),
            overlay_event->vpts);
#endif
    ovl_instance->add_event (ovl_instance, (void *)overlay_event);
    free(overlay_event);
    free(overlay);
  } else {
    free(overlay_event);
    free(overlay);
  }
  return;
}

static spu_decoder_t *open_plugin (spu_decoder_class_t *class_gen, xine_stream_t *stream) {

  spudec_decoder_t *this ;
  int i;

  this = (spudec_decoder_t *) xine_xmalloc (sizeof (spudec_decoder_t));

  this->spu_decoder.decode_data         = spudec_decode_data;
  this->spu_decoder.reset               = spudec_reset;
  this->spu_decoder.dispose             = spudec_dispose;
  this->spu_decoder.get_nav_pci         = spudec_get_nav_pci;
  this->spu_decoder.set_button          = spudec_set_button;
  this->stream                          = stream;
  this->class                           = (spudec_class_t *) class_gen;

  this->menu_handle = -1;
  this->buttonN = 1;
  this->event.object.overlay = malloc(sizeof(vo_overlay_t));
 
/* FIXME: get events working again. */
  /*xine_register_event_listener(xine, spudec_event_listener, this);*/

  pthread_mutex_init(&this->nav_pci_lock, NULL);

  this->vo_out      = stream->video_out;
  this->ovl_caps    = stream->video_out->get_capabilities(stream->video_out);
  this->output_open = 0;
  this->last_event_vpts = 0;
  for (i=0; i < MAX_STREAMS; i++) {
    this->spudec_stream_state[i].stream_filter = 1; /* So it works with non-navdvd plugins */
    this->spudec_stream_state[i].ra_seq.complete = 1;
    this->spudec_stream_state[i].overlay_handle = -1;
  }

/* FIXME:Do we really need a default clut? */
  xine_fast_memcpy(this->state.clut, __default_clut, sizeof(this->state.clut));
  this->state.need_clut = 1;

  return &this->spu_decoder;
}

static char *get_identifier (spu_decoder_class_t *this) {
  printf ("libspudec:get_identifier called\n");
  return "spudec";
}

static char *get_description (spu_decoder_class_t *this) {
  printf ("libspudec:get_description called\n");
  return "DVD/VOB SPU decoder plugin";
}

static void dispose_class (spu_decoder_class_t *this) {
  printf ("libspudec:dispose_class called\n");
  free (this);
}


static void *init_plugin (xine_t *xine, void *data) {

  spudec_class_t *this;

  this = (spudec_class_t *) malloc (sizeof (spudec_class_t));

  this->decoder_class.open_plugin     = open_plugin;
  this->decoder_class.get_identifier  = get_identifier;
  this->decoder_class.get_description = get_description;
  this->decoder_class.dispose         = dispose_class;
/* FIXME: get config stuff working */

/*  this->config = xine->config; */

  printf ("libspudec:init_plugin called\n");
  return this;
}

/* plugin catalog information */
static uint32_t supported_types[] = { BUF_SPU_DVD, 0 };

static decoder_info_t dec_info_data = {
  supported_types,     /* supported types */
  5                    /* priority        */
};

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_SPU_DECODER, 10, "spudec", XINE_VERSION_CODE, &dec_info_data, &init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
