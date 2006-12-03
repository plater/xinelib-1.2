/*
 * Copyright (C) 2000-2005 the xine project
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
 * Demuxer helper functions
 * hide some xine engine details from demuxers and reduce code duplication
 *
 * $Id: demux.c,v 1.65 2006/12/03 19:23:16 miguelfreitas Exp $ 
 */


#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define XINE_ENGINE_INTERNAL

#define LOG_MODULE "demux"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xine_internal.h"
#include "demuxers/demux.h"
#include "buffer.h"

#ifdef WIN32
#include <winsock.h>
#endif

#ifdef MIN
#undef MIN
#endif
#define MIN(a,b) ( (a) < (b) ) ? (a) : (b)

/* 
 *  Flush audio and video buffers. It is called from demuxers on
 *  seek/stop, and may be useful when user input changes a stream and
 *  xine-lib has cached buffers that have yet to be played.
 *
 * warning: after clearing decoders fifos an absolute discontinuity
 *          indication must be sent. relative discontinuities are likely
 *          to cause "jumps" on metronom.
 */
void _x_demux_flush_engine (xine_stream_t *stream) {

  buf_element_t *buf;
  
  if( stream->gapless_switch )
    return;
  
  stream->xine->port_ticket->acquire(stream->xine->port_ticket, 1);

  /* only flush/discard output ports on master streams */
  if( stream->master == stream ) {
    if (stream->video_out) {
      stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 1);
    }
    if (stream->audio_out) {
      stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 1);
    }
  }
    
  stream->video_fifo->clear(stream->video_fifo);
  stream->audio_fifo->clear(stream->audio_fifo);
  
  pthread_mutex_lock(&stream->demux_mutex);  

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_RESET_DECODER;
  stream->video_fifo->put (stream->video_fifo, buf);
  
  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_RESET_DECODER;
  stream->audio_fifo->put (stream->audio_fifo, buf);
  
  pthread_mutex_unlock(&stream->demux_mutex);  

  /* on seeking we must wait decoder fifos to process before doing flush. 
   * otherwise we flush too early (before the old data has left decoders)
   */
  _x_demux_control_headers_done (stream);

  if (stream->video_out) {
    video_overlay_manager_t *ovl = stream->video_out->get_overlay_manager(stream->video_out);
    ovl->flush_events(ovl);
  }
  
  /* only flush/discard output ports on master streams */
  if( stream->master == stream ) {
    if (stream->video_out) {
      stream->video_out->flush(stream->video_out);
      stream->video_out->set_property(stream->video_out, VO_PROP_DISCARD_FRAMES, 0);
    }

    if (stream->audio_out) {
      stream->audio_out->flush(stream->audio_out);
      stream->audio_out->set_property(stream->audio_out, AO_PROP_DISCARD_BUFFERS, 0);
    }
  }
  
  stream->xine->port_ticket->release(stream->xine->port_ticket, 1);
}


void _x_demux_control_newpts( xine_stream_t *stream, int64_t pts, uint32_t flags ) {

  buf_element_t *buf;
      
  pthread_mutex_lock(&stream->demux_mutex);  

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_NEWPTS;
  buf->decoder_flags = flags;
  buf->disc_off = pts;
  stream->video_fifo->put (stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_NEWPTS;
  buf->decoder_flags = flags;
  buf->disc_off = pts;
  stream->audio_fifo->put (stream->audio_fifo, buf);

  pthread_mutex_unlock(&stream->demux_mutex);  
}

/* sync with decoder fifos, making sure everything gets processed */
void _x_demux_control_headers_done (xine_stream_t *stream) {

  int header_count_audio;
  int header_count_video;
  buf_element_t *buf_video, *buf_audio;

  /* we use demux_action_pending to wake up sleeping spu decoders */
  stream->demux_action_pending = 1;
  
  /* allocate the buffers before grabbing the lock to prevent cyclic wait situations */
  buf_video = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf_audio = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);

  pthread_mutex_lock (&stream->counter_lock);

  if (stream->video_thread_created) {
    header_count_video = stream->header_count_video + 1;
  } else {
    header_count_video = 0;
  }

  if (stream->audio_thread_created) {
    header_count_audio = stream->header_count_audio + 1;
  } else {
    header_count_audio = 0;
  }
  
  pthread_mutex_lock(&stream->demux_mutex);  

  buf_video->type = BUF_CONTROL_HEADERS_DONE;
  stream->video_fifo->put (stream->video_fifo, buf_video);

  buf_audio->type = BUF_CONTROL_HEADERS_DONE;
  stream->audio_fifo->put (stream->audio_fifo, buf_audio);

  pthread_mutex_unlock(&stream->demux_mutex);  

  while ((stream->header_count_audio < header_count_audio) || 
         (stream->header_count_video < header_count_video)) {
    struct timeval tv;
    struct timespec ts;

    lprintf ("waiting for headers. v:%d %d   a:%d %d\n",
	     stream->header_count_video, header_count_video,
	     stream->header_count_audio, header_count_audio); 

    gettimeofday(&tv, NULL);
    ts.tv_sec  = tv.tv_sec + 1;
    ts.tv_nsec = tv.tv_usec * 1000;
    /* use timedwait to workaround buggy pthread broadcast implementations */
    pthread_cond_timedwait (&stream->counter_changed, &stream->counter_lock, &ts);
  }

  stream->demux_action_pending = 0;
  
  lprintf ("headers processed.\n");

  pthread_mutex_unlock (&stream->counter_lock);
}

void _x_demux_control_start( xine_stream_t *stream ) {

  buf_element_t *buf;

  pthread_mutex_lock(&stream->demux_mutex);  

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_START;
  stream->video_fifo->put (stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_START;
  stream->audio_fifo->put (stream->audio_fifo, buf);

  pthread_mutex_unlock(&stream->demux_mutex);  
}

void _x_demux_control_end( xine_stream_t *stream, uint32_t flags ) {

  buf_element_t *buf;

  pthread_mutex_lock(&stream->demux_mutex);  

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_END;
  buf->decoder_flags = flags;
  stream->video_fifo->put (stream->video_fifo, buf);

  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_END;
  buf->decoder_flags = flags;
  stream->audio_fifo->put (stream->audio_fifo, buf);

  pthread_mutex_unlock(&stream->demux_mutex);  
}

void _x_demux_control_nop( xine_stream_t *stream, uint32_t flags ) {

  buf_element_t *buf;

  pthread_mutex_lock(&stream->demux_mutex);  

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
  buf->type = BUF_CONTROL_NOP;
  buf->decoder_flags = flags;
  stream->video_fifo->put (stream->video_fifo, buf);
  
  buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
  buf->type = BUF_CONTROL_NOP;
  buf->decoder_flags = flags;
  stream->audio_fifo->put (stream->audio_fifo, buf);

  pthread_mutex_unlock(&stream->demux_mutex);  
}

static void *demux_loop (void *stream_gen) {

  xine_stream_t *stream = (xine_stream_t *)stream_gen;
  int status;
  int finished_count_audio = 0;
  int finished_count_video = 0;
  int non_user;

  lprintf ("loop starting...\n");

  pthread_mutex_lock( &stream->demux_lock );
  stream->emergency_brake = 0;
  
  /* do-while needed to seek after demux finished */
  do {

    /* main demuxer loop */
    status = stream->demux_plugin->get_status(stream->demux_plugin);
    while(status == DEMUX_OK && stream->demux_thread_running &&
          !stream->emergency_brake) {

      status = stream->demux_plugin->send_chunk(stream->demux_plugin);

      /* someone may want to interrupt us */
      if( stream->demux_action_pending ) {
        pthread_mutex_unlock( &stream->demux_lock );

        lprintf ("sched_yield\n");

        sched_yield();
        pthread_mutex_lock( &stream->demux_lock );
      }
    }

    lprintf ("main demuxer loop finished (status: %d)\n", status);

    /* tell to the net_buf_ctrl that we are at the end of the stream
     * then the net_buf_ctrl will not pause
     */
    _x_demux_control_nop(stream, BUF_FLAG_END_STREAM);

    /* wait before sending end buffers: user might want to do a new seek */
    while(stream->demux_thread_running &&
          ((stream->video_fifo->size(stream->video_fifo)) ||
           (stream->audio_fifo->size(stream->audio_fifo))) &&
          status == DEMUX_FINISHED && !stream->emergency_brake){
      pthread_mutex_unlock( &stream->demux_lock );
      xine_usec_sleep(100000);
      pthread_mutex_lock( &stream->demux_lock );
      status = stream->demux_plugin->get_status(stream->demux_plugin);
    }

    /* delay sending finished event - used for image presentations */
    while(stream->demux_thread_running &&
          status == DEMUX_FINISHED && stream->delay_finish_event != 0){
      pthread_mutex_unlock( &stream->demux_lock );
      xine_usec_sleep(100000);
      if( stream->delay_finish_event > 0 )
        stream->delay_finish_event--;
      pthread_mutex_lock( &stream->demux_lock );
      status = stream->demux_plugin->get_status(stream->demux_plugin);
    }

  } while( status == DEMUX_OK && stream->demux_thread_running &&
           !stream->emergency_brake);

  lprintf ("loop finished (status: %d)\n", status);

  pthread_mutex_lock (&stream->counter_lock);
  if (stream->audio_thread_created)
    finished_count_audio = stream->finished_count_audio + 1;
  if (stream->video_thread_created)
    finished_count_video = stream->finished_count_video + 1;
  pthread_mutex_unlock (&stream->counter_lock);

  /* demux_thread_running is zero if demux loop has been stopped by user */
  non_user = stream->demux_thread_running;
  stream->demux_thread_running = 0;
  
  _x_demux_control_end(stream, non_user);

  lprintf ("loop finished, end buffer sent\n");

  pthread_mutex_unlock( &stream->demux_lock );

  pthread_mutex_lock (&stream->counter_lock);
  while ((stream->finished_count_audio < finished_count_audio) || 
         (stream->finished_count_video < finished_count_video)) {
    lprintf ("waiting for finisheds.\n");
    pthread_cond_wait (&stream->counter_changed, &stream->counter_lock);
  }
  pthread_mutex_unlock (&stream->counter_lock);
  
  _x_handle_stream_end(stream, non_user);
  return NULL;
}

int _x_demux_start_thread (xine_stream_t *stream) {

  int err;
  
  lprintf ("start thread called\n");
  
  stream->demux_action_pending = 1;
  pthread_mutex_lock( &stream->demux_lock );
  stream->demux_action_pending = 0;
  
  if( !stream->demux_thread_running ) {

    if (stream->demux_thread_created) {
      void *p;
      pthread_join(stream->demux_thread, &p);
    }

    stream->demux_thread_running = 1;
    stream->demux_thread_created = 1;
    if ((err = pthread_create (&stream->demux_thread,
			       NULL, demux_loop, (void *)stream)) != 0) {
      printf ("demux: can't create new thread (%s)\n", strerror(err));
      _x_abort();
    }
  }
  
  pthread_mutex_unlock( &stream->demux_lock );
  return 0;
}

int _x_demux_stop_thread (xine_stream_t *stream) {
  
  void *p;
  
  lprintf ("stop thread called\n");
  
  stream->demux_action_pending = 1;
  pthread_mutex_lock( &stream->demux_lock );
  stream->demux_thread_running = 0;
  stream->demux_action_pending = 0;

  /* At that point, the demuxer has sent the last audio/video buffer,
   * so it's a safe place to flush the engine.
   */
  _x_demux_flush_engine( stream );
  pthread_mutex_unlock( &stream->demux_lock );

  lprintf ("joining thread %ld\n", stream->demux_thread );
  
  if( stream->demux_thread_created ) {
    pthread_join (stream->demux_thread, &p);
    stream->demux_thread_created = 0;
  }
  
  /*
   * Wake up xine_play if it's waiting for a frame
   */
  pthread_mutex_lock (&stream->first_frame_lock);
  if (stream->first_frame_flag) {
    stream->first_frame_flag = 0;
    pthread_cond_broadcast(&stream->first_frame_reached);
  }
  pthread_mutex_unlock (&stream->first_frame_lock);

  return 0;
}

int _x_demux_read_header( input_plugin_t *input, unsigned char *buffer, off_t size){
  int read_size;
  unsigned char *buf;

  if (!input || !size || size > MAX_PREVIEW_SIZE)
    return 0;

  if (input->get_capabilities(input) & INPUT_CAP_SEEKABLE) {
    input->seek(input, 0, SEEK_SET);
    read_size = input->read(input, buffer, size);
    input->seek(input, 0, SEEK_SET);
  } else if (input->get_capabilities(input) & INPUT_CAP_PREVIEW) {
    buf = xine_xmalloc(MAX_PREVIEW_SIZE);
    read_size = input->get_optional_data(input, buf, INPUT_OPTIONAL_DATA_PREVIEW);
    read_size = MIN (read_size, size);
    memcpy(buffer, buf, read_size);
    free(buf);
  } else {
    return 0;
  }
  return read_size;
}

int _x_demux_check_extension (char *mrl, char *extensions){
  char *last_dot, *e, *ext_copy, *ext_work;

  ext_copy = strdup(extensions);
  ext_work = ext_copy;

  last_dot = strrchr (mrl, '.');
  if (last_dot) {
    last_dot++;
    while ( ( e = xine_strsep(&ext_work, " ")) != NULL ) {
      if (strcasecmp (last_dot, e) == 0) {
        free(ext_copy);
        return 1;
      }
    }
  }
  free(ext_copy);
  return 0;
}


/*
 * read from socket/file descriptor checking demux_action_pending
 *
 * network input plugins should use this function in order to
 * not freeze the engine.
 *
 * aborts with zero if no data is available and demux_action_pending is set
 */
off_t _x_read_abort (xine_stream_t *stream, int fd, char *buf, off_t todo) {

  off_t ret, total;

  total = 0;

  while (total < todo) {

    fd_set rset;
    struct timeval timeout;

    while(1) {

      FD_ZERO (&rset);
      FD_SET  (fd, &rset);

      timeout.tv_sec  = 0;
      timeout.tv_usec = 50000;

      if( select (fd+1, &rset, NULL, NULL, &timeout) <= 0 ) {
        /* aborts current read if action pending. otherwise xine
         * cannot be stopped when no more data is available.
         */
        if( stream->demux_action_pending )
          return total;
      } else {
        break;
      }
    }

#ifndef WIN32
    ret = read (fd, &buf[total], todo - total);

    /* check EOF */
    if (!ret)
      break;

    /* check errors */
    if(ret < 0) {
      if(errno == EAGAIN)
        continue;

      perror("_x_read_abort");
      return ret;
    }
#else
    ret = recv (fd, &buf[total], todo - total, 0);
    if (ret <= 0)
	{
      perror("_x_read_abort");
	  return ret;
	}
#endif

    total += ret;
  }

  return total;
}

int _x_action_pending (xine_stream_t *stream) {
  return stream->demux_action_pending;
}

/*
 * demuxer helper function to send data to fifo, breaking into smaller
 * pieces (bufs) as needed.
 *
 * it has quite some parameters, but only the first 6 are needed.
 *
 * the other ones help enforcing that demuxers provide the information
 * they have about the stream, so things like the GUI slider bar can
 * work as expected. 
 */
void _x_demux_send_data(fifo_buffer_t *fifo, uint8_t *data, int size,
                        int64_t pts, uint32_t type, uint32_t decoder_flags,
                        int input_normpos,
                        int input_time, int total_time,
                        uint32_t frame_number) {
  buf_element_t *buf;

  decoder_flags |= BUF_FLAG_FRAME_START;

  _x_assert(size > 0);
  while (fifo && size > 0) {

    buf = fifo->buffer_pool_alloc (fifo);

    if ( size > buf->max_size ) {
      buf->size          = buf->max_size;
      buf->decoder_flags = decoder_flags;
    } else {
      buf->size          = size;
      buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
    }
    decoder_flags &= ~BUF_FLAG_FRAME_START;

    xine_fast_memcpy (buf->content, data, buf->size);
    data += buf->size;
    size -= buf->size;

    buf->pts = pts;
    pts = 0;

    buf->extra_info->input_normpos = input_normpos;
    buf->extra_info->input_time    = input_time;
    buf->extra_info->total_time    = total_time;
    buf->extra_info->frame_number  = frame_number;

    buf->type                      = type;

    fifo->put (fifo, buf);
  }
}

/*
 * Analogous to above, but reads data from input plugin
 *
 * If reading fails, -1 is returned
 */
int _x_demux_read_send_data(fifo_buffer_t *fifo, input_plugin_t *input, 
                            int size, int64_t pts, uint32_t type, 
                            uint32_t decoder_flags, off_t input_normpos, 
                            int input_time, int total_time, 
                            uint32_t frame_number) {
  buf_element_t *buf;

  decoder_flags |= BUF_FLAG_FRAME_START;

  _x_assert(size > 0);
  while (fifo && size > 0) {

    buf = fifo->buffer_pool_alloc (fifo);

    if ( size > buf->max_size ) {
      buf->size          = buf->max_size;
      buf->decoder_flags = decoder_flags;
    } else {
      buf->size          = size;
      buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
    }
    decoder_flags &= ~BUF_FLAG_FRAME_START;

    if(input->read(input, buf->content, buf->size) < buf->size) {
      buf->free_buffer(buf);
      return -1;    
    }
    size -= buf->size;

    buf->pts = pts;
    pts = 0;

    buf->extra_info->input_normpos = input_normpos;
    buf->extra_info->input_time    = input_time;
    buf->extra_info->total_time    = total_time;
    buf->extra_info->frame_number  = frame_number;

    buf->type                      = type;

    fifo->put (fifo, buf);
  }
  
  return 0;
}

/*
 * Helper function for sending MRL reference events
 */
void _x_demux_send_mrl_reference (xine_stream_t *stream, int alternative,
				  const char *mrl, const char *title,
				  int start_time, int duration)
{
  xine_event_t event;
  union {
    xine_mrl_reference_data_ext_t *e;
    xine_mrl_reference_data_t *b;
  } data;
  int mrl_len = strlen (mrl);

  if (!title)
    title = "";

  /* extended MRL reference event */

  event.stream = stream;
  event.data_length = offsetof (xine_mrl_reference_data_ext_t, mrl) +
                      mrl_len + strlen (title) + 2;
  data.e = event.data = malloc (event.data_length);
    
  data.e->alternative = alternative;
  data.e->start_time = start_time;
  data.e->duration = duration;
  strcpy((char *)data.e->mrl, mrl);
  strcpy((char *)data.e->mrl + mrl_len + 1, title ? title : "");

  event.type = XINE_EVENT_MRL_REFERENCE_EXT;
  xine_event_send (stream, &event);

  /* plain MRL reference event */

  event.data_length = offsetof (xine_mrl_reference_data_t, mrl) + mrl_len + 1;

  /*data.b->alternative = alternative;*/
  strcpy (data.b->mrl, mrl);

  event.type = XINE_EVENT_MRL_REFERENCE;
  xine_event_send (stream, &event);

  free (data.e);
}
