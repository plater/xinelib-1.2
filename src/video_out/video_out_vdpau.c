/*
 * Copyright (C) 2008 the xine project
 * Copyright (C) 2008 Christophe Thommeret <hftom@free.fr>
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
 *
 * video_out_vdpau.c, a video output plugin
 * using VDPAU (Video Decode and Presentation Api for Unix)
 *
 *
 */

/* #define LOG */
#define LOG_MODULE "video_out_vdpau"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>

#include <xine.h>
#include <xine/video_out.h>
#include <xine/vo_scale.h>
#include <xine/xine_internal.h>
#include <xine/xineutils.h>
#include "yuv2rgb.h"

#include <vdpau/vdpau_x11.h>
#include "accel_vdpau.h"

#ifdef HAVE_FFMPEG_AVUTIL_H
#  include <mem.h>
#else
#  include <libavutil/mem.h>
#endif

#define NUM_FRAMES_BACK 1

#define LOCKDISPLAY /*define this if you have a buggy libX11/xcb*/


#define DEINT_BOB                    1
#define DEINT_HALF_TEMPORAL          2
#define DEINT_HALF_TEMPORAL_SPATIAL  3
#define DEINT_TEMPORAL               4
#define DEINT_TEMPORAL_SPATIAL       5

#define NUMBER_OF_DEINTERLACERS 5

char *vdpau_deinterlacer_name[] = {
  "bob",
  "half temporal",
  "half temporal_spatial",
  "temporal",
  "temporal_spatial",
  NULL
};

char* vdpau_deinterlacer_description [] = {
  "bob\nBasic deinterlacing, doing 50i->50p.\n\n",
  "half temporal\nDisplays first field only, doing 50i->25p\n\n",
  "half temporal_spatial\nDisplays first field only, doing 50i->25p\n\n",
  "temporal\nVery good, 50i->50p\n\n",
  "temporal_spatial\nThe best, but very GPU intensive.\n\n",
  NULL
};


VdpOutputSurfaceRenderBlendState blend = {
  VDP_OUTPUT_SURFACE_RENDER_BLEND_STATE_VERSION,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD,
  VDP_OUTPUT_SURFACE_RENDER_BLEND_EQUATION_ADD,
  0
};



VdpDevice vdp_device;
VdpPresentationQueue vdp_queue;
VdpPresentationQueueTarget vdp_queue_target;

VdpDeviceDestroy *vdp_device_destroy;

VdpGetProcAddress *vdp_get_proc_address;

VdpGetApiVersion *vdp_get_api_version;
VdpGetInformationString *vdp_get_information_string;
VdpGetErrorString *vdp_get_error_string;

VdpVideoSurfaceQueryGetPutBitsYCbCrCapabilities *vdp_video_surface_query_get_put_bits_ycbcr_capabilities;
VdpVideoSurfaceCreate *vdp_video_surface_create;
VdpVideoSurfaceDestroy *vdp_video_surface_destroy;
VdpVideoSurfacePutBitsYCbCr *vdp_video_surface_putbits_ycbcr;
VdpVideoSurfaceGetBitsYCbCr *vdp_video_surface_getbits_ycbcr;

VdpOutputSurfaceCreate *vdp_output_surface_create;
VdpOutputSurfaceDestroy *vdp_output_surface_destroy;
VdpOutputSurfaceRenderBitmapSurface *vdp_output_surface_render_bitmap_surface;
VdpOutputSurfacePutBitsNative *vdp_output_surface_put_bits;

VdpVideoMixerCreate *vdp_video_mixer_create;
VdpVideoMixerDestroy *vdp_video_mixer_destroy;
VdpVideoMixerRender *vdp_video_mixer_render;
VdpVideoMixerSetAttributeValues *vdp_video_mixer_set_attribute_values;
VdpVideoMixerSetFeatureEnables *vdp_video_mixer_set_feature_enables;
VdpVideoMixerGetFeatureEnables *vdp_video_mixer_get_feature_enables;
VdpVideoMixerQueryFeatureSupport *vdp_video_mixer_query_feature_support;
VdpVideoMixerQueryParameterSupport *vdp_video_mixer_query_parameter_support;
VdpVideoMixerQueryAttributeSupport *vdp_video_mixer_query_attribute_support;
VdpVideoMixerQueryParameterValueRange *vdp_video_mixer_query_parameter_value_range;
VdpVideoMixerQueryAttributeValueRange *vdp_video_mixer_query_attribute_value_range;

VdpGenerateCSCMatrix *vdp_generate_csc_matrix;

VdpPresentationQueueTargetCreateX11 *vdp_queue_target_create_x11;
VdpPresentationQueueTargetDestroy *vdp_queue_target_destroy;
VdpPresentationQueueCreate *vdp_queue_create;
VdpPresentationQueueDestroy *vdp_queue_destroy;
VdpPresentationQueueDisplay *vdp_queue_display;
VdpPresentationQueueBlockUntilSurfaceIdle *vdp_queue_block;
VdpPresentationQueueSetBackgroundColor *vdp_queue_set_background_color;
VdpPresentationQueueGetTime *vdp_queue_get_time;
VdpPresentationQueueQuerySurfaceStatus *vdp_queue_query_surface_status;

VdpBitmapSurfacePutBitsNative *vdp_bitmap_put_bits;
VdpBitmapSurfaceCreate  *vdp_bitmap_create;
VdpBitmapSurfaceDestroy *vdp_bitmap_destroy;

VdpDecoderQueryCapabilities *vdp_decoder_query_capabilities;
VdpDecoderCreate *vdp_decoder_create;
VdpDecoderDestroy *vdp_decoder_destroy;
VdpDecoderRender *vdp_decoder_render;

VdpPreemptionCallbackRegister *vdp_preemption_callback_register;

static void vdp_preemption_callback( VdpDevice device, void *context );
static void vdpau_reinit( vo_driver_t *this_gen );

static VdpVideoSurfaceCreate *orig_vdp_video_surface_create;
static VdpVideoSurfaceDestroy *orig_vdp_video_surface_destroy;

static VdpDecoderCreate *orig_vdp_decoder_create;
static VdpDecoderDestroy *orig_vdp_decoder_destroy;
static VdpDecoderRender *orig_vdp_decoder_render;

static Display *guarded_display;

static VdpStatus guarded_vdp_video_surface_create(VdpDevice device, VdpChromaType chroma_type, uint32_t width, uint32_t height,VdpVideoSurface *surface)
{
  VdpStatus r;
#ifdef LOCKDISPLAY
  XLockDisplay(guarded_display);
#endif
  r = orig_vdp_video_surface_create(device, chroma_type, width, height, surface);
#ifdef LOCKDISPLAY
  XUnlockDisplay(guarded_display);
#endif
  return r;
}

static VdpStatus guarded_vdp_video_surface_destroy(VdpVideoSurface surface)
{
  VdpStatus r;
  /*XLockDisplay(guarded_display);*/
  r = orig_vdp_video_surface_destroy(surface);
  /*XUnlockDisplay(guarded_display);*/
  return r;
}

static VdpStatus guarded_vdp_decoder_create(VdpDevice device, VdpDecoderProfile profile, uint32_t width, uint32_t height, uint32_t max_references, VdpDecoder *decoder)
{
  VdpStatus r;
#ifdef LOCKDISPLAY
  XLockDisplay(guarded_display);
#endif
  r = orig_vdp_decoder_create(device, profile, width, height, max_references, decoder);
#ifdef LOCKDISPLAY
  XUnlockDisplay(guarded_display);
#endif
  return r;
}

static VdpStatus guarded_vdp_decoder_destroy(VdpDecoder decoder)
{
  VdpStatus r;
#ifdef LOCKDISPLAY
  XLockDisplay(guarded_display);
#endif
  r = orig_vdp_decoder_destroy(decoder);
#ifdef LOCKDISPLAY
  XUnlockDisplay(guarded_display);
#endif
  return r;
}

static VdpStatus guarded_vdp_decoder_render(VdpDecoder decoder, VdpVideoSurface target, VdpPictureInfo const *picture_info, uint32_t bitstream_buffer_count, VdpBitstreamBuffer const *bitstream_buffers)
{
  VdpStatus r;
#ifdef LOCKDISPLAY
  XLockDisplay(guarded_display);
#endif
  r = orig_vdp_decoder_render(decoder, target, picture_info, bitstream_buffer_count, bitstream_buffers);
#ifdef LOCKDISPLAY
  XUnlockDisplay(guarded_display);
#endif
  return r;
}



typedef struct {
  VdpBitmapSurface ovl_bitmap;
  uint32_t  bitmap_width, bitmap_height;
  int ovl_w, ovl_h; /* overlay's width and height */
  int ovl_x, ovl_y; /* overlay's top-left display position */
  int unscaled;
  int expected_overlay_width; /*if >0 scale to video width*/
  int expected_overlay_height; /* if >0 scale to video height */
} vdpau_overlay_t;


typedef struct {
  int                 x;
  int                 y;
  int                 w;
  int                 h;
}
argb_ovl_data_t;


typedef struct {
  vo_frame_t         vo_frame;

  int                width, height, format, flags;
  double             ratio;

  int                surface_cleared_nr;

  vdpau_accel_t     vdpau_accel_data;
} vdpau_frame_t;


typedef struct {

  vo_driver_t        vo_driver;
  vo_scale_t         sc;

  Display           *display;
  int                screen;
  Drawable           drawable;

  config_values_t   *config;

  int ovl_changed;
  vdpau_overlay_t     overlays[XINE_VORAW_MAX_OVL];
  yuv2rgb_factory_t   *yuv2rgb_factory;
  yuv2rgb_t           *ovl_yuv2rgb;
  VdpOutputSurface    overlay_output;
  uint32_t            overlay_output_width;
  uint32_t            overlay_output_height;
  int                 has_overlay;

  VdpOutputSurface    overlay_unscaled;
  uint32_t            overlay_unscaled_width;
  uint32_t            overlay_unscaled_height;
  int                 has_unscaled;

  VdpOutputSurface    argb_overlay;
  uint32_t            argb_overlay_width;
  uint32_t            argb_overlay_height;
  int                 has_argb_overlay;
  int                 argb_ovl_count;
  vo_overlay_t       *argb_ovl[XINE_VORAW_MAX_OVL];
  int                 argb_ovl_data_count;
  argb_ovl_data_t     argb_ovl_data[XINE_VORAW_MAX_OVL];

  int32_t             video_window_x;
  int32_t             video_window_y;
  int32_t             video_window_width;
  int32_t             video_window_height;

  VdpVideoSurface      soft_surface;
  uint32_t             soft_surface_width;
  uint32_t             soft_surface_height;
  int                  soft_surface_format;

#define NOUTPUTSURFACE 2
  VdpOutputSurface     output_surface[NOUTPUTSURFACE];
  uint8_t              current_output_surface;
  uint32_t             output_surface_width[NOUTPUTSURFACE];
  uint32_t             output_surface_height[NOUTPUTSURFACE];
  uint8_t              init_queue;

  VdpVideoMixer        video_mixer;
  VdpChromaType        video_mixer_chroma;
  uint32_t             video_mixer_width;
  uint32_t             video_mixer_height;
  VdpColorStandard     color_standard;
  VdpBool              temporal_spatial_is_supported;
  VdpBool              temporal_is_supported;
  VdpBool              noise_reduction_is_supported;
  VdpBool              sharpness_is_supported;
  VdpBool              inverse_telecine_is_supported;
  VdpBool              skip_chroma_is_supported;
  VdpBool              background_is_supported;

  char*                deinterlacers_name[NUMBER_OF_DEINTERLACERS+1];
  int                  deinterlacers_method[NUMBER_OF_DEINTERLACERS];

  int                  scaling_level_max;
  int                  scaling_level_current;

  VdpColor             back_color;

  vdpau_frame_t        *back_frame[ NUM_FRAMES_BACK ];

  uint32_t          capabilities;
  xine_t            *xine;

  int               hue;
  int               saturation;
  int               brightness;
  int               contrast;
  int               sharpness;
  int               noise;
  int               deinterlace;
  int               deinterlace_method;
  int               enable_inverse_telecine;
  int               honor_progressive;
  int               skip_chroma;
  int               studio_levels;
  int               background;

  int               vdp_runtime_nr;
  int               reinit_needed;

  int               surface_cleared_nr;

  int               allocated_surfaces;
  int		            zoom_x;
  int		            zoom_y;
} vdpau_driver_t;


typedef struct {
  video_driver_class_t driver_class;
  xine_t              *xine;
} vdpau_class_t;



static void vdpau_overlay_clut_yuv2rgb(vdpau_driver_t  *this, vo_overlay_t *overlay, vdpau_frame_t *frame)
{
  int i;
  clut_t* clut = (clut_t*) overlay->color;

  if (!overlay->rgb_clut) {
    for ( i=0; i<sizeof(overlay->color)/sizeof(overlay->color[0]); i++ ) {
      *((uint32_t *)&clut[i]) = this->ovl_yuv2rgb->yuv2rgb_single_pixel_fun(this->ovl_yuv2rgb, clut[i].y, clut[i].cb, clut[i].cr);
    }
    overlay->rgb_clut++;
  }
  if (!overlay->hili_rgb_clut) {
    clut = (clut_t*) overlay->hili_color;
    for ( i=0; i<sizeof(overlay->color)/sizeof(overlay->color[0]); i++) {
      *((uint32_t *)&clut[i]) = this->ovl_yuv2rgb->yuv2rgb_single_pixel_fun(this->ovl_yuv2rgb, clut[i].y, clut[i].cb, clut[i].cr);
    }
    overlay->hili_rgb_clut++;
  }
}



static void vdpau_process_argb_ovls(vdpau_driver_t *this_gen, vo_frame_t *frame_gen)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;
  int i, k;

  vo_overlay_t *ovl[XINE_VORAW_MAX_OVL];
  argb_ovl_data_t ovl_data[XINE_VORAW_MAX_OVL];
  int ovl_data_count = 0;

  int total_extent_width = 0, total_extent_height = 0;
  this->video_window_x      = 0;
  this->video_window_y      = 0;
  this->video_window_width  = 0;
  this->video_window_height = 0;

  /* lock layers while processing and determine extent */
  for (i = 0; i < this->argb_ovl_count; i++) {
    pthread_mutex_lock(&this->argb_ovl[i]->argb_layer->mutex);

    if (this->argb_ovl[i]->argb_layer->buffer != NULL) {
      int extent_width  = this->argb_ovl[i]->extent_width;
      int extent_height = this->argb_ovl[i]->extent_height;
      if (extent_width <= 0 || extent_height <= 0) {
        extent_width  = frame_gen->width;
        extent_height = frame_gen->height;
      }
      if (extent_width > 0 && extent_height > 0) {
        if (total_extent_width < extent_width)
          total_extent_width = extent_width;
        if (total_extent_height < extent_height)
          total_extent_height = extent_height;
        ovl_data[ovl_data_count].x = this->argb_ovl[i]->x;
        ovl_data[ovl_data_count].y = this->argb_ovl[i]->y;
        ovl_data[ovl_data_count].w = this->argb_ovl[i]->width;
        ovl_data[ovl_data_count].h = this->argb_ovl[i]->height;
        ovl[ovl_data_count++] = this->argb_ovl[i];
      }
      if (this->argb_ovl[i]->video_window_width > 0
        && this->argb_ovl[i]->video_window_height > 0) {
        /* last one wins */
        this->video_window_x      = this->argb_ovl[i]->video_window_x;
        this->video_window_y      = this->argb_ovl[i]->video_window_y;
        this->video_window_width  = this->argb_ovl[i]->video_window_width;
        this->video_window_height = this->argb_ovl[i]->video_window_height;
      }
    }
  }

  /* adjust surface */
  if (total_extent_width > 0 && total_extent_height > 0) {
    if (this->argb_overlay_width != total_extent_width || this->argb_overlay_height != total_extent_height || this->argb_overlay == VDP_INVALID_HANDLE) {
      if (this->argb_overlay != VDP_INVALID_HANDLE)
        vdp_output_surface_destroy(this->argb_overlay);

      VdpStatus st = vdp_output_surface_create(vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, total_extent_width, total_extent_height, &this->argb_overlay);
      if (st != VDP_STATUS_OK)
        fprintf(stderr, "vdpau_process_argb_ovl: vdp_output_surface_create failed : %s\n", vdp_get_error_string(st));

      this->argb_overlay_width  = total_extent_width;
      this->argb_overlay_height = total_extent_height;

      /* change argb_ovl_data to wipe complete surface */
      this->argb_ovl_data_count = 1;
      this->argb_ovl_data[0].x = 0;
      this->argb_ovl_data[0].y = 0;
      this->argb_ovl_data[0].w = total_extent_width;
      this->argb_ovl_data[0].h = total_extent_height;

      /* extend dirty areas to maximum for filling wiped surface */
      for (i = 0; i < ovl_data_count; i++) {
        ovl[i]->argb_layer->x1 = 0;
        ovl[i]->argb_layer->y1 = 0;
        ovl[i]->argb_layer->x2 = ovl[i]->width;
        ovl[i]->argb_layer->y2 = ovl[i]->height;
      }
    }
  }

  /* wipe surface for gone overlays */
  if (this->argb_overlay != VDP_INVALID_HANDLE) {
    uint32_t *zeros = NULL;
    for (i = 0; i < this->argb_ovl_data_count; i++) {
      argb_ovl_data_t *curr_ovl_data = &this->argb_ovl_data[i];
      int ovl_gone = 1;
      for (k = 0; k < ovl_data_count; k++) {
        if (0 == memcmp(curr_ovl_data, &ovl_data[k], sizeof (*curr_ovl_data))) {
          ovl_gone = 0;
          break;
        }
      }
      if (!ovl_gone)
        continue;
      if (!zeros)
        zeros = calloc(4, this->argb_overlay_width * this->argb_overlay_height);
      if (zeros) {
        uint32_t pitch = curr_ovl_data->w * 4;
        VdpRect dest = { curr_ovl_data->x, curr_ovl_data->y, curr_ovl_data->x + curr_ovl_data->w, curr_ovl_data->y + curr_ovl_data->h };
        VdpStatus st = vdp_output_surface_put_bits(this->argb_overlay, (void *)&zeros, &pitch, &dest);
        if (st != VDP_STATUS_OK)
          fprintf(stderr, "vdpau_process_argb_ovl: vdp_output_surface_put_bits_native failed : %s\n", vdp_get_error_string(st));
      }
    }
    free(zeros);
  }

  /* set destination area according to dirty area of argb layer and reset dirty area */
  for (i = 0; i < ovl_data_count; i++) {
    uint32_t pitch = ovl[i]->width * 4;
    uint32_t *buffer_start = ovl[i]->argb_layer->buffer + ovl[i]->argb_layer->y1 * ovl[i]->width + ovl[i]->argb_layer->x1;
    VdpRect dest = { ovl[i]->x + ovl[i]->argb_layer->x1, ovl[i]->y + ovl[i]->argb_layer->y1, ovl[i]->x + ovl[i]->argb_layer->x2, ovl[i]->y + ovl[i]->argb_layer->y2 };
    ovl[i]->argb_layer->x1 = ovl[i]->width;
    ovl[i]->argb_layer->y1 = ovl[i]->height;
    ovl[i]->argb_layer->x2 = 0;
    ovl[i]->argb_layer->y2 = 0;

    VdpStatus st = vdp_output_surface_put_bits(this->argb_overlay, (void *)&buffer_start, &pitch, &dest);
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vdpau_process_argb_ovl: vdp_output_surface_put_bits_native failed : %s\n", vdp_get_error_string(st));
    else
      this->has_argb_overlay = 1;
  }

  /* store ovl_data */
  memcpy(this->argb_ovl_data, ovl_data, sizeof (ovl_data));
  this->argb_ovl_data_count = ovl_data_count;

  /* unlock layers */
  for (i = 0; i < this->argb_ovl_count; i++)
    pthread_mutex_unlock(&this->argb_ovl[i]->argb_layer->mutex);
}



static int vdpau_process_ovl( vdpau_driver_t *this_gen, vo_overlay_t *overlay )
{
  vdpau_overlay_t *ovl = &this_gen->overlays[this_gen->ovl_changed-1];

  if ( overlay->width<=0 || overlay->height<=0 )
    return 0;

  if ( (ovl->bitmap_width < overlay->width ) || (ovl->bitmap_height < overlay->height) || (ovl->ovl_bitmap == VDP_INVALID_HANDLE) ) {
    if (ovl->ovl_bitmap != VDP_INVALID_HANDLE) {
      vdp_bitmap_destroy( ovl->ovl_bitmap );
      ovl->ovl_bitmap = VDP_INVALID_HANDLE;
    }
    VdpStatus st = vdp_bitmap_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, overlay->width, overlay->height, 0, &ovl->ovl_bitmap );
    if ( st != VDP_STATUS_OK ) {
      fprintf(stderr, "vdpau_process_ovl: vdp_bitmap_create failed : %s\n", vdp_get_error_string(st) );
    }
    ovl->bitmap_width = overlay->width;
    ovl->bitmap_height = overlay->height;
  }
  ovl->ovl_w = overlay->width;
  ovl->ovl_h = overlay->height;
  ovl->ovl_x = overlay->x;
  ovl->ovl_y = overlay->y;
  ovl->unscaled = overlay->unscaled;
  ovl->expected_overlay_width = overlay->extent_width;
  ovl->expected_overlay_height = overlay->extent_height;
  uint32_t *buf = (uint32_t*)malloc(ovl->ovl_w*ovl->ovl_h*4);
  if ( !buf )
    return 0;

  int num_rle = overlay->num_rle;
  rle_elem_t *rle = overlay->rle;
  uint32_t *rgba = buf;
  uint32_t red, green, blue, alpha;
  clut_t *low_colors = (clut_t*)overlay->color;
  clut_t *hili_colors = (clut_t*)overlay->hili_color;
  uint8_t *low_trans = overlay->trans;
  uint8_t *hili_trans = overlay->hili_trans;
  clut_t *colors;
  uint8_t *trans;
  int rlelen = 0;
  uint8_t clr = 0;
  int i, pos=0, x, y;

  while ( num_rle>0 ) {
    x = pos%ovl->ovl_w;
    y = pos/ovl->ovl_w;
    if ( (x>=overlay->hili_left && x<=overlay->hili_right) && (y>=overlay->hili_top && y<=overlay->hili_bottom) ) {
      colors = hili_colors;
      trans = hili_trans;
    }
    else {
      colors = low_colors;
      trans = low_trans;
    }
    rlelen = rle->len;
    clr = rle->color;
    for ( i=0; i<rlelen; ++i ) {
      if ( trans[clr] == 0 ) {
        alpha = red = green = blue = 0;
      }
      else {
        red = colors[clr].y; /* red */
        green = colors[clr].cr; /* green */
        blue = colors[clr].cb; /* blue */
        alpha = trans[clr]*255/15;
      }
      *rgba = (alpha<<24) | (red<<16) | (green<<8) | blue;
      rgba++;
      ++pos;
    }
    ++rle;
    --num_rle;
  }
  uint32_t pitch = ovl->ovl_w*4;
  VdpRect dest = { 0, 0, ovl->ovl_w, ovl->ovl_h };
  VdpStatus st = vdp_bitmap_put_bits( ovl->ovl_bitmap, &buf, &pitch, &dest);
  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vdpau_process_ovl: vdp_bitmap_put_bits failed : %s\n", vdp_get_error_string(st) );
  }
  free(buf);
  return 1;
}



static void vdpau_overlay_begin (vo_driver_t *this_gen, vo_frame_t *frame_gen, int changed)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;

  if ( !changed )
    return;

  this->has_overlay = this->has_unscaled = 0;
  this->has_argb_overlay = 0;
  this->argb_ovl_count = 0;
  ++this->ovl_changed;
}



static void vdpau_overlay_blend (vo_driver_t *this_gen, vo_frame_t *frame_gen, vo_overlay_t *overlay)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;
  vdpau_frame_t *frame = (vdpau_frame_t *) frame_gen;

  if (!this->ovl_changed)
    return;

  if (overlay->rle) {
    if (this->ovl_changed >= XINE_VORAW_MAX_OVL)
      return;
    if (!overlay->rgb_clut || !overlay->hili_rgb_clut)
      vdpau_overlay_clut_yuv2rgb (this, overlay, frame);
    if ( vdpau_process_ovl( this, overlay ) )
      ++this->ovl_changed;
  }

  if (overlay->argb_layer) {
    if (this->argb_ovl_count >= XINE_VORAW_MAX_OVL)
      return;
    this->argb_ovl[this->argb_ovl_count++] = overlay;
  }
}



static void vdpau_overlay_end (vo_driver_t *this_gen, vo_frame_t *frame)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;
  int i;
  VdpStatus st;

  if ( !this->ovl_changed )
    return;

  if (this->argb_ovl_count || this->argb_ovl_data_count)
    vdpau_process_argb_ovls(this, frame);

  if ( !(this->ovl_changed-1) ) {
    this->ovl_changed = 0;
    this->has_overlay = 0;
    this->has_unscaled = 0;
    return;
  }

  int w=0, h=0;
  int scaler = 0;
  for ( i=0; i<this->ovl_changed-1; ++i ) {
    if ( this->overlays[i].unscaled )
      continue;
    if ( w < (this->overlays[i].ovl_x+this->overlays[i].ovl_w) )
      w = this->overlays[i].ovl_x+this->overlays[i].ovl_w;
    if ( h < (this->overlays[i].ovl_y+this->overlays[i].ovl_h) )
      h = this->overlays[i].ovl_y+this->overlays[i].ovl_h;
    if ( this->overlays[i].expected_overlay_width )
      scaler = 1;
    if ( this->overlays[i].expected_overlay_height )
      scaler = 1;
  }

  if ( scaler ) {
    w = this->video_mixer_width;
    h = this->video_mixer_height;
  }

  int out_w = (w>frame->width) ? w : frame->width;
  int out_h = (h>frame->height) ? h : frame->height;

  if ( (this->overlay_output_width!=out_w || this->overlay_output_height!=out_h) && this->overlay_output != VDP_INVALID_HANDLE ) {
    st = vdp_output_surface_destroy( this->overlay_output );
    if ( st != VDP_STATUS_OK ) {
      fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_destroy failed : %s\n", vdp_get_error_string(st) );
    }
    this->overlay_output = VDP_INVALID_HANDLE;
  }

  this->overlay_output_width = out_w;
  this->overlay_output_height = out_h;

  w = 64; h = 64;
  for ( i=0; i<this->ovl_changed-1; ++i ) {
    if ( !this->overlays[i].unscaled )
      continue;
    if ( w < (this->overlays[i].ovl_x+this->overlays[i].ovl_w) )
      w = this->overlays[i].ovl_x+this->overlays[i].ovl_w;
    if ( h < (this->overlays[i].ovl_y+this->overlays[i].ovl_h) )
      h = this->overlays[i].ovl_y+this->overlays[i].ovl_h;
  }

  if ( (this->overlay_unscaled_width!=w || this->overlay_unscaled_height!=h) && this->overlay_unscaled != VDP_INVALID_HANDLE ) {
    st = vdp_output_surface_destroy( this->overlay_unscaled );
    if ( st != VDP_STATUS_OK ) {
      fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_destroy failed : %s\n", vdp_get_error_string(st) );
    }
    this->overlay_unscaled = VDP_INVALID_HANDLE;
  }

  this->overlay_unscaled_width = w;
  this->overlay_unscaled_height = h;

  if ( this->overlay_unscaled == VDP_INVALID_HANDLE ) {
    st = vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->overlay_unscaled_width, this->overlay_unscaled_height, &this->overlay_unscaled );
    if ( st != VDP_STATUS_OK )
      fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_create failed : %s\n", vdp_get_error_string(st) );
  }

  if ( this->overlay_output == VDP_INVALID_HANDLE ) {
    st = vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->overlay_output_width, this->overlay_output_height, &this->overlay_output );
    if ( st != VDP_STATUS_OK )
      fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_create failed : %s\n", vdp_get_error_string(st) );
  }

  w = (this->overlay_unscaled_width>this->overlay_output_width) ? this->overlay_unscaled_width : this->overlay_output_width;
  h = (this->overlay_unscaled_height>this->overlay_output_height) ? this->overlay_unscaled_height : this->overlay_output_height;

  uint32_t *buf = (uint32_t*)calloc(w*4,h);
  uint32_t pitch = w*4;
  VdpRect clear = { 0, 0, this->overlay_output_width, this->overlay_output_height };
  st = vdp_output_surface_put_bits( this->overlay_output, &buf, &pitch, &clear );
  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_put_bits (clear) failed : %s\n", vdp_get_error_string(st) );
  }
  clear.x1 = this->overlay_unscaled_width; clear.y1 = this->overlay_unscaled_height;
  st = vdp_output_surface_put_bits( this->overlay_unscaled, &buf, &pitch, &clear );
  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_put_bits (clear) failed : %s\n", vdp_get_error_string(st) );
  }
  free(buf);

  VdpOutputSurface *surface;
  for ( i=0; i<this->ovl_changed-1; ++i ) {
    VdpRect dest = { this->overlays[i].ovl_x, this->overlays[i].ovl_y, this->overlays[i].ovl_x+this->overlays[i].ovl_w, this->overlays[i].ovl_y+this->overlays[i].ovl_h };
    if ( this->overlays[i].expected_overlay_width ) {
      double rx = (double)this->overlay_output_width/(double)this->overlays[i].expected_overlay_width;
      double ry = (double)this->overlay_output_height/(double)this->overlays[i].expected_overlay_height;
      dest.x0 *= rx; dest.y0 *= ry; dest.x1 *=rx; dest.y1 *= ry;
      lprintf( "vdpau_overlay_end: overlay_width=%d overlay_height=%d rx=%f ry=%f\n", this->overlay_output_width, this->overlay_output_height, rx, ry );
    }
    VdpRect src = { 0, 0, this->overlays[i].ovl_w, this->overlays[i].ovl_h };
    surface = (this->overlays[i].unscaled) ? &this->overlay_unscaled : &this->overlay_output;
    st = vdp_output_surface_render_bitmap_surface( *surface, &dest, this->overlays[i].ovl_bitmap, &src, 0, &blend, 0 );
    if ( st != VDP_STATUS_OK ) {
      fprintf(stderr, "vdpau_overlay_end: vdp_output_surface_render_bitmap_surface failed : %s\n", vdp_get_error_string(st) );
    }
  }
  this->has_overlay = 1;
  this->ovl_changed = 0;
}



static void vdpau_frame_proc_slice (vo_frame_t *vo_img, uint8_t **src)
{
  vdpau_frame_t  *frame = (vdpau_frame_t *) vo_img ;

  vo_img->proc_called = 1;
}



static void vdpau_frame_field (vo_frame_t *vo_img, int which_field)
{
}



static void vdpau_frame_dispose (vo_frame_t *vo_img)
{
  vdpau_frame_t  *frame = (vdpau_frame_t *) vo_img ;

  av_free (frame->vo_frame.base[0]);
  av_free (frame->vo_frame.base[1]);
  av_free (frame->vo_frame.base[2]);
  if ( frame->vdpau_accel_data.surface != VDP_INVALID_HANDLE )
    vdp_video_surface_destroy( frame->vdpau_accel_data.surface );
  free (frame);
}



static vo_frame_t *vdpau_alloc_frame (vo_driver_t *this_gen)
{
  vdpau_frame_t  *frame;
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;

  lprintf( "vo_vdpau: vdpau_alloc_frame\n" );

  frame = (vdpau_frame_t *) calloc(1, sizeof(vdpau_frame_t));

  if (!frame)
    return NULL;

  frame->vo_frame.base[0] = frame->vo_frame.base[1] = frame->vo_frame.base[2] = NULL;
  frame->width = frame->height = frame->format = frame->flags = 0;

  frame->vo_frame.accel_data = &frame->vdpau_accel_data;

  pthread_mutex_init (&frame->vo_frame.mutex, NULL);

  /*
   * supply required functions/fields
   */
  frame->vo_frame.proc_duplicate_frame_data = NULL;
  frame->vo_frame.proc_slice = vdpau_frame_proc_slice;
  frame->vo_frame.proc_frame = NULL;
  frame->vo_frame.field      = vdpau_frame_field;
  frame->vo_frame.dispose    = vdpau_frame_dispose;
  frame->vo_frame.driver     = this_gen;

  frame->surface_cleared_nr = 0;

  frame->vdpau_accel_data.vo_frame = &frame->vo_frame;
  frame->vdpau_accel_data.vdp_device = vdp_device;
  frame->vdpau_accel_data.surface = VDP_INVALID_HANDLE;
  frame->vdpau_accel_data.chroma = VDP_CHROMA_TYPE_420;
  frame->vdpau_accel_data.color_standard = this->color_standard;
  frame->vdpau_accel_data.vdp_decoder_create = vdp_decoder_create;
  frame->vdpau_accel_data.vdp_decoder_destroy = vdp_decoder_destroy;
  frame->vdpau_accel_data.vdp_decoder_render = vdp_decoder_render;
  frame->vdpau_accel_data.vdp_get_error_string = vdp_get_error_string;
  frame->vdpau_accel_data.vdp_runtime_nr = this->vdp_runtime_nr;
  frame->vdpau_accel_data.current_vdp_runtime_nr = &this->vdp_runtime_nr;

  return (vo_frame_t *) frame;
}



static void vdpau_provide_standard_frame_data (vo_frame_t *this_gen, xine_current_frame_data_t *data)
{
  vdpau_frame_t *this = (vdpau_frame_t *)this_gen;
  VdpStatus st;
  VdpYCbCrFormat format;

  if (this->vo_frame.format != XINE_IMGFMT_VDPAU) {
    fprintf(stderr, "vdpau_provide_standard_frame_data: unexpected frame format 0x%08x!\n", this->vo_frame.format);
    return;
  }

  if (!(this->flags & VO_CHROMA_422)) {
    data->format = XINE_IMGFMT_YV12;
    data->img_size = this->vo_frame.width * this->vo_frame.height
                   + ((this->vo_frame.width + 1) / 2) * ((this->vo_frame.height + 1) / 2)
                   + ((this->vo_frame.width + 1) / 2) * ((this->vo_frame.height + 1) / 2);
    if (data->img) {
      this->vo_frame.pitches[0] = 8*((this->vo_frame.width + 7) / 8);
      this->vo_frame.pitches[1] = 8*((this->vo_frame.width + 15) / 16);
      this->vo_frame.pitches[2] = 8*((this->vo_frame.width + 15) / 16);
      this->vo_frame.base[0] = av_mallocz(this->vo_frame.pitches[0] * this->vo_frame.height);
      this->vo_frame.base[1] = av_mallocz(this->vo_frame.pitches[1] * ((this->vo_frame.height+1)/2));
      this->vo_frame.base[2] = av_mallocz(this->vo_frame.pitches[2] * ((this->vo_frame.height+1)/2));
      format = VDP_YCBCR_FORMAT_YV12;
    }
  } else {
    data->format = XINE_IMGFMT_YUY2;
    data->img_size = this->vo_frame.width * this->vo_frame.height
                   + ((this->vo_frame.width + 1) / 2) * this->vo_frame.height
                   + ((this->vo_frame.width + 1) / 2) * this->vo_frame.height;
    if (data->img) {
      this->vo_frame.pitches[0] = 8*((this->vo_frame.width + 3) / 4);
      this->vo_frame.base[0] = av_mallocz(this->vo_frame.pitches[0] * this->vo_frame.height);
      format = VDP_YCBCR_FORMAT_YUYV;
    }
  }

  if (data->img) {
    st = vdp_video_surface_getbits_ycbcr(this->vdpau_accel_data.surface, format, this->vo_frame.base, this->vo_frame.pitches);
    if (st != VDP_STATUS_OK)
      fprintf(stderr, "vo_vdpau: failed to get surface bits !! %s\n", vdp_get_error_string(st));

    if (format == VDP_YCBCR_FORMAT_YV12) {
      yv12_to_yv12(
       /* Y */
        this->vo_frame.base[0], this->vo_frame.pitches[0],
        data->img, this->vo_frame.width,
       /* U */
        this->vo_frame.base[2], this->vo_frame.pitches[2],
        data->img+this->vo_frame.width*this->vo_frame.height, this->vo_frame.width/2,
       /* V */
        this->vo_frame.base[1], this->vo_frame.pitches[1],
        data->img+this->vo_frame.width*this->vo_frame.height+this->vo_frame.width*this->vo_frame.height/4, this->vo_frame.width/2,
       /* width x height */
        this->vo_frame.width, this->vo_frame.height);
    } else {
      yuy2_to_yuy2(
       /* src */
        this->vo_frame.base[0], this->vo_frame.pitches[0],
       /* dst */
        data->img, this->vo_frame.width*2,
       /* width x height */
        this->vo_frame.width, this->vo_frame.height);
    }

    av_freep (&this->vo_frame.base[0]);
    av_freep (&this->vo_frame.base[1]);
    av_freep (&this->vo_frame.base[2]);
  }
}



static void vdpau_duplicate_frame_data (vo_frame_t *this_gen, vo_frame_t *original)
{
  vdpau_frame_t *this = (vdpau_frame_t *)this_gen;
  vdpau_frame_t *orig = (vdpau_frame_t *)original;
  VdpStatus st;
  VdpYCbCrFormat format;

  if (orig->vo_frame.format != XINE_IMGFMT_VDPAU) {
    fprintf(stderr, "vdpau_duplicate_frame_data: unexpected frame format 0x%08x!\n", orig->vo_frame.format);
    return;
  }

  if(orig->vdpau_accel_data.vdp_runtime_nr != this->vdpau_accel_data.vdp_runtime_nr) {
    fprintf(stderr, "vdpau_duplicate_frame_data: called with invalid frame\n");
    return;
  }

  if (!(orig->flags & VO_CHROMA_422)) {
    this->vo_frame.pitches[0] = 8*((orig->vo_frame.width + 7) / 8);
    this->vo_frame.pitches[1] = 8*((orig->vo_frame.width + 15) / 16);
    this->vo_frame.pitches[2] = 8*((orig->vo_frame.width + 15) / 16);
    this->vo_frame.base[0] = av_mallocz(this->vo_frame.pitches[0] * orig->vo_frame.height);
    this->vo_frame.base[1] = av_mallocz(this->vo_frame.pitches[1] * ((orig->vo_frame.height+1)/2));
    this->vo_frame.base[2] = av_mallocz(this->vo_frame.pitches[2] * ((orig->vo_frame.height+1)/2));
    format = VDP_YCBCR_FORMAT_YV12;
  } else {
    this->vo_frame.pitches[0] = 8*((orig->vo_frame.width + 3) / 4);
    this->vo_frame.base[0] = av_mallocz(this->vo_frame.pitches[0] * orig->vo_frame.height);
    format = VDP_YCBCR_FORMAT_YUYV;
  }

  st = vdp_video_surface_getbits_ycbcr(orig->vdpau_accel_data.surface, format, this->vo_frame.base, this->vo_frame.pitches);
  if (st != VDP_STATUS_OK)
    fprintf(stderr, "vo_vdpau: failed to get surface bits !! %s\n", vdp_get_error_string(st));

  st = vdp_video_surface_putbits_ycbcr(this->vdpau_accel_data.surface, format, this->vo_frame.base, this->vo_frame.pitches);
  if (st != VDP_STATUS_OK)
    fprintf(stderr, "vo_vdpau: failed to put surface bits !! %s\n", vdp_get_error_string(st));

  this->vdpau_accel_data.color_standard = orig->vdpau_accel_data.color_standard;

  av_freep (&this->vo_frame.base[0]);
  av_freep (&this->vo_frame.base[1]);
  av_freep (&this->vo_frame.base[2]);
}



static void vdpau_update_frame_format (vo_driver_t *this_gen, vo_frame_t *frame_gen,
      uint32_t width, uint32_t height, double ratio, int format, int flags)
{
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;
  vdpau_frame_t   *frame = VDPAU_FRAME(frame_gen);

  int clear = 0;

  if ( flags & VO_NEW_SEQUENCE_FLAG )
    ++this->surface_cleared_nr;

  VdpChromaType chroma = (flags & VO_CHROMA_422) ? VDP_CHROMA_TYPE_422 : VDP_CHROMA_TYPE_420;

  vo_frame_t orig_frame_content;
  if (format == XINE_IMGFMT_VDPAU) {
    if (frame_gen != &frame->vo_frame) {
      /* this is an intercepted frame, so we need to detect and propagate any
       * changes on the original vo_frame to all the intercepted frames */
       xine_fast_memcpy(&orig_frame_content, &frame->vo_frame, sizeof (vo_frame_t));
    }
  }

  /* Check frame size and format and reallocate if necessary */
  if ( (frame->width != width) || (frame->height != height) || (frame->format != format) || (frame->format==XINE_IMGFMT_VDPAU && frame->vdpau_accel_data.chroma!=chroma) ||
        (frame->vdpau_accel_data.vdp_runtime_nr != this->vdp_runtime_nr)) {

    /* (re-) allocate render space */
    av_freep (&frame->vo_frame.base[0]);
    av_freep (&frame->vo_frame.base[1]);
    av_freep (&frame->vo_frame.base[2]);

    if (format == XINE_IMGFMT_YV12) {
      frame->vo_frame.pitches[0] = 8*((width + 7) / 8);
      frame->vo_frame.pitches[1] = 8*((width + 15) / 16);
      frame->vo_frame.pitches[2] = 8*((width + 15) / 16);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height);
      frame->vo_frame.base[1] = av_mallocz (frame->vo_frame.pitches[1] * ((height+1)/2));
      frame->vo_frame.base[2] = av_mallocz (frame->vo_frame.pitches[2] * ((height+1)/2));
    } else if (format == XINE_IMGFMT_YUY2){
      frame->vo_frame.pitches[0] = 8*((width + 3) / 4);
      frame->vo_frame.base[0] = av_mallocz (frame->vo_frame.pitches[0] * height);
    }

    if ( frame->vdpau_accel_data.vdp_runtime_nr != this->vdp_runtime_nr ) {
      frame->vdpau_accel_data.surface = VDP_INVALID_HANDLE;
      frame->vdpau_accel_data.vdp_runtime_nr = this->vdp_runtime_nr;
      frame->vdpau_accel_data.vdp_device = vdp_device;
      frame->vo_frame.proc_duplicate_frame_data = NULL;
      frame->vo_frame.proc_provide_standard_frame_data = NULL;
    }

    if ( frame->vdpau_accel_data.surface != VDP_INVALID_HANDLE  ) {
      if ( (frame->width != width) || (frame->height != height) || (format != XINE_IMGFMT_VDPAU) || frame->vdpau_accel_data.chroma != chroma ) {
        lprintf("vo_vdpau: update_frame - destroy surface\n");
        vdp_video_surface_destroy( frame->vdpau_accel_data.surface );
        frame->vdpau_accel_data.surface = VDP_INVALID_HANDLE;
        --this->allocated_surfaces;
        frame->vo_frame.proc_duplicate_frame_data = NULL;
        frame->vo_frame.proc_provide_standard_frame_data = NULL;
      }
    }

    if ( (format == XINE_IMGFMT_VDPAU) && (frame->vdpau_accel_data.surface == VDP_INVALID_HANDLE) ) {
      VdpStatus st = vdp_video_surface_create( vdp_device, chroma, width, height, &frame->vdpau_accel_data.surface );
      if ( st!=VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: failed to create surface !! %s\n", vdp_get_error_string( st ) );
      else {
        clear = 1;
        frame->vdpau_accel_data.chroma = chroma;
        ++this->allocated_surfaces;
        frame->vo_frame.proc_duplicate_frame_data = vdpau_duplicate_frame_data;
        frame->vo_frame.proc_provide_standard_frame_data = vdpau_provide_standard_frame_data;
      }
    }

    frame->width = width;
    frame->height = height;
    frame->format = format;
    frame->flags = flags;

    vdpau_frame_field ((vo_frame_t *)frame, flags);
  }

  if ( (format == XINE_IMGFMT_VDPAU) && (clear || (frame->surface_cleared_nr != this->surface_cleared_nr)) ) {
    lprintf( "clear surface: %d\n", frame->vdpau_accel_data.surface );
    if ( frame->vdpau_accel_data.chroma == VDP_CHROMA_TYPE_422 ) {
      uint8_t *cb = malloc( frame->width * frame->height * 2 );
      memset( cb, 127, frame->width * frame->height * 2 );
      uint32_t pitches[] = { frame->width };
      void* data[] = { cb };
      VdpStatus st = vdp_video_surface_putbits_ycbcr( frame->vdpau_accel_data.surface, VDP_YCBCR_FORMAT_YUYV, &data, pitches );
      if ( st!=VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: failed to clear surface: %s\n", vdp_get_error_string( st ) );
      free( cb );
    }
    else {
      uint8_t *cb = malloc( frame->width * frame->height );
      memset( cb, 127, frame->width * frame->height );
      uint32_t pitches[] = { frame->width, frame->height, frame->height };
      void* data[] = { cb, cb, cb };
      VdpStatus st = vdp_video_surface_putbits_ycbcr( frame->vdpau_accel_data.surface, VDP_YCBCR_FORMAT_YV12, &data, pitches );
      if ( st!=VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: failed to clear surface: %s\n", vdp_get_error_string( st ) );
      free( cb );
    }
    if ( frame->surface_cleared_nr != this->surface_cleared_nr )
      frame->surface_cleared_nr = this->surface_cleared_nr;
  }

  frame->vdpau_accel_data.color_standard = VDP_COLOR_STANDARD_ITUR_BT_601;
  frame->ratio = ratio;
  frame->vo_frame.future_frame = NULL;

  if (format == XINE_IMGFMT_VDPAU) {
    if (frame_gen != &frame->vo_frame) {
      /* this is an intercepted frame, so we need to detect and propagate any
       * changes on the original vo_frame to all the intercepted frames */
      unsigned char *p0 = (unsigned char *)&orig_frame_content;
      unsigned char *p1 = (unsigned char *)&frame->vo_frame;
      int i;
      for (i = 0; i < sizeof (vo_frame_t); i++) {
        if (*p0 != *p1) {
          /* propagate the change */
          vo_frame_t *f = frame_gen;
          while (f->next) {
            /* serveral restrictions apply when intercepting VDPAU frames. So let's check
             * the intercepted frames before modifing them and fail otherwise. */
            unsigned char *p = (unsigned char *)f + i;
            if (*p != *p0) {
              xprintf(this->xine, XINE_VERBOSITY_DEBUG, "vdpau_update_frame_format: a post plugin violates the restrictions on intercepting VDPAU frames\n");
              _x_abort();
            }

            *p = *p1;
            f = f->next;
          }
        }
        p0++;
        p1++;
      }
    }
  }
}



static int vdpau_redraw_needed (vo_driver_t *this_gen)
{
  vdpau_driver_t  *this = (vdpau_driver_t *) this_gen;

  _x_vo_scale_compute_ideal_size( &this->sc );
  if ( _x_vo_scale_redraw_needed( &this->sc ) ) {
    _x_vo_scale_compute_output_size( &this->sc );
    return 1;
  }
  return 0;
}



static void vdpau_release_back_frames( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  int i;

  for ( i=0; i<NUM_FRAMES_BACK; ++i ) {
    if ( this->back_frame[ i ])
      this->back_frame[ i ]->vo_frame.free( &this->back_frame[ i ]->vo_frame );
    this->back_frame[ i ] = NULL;
  }
}



static void vdpau_backup_frame( vo_driver_t *this_gen, vo_frame_t *frame_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  vdpau_frame_t   *frame = (vdpau_frame_t *) frame_gen;

  int i;
  if ( this->back_frame[NUM_FRAMES_BACK-1]) {
    this->back_frame[NUM_FRAMES_BACK-1]->vo_frame.free (&this->back_frame[NUM_FRAMES_BACK-1]->vo_frame);
  }
  for ( i=NUM_FRAMES_BACK-1; i>0; i-- )
    this->back_frame[i] = this->back_frame[i-1];
  this->back_frame[0] = frame;
}



static void vdpau_set_deinterlace( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  VdpVideoMixerFeature features[2];
  VdpBool feature_enables[2];
  int features_count = 0;
  if ( this->temporal_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    ++features_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    ++features_count;
  }

  if ( !features_count )
    return;

  if ( this->deinterlace ) {
    if ( this->video_mixer_width<800 ) {
      feature_enables[0] = feature_enables[1] = 1;
	    if ( this->temporal_is_supported ) {
	      if ( this->temporal_spatial_is_supported )
	        fprintf(stderr, "vo_vdpau: deinterlace: temporal_spatial\n" );
		    else
		      fprintf(stderr, "vo_vdpau: deinterlace: temporal\n" );
      }
      else
        fprintf(stderr, "vo_vdpau: deinterlace: bob\n" );
    }
    else {
      switch ( this->deinterlacers_method[this->deinterlace_method] ) {
        case DEINT_BOB:
          feature_enables[0] = feature_enables[1] = 0;
          fprintf(stderr, "vo_vdpau: deinterlace: bob\n" );
          break;
        case DEINT_HALF_TEMPORAL:
          feature_enables[0] = 1; feature_enables[1] = 0;
          fprintf(stderr, "vo_vdpau: deinterlace: half_temporal\n" );
          break;
        case DEINT_TEMPORAL:
          feature_enables[0] = 1; feature_enables[1] = 0;
          fprintf(stderr, "vo_vdpau: deinterlace: temporal\n" );
          break;
        case DEINT_HALF_TEMPORAL_SPATIAL:
          feature_enables[0] = feature_enables[1] = 1;
          fprintf(stderr, "vo_vdpau: deinterlace: half_temporal_spatial\n" );
          break;
        case DEINT_TEMPORAL_SPATIAL:
          feature_enables[0] = feature_enables[1] = 1;
          fprintf(stderr, "vo_vdpau: deinterlace: temporal_spatial\n" );
          break;
      }
    }
  }
  else {
    feature_enables[0] = feature_enables[1] = 0;
    fprintf(stderr, "vo_vdpau: deinterlace: none\n" );
  }

  vdp_video_mixer_set_feature_enables( this->video_mixer, features_count, features, feature_enables );
}



static void vdpau_set_inverse_telecine( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  if ( !this->inverse_telecine_is_supported )
    return;

  VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE };
  VdpBool feature_enables[1];
  if ( this->deinterlace && this->enable_inverse_telecine )
    feature_enables[0] = 1;
  else
    feature_enables[0] = 0;

  vdp_video_mixer_set_feature_enables( this->video_mixer, 1, features, feature_enables );
  vdp_video_mixer_get_feature_enables( this->video_mixer, 1, features, feature_enables );
  fprintf(stderr, "vo_vdpau: enabled features: inverse_telecine=%d\n", feature_enables[0] );
}



static void vdpau_update_deinterlace_method( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->deinterlace_method = entry->num_value;
  fprintf(stderr,  "vo_vdpau: deinterlace_method=%d\n", this->deinterlace_method );
  vdpau_set_deinterlace( (vo_driver_t*)this_gen );
}



static void vdpau_set_scaling_level( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  int i;
  VdpVideoMixerFeature features[9];
  VdpBool feature_enables[9];
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  for ( i=0; i<this->scaling_level_max; ++i ) {
    features[i] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
    feature_enables[i] = 0;
  }
  vdp_video_mixer_set_feature_enables( this->video_mixer, this->scaling_level_max, features, feature_enables );

  if ( this->scaling_level_current ) {
    features[0] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 - 1 + this->scaling_level_current;
    feature_enables[0] = 1;
    vdp_video_mixer_set_feature_enables( this->video_mixer, 1, features, feature_enables );
  }

  fprintf(stderr,  "vo_vdpau: set_scaling_level=%d\n", this->scaling_level_current );
#endif
}



static void vdpau_update_scaling_level( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->scaling_level_current = entry->num_value;
  fprintf(stderr,  "vo_vdpau: scaling_quality=%d\n", this->scaling_level_current );
  vdpau_set_scaling_level( (vo_driver_t*)this_gen );
}



static void vdpau_update_enable_inverse_telecine( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->enable_inverse_telecine = entry->num_value;
  fprintf(stderr, "vo_vdpau: enable inverse_telecine=%d\n", this->enable_inverse_telecine );
  vdpau_set_inverse_telecine( (vo_driver_t*)this_gen );
}



static void vdpau_honor_progressive_flag( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  this->honor_progressive = entry->num_value;
  fprintf(stderr, "vo_vdpau: honor_progressive=%d\n", this->honor_progressive );
}



static void vdpau_update_noise( vdpau_driver_t *this_gen )
{
  if ( !this_gen->noise_reduction_is_supported )
    return;

  float value = this_gen->noise/100.0;
  if ( value==0 ) {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION };
    VdpBool feature_enables[] = { 0 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: disable noise reduction.\n" );
    return;
  }
  else {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION };
    VdpBool feature_enables[] = { 1 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: enable noise reduction.\n" );
  }

  VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_NOISE_REDUCTION_LEVEL };
  void* attribute_values[] = { &value };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    fprintf(stderr, "vo_vdpau: error, can't set noise reduction level !!\n" );
}



static void vdpau_update_sharpness( vdpau_driver_t *this_gen )
{
  if ( !this_gen->sharpness_is_supported )
    return;

  float value = this_gen->sharpness/100.0;
  if ( value==0 ) {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_SHARPNESS  };
    VdpBool feature_enables[] = { 0 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: disable sharpness.\n" );
    return;
  }
  else {
    VdpVideoMixerFeature features[] = { VDP_VIDEO_MIXER_FEATURE_SHARPNESS  };
    VdpBool feature_enables[] = { 1 };
    vdp_video_mixer_set_feature_enables( this_gen->video_mixer, 1, features, feature_enables );
    fprintf(stderr, "vo_vdpau: enable sharpness.\n" );
  }

  VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_SHARPNESS_LEVEL };
  void* attribute_values[] = { &value };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    fprintf(stderr, "vo_vdpau: error, can't set sharpness level !!\n" );
}



static void vdpau_update_csc( vdpau_driver_t *this_gen )
{
  float hue = this_gen->hue/100.0;
  float saturation = this_gen->saturation/100.0;
  float contrast = this_gen->contrast/100.0;
  float brightness = this_gen->brightness/100.0;

  fprintf(stderr, "vo_vdpau: vdpau_update_csc: hue=%f, saturation=%f, contrast=%f, brightness=%f, color_standard=%d studio_levels=%d\n", hue, saturation, contrast, brightness, this_gen->color_standard, this_gen->studio_levels );

  VdpStatus st;
  VdpCSCMatrix matrix;
  VdpProcamp procamp = { VDP_PROCAMP_VERSION, brightness, contrast, saturation, hue };

  if ( this_gen->studio_levels ) {
    int i;
    float Kr, Kg, Kb;
    float uvcos = procamp.saturation * cos(procamp.hue);
    float uvsin = procamp.saturation * sin(procamp.hue);
    int rgbmin = 16;
    int rgbr = 235 - 16;
    switch ( this_gen->color_standard ) {
      case VDP_COLOR_STANDARD_SMPTE_240M:
        Kr = 0.2122;
        Kg = 0.7013;
        Kb = 0.0865;
        break;
      case VDP_COLOR_STANDARD_ITUR_BT_709:
        Kr = 0.2125;
        Kg = 0.7154;
        Kb = 0.0721;
        break;
      case VDP_COLOR_STANDARD_ITUR_BT_601:
      default:
        Kr = 0.299;
        Kg = 0.587;
        Kb = 0.114;
        break;
    }
    float uv_coeffs[3][2] = {{ 0.000,                                (rgbr / 112.0) * (1 - Kr)           },
                             {-(rgbr / 112.0) * (1 - Kb) * Kb / Kg, -(rgbr / 112.0) * (1 - Kr) * Kr / Kg },
                             { (rgbr / 112.0) * (1 - Kb),            0.000                               }};
    for (i = 0; i < 3; ++i) {
      matrix[i][3]  = procamp.brightness;
      matrix[i][0]  = rgbr * procamp.contrast / 219;
      matrix[i][3] += (-16 / 255.0) * matrix[i][0];
      matrix[i][1]  = uv_coeffs[i][0] * uvcos + uv_coeffs[i][1] * uvsin;
      matrix[i][3] += (-128 / 255.0) * matrix[i][1];
      matrix[i][2]  = uv_coeffs[i][0] * uvsin + uv_coeffs[i][1] * uvcos;
      matrix[i][3] += (-128 / 255.0) * matrix[i][2];
      matrix[i][3] += rgbmin / 255.0;
      matrix[i][3] += 0.5 - procamp.contrast / 2.0;
    }
  }
  else {
    st = vdp_generate_csc_matrix( &procamp, this_gen->color_standard, &matrix );
    if ( st != VDP_STATUS_OK ) {
      fprintf(stderr, "vo_vdpau: error, can't generate csc matrix !!\n" );
      return;
    }
  }
  VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX };
  void* attribute_values[] = { &matrix };
  st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    fprintf(stderr, "vo_vdpau: error, can't set csc matrix !!\n" );
}



static void vdpau_update_skip_chroma( vdpau_driver_t *this_gen )
{
  if ( !this_gen->skip_chroma_is_supported )
    return;

  VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_SKIP_CHROMA_DEINTERLACE };
  void* attribute_values[] = { &(this_gen->skip_chroma) };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    fprintf(stderr, "vo_vdpau: error, can't set skip_chroma !!\n" );
  else
    fprintf(stderr, "vo_vdpau: skip_chroma = %d\n", this_gen->skip_chroma );
}



static void vdpau_set_skip_chroma( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  this->skip_chroma = entry->num_value;
  vdpau_update_skip_chroma( this );
}



static void vdpau_set_studio_levels( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  this->studio_levels = entry->num_value;
  vdpau_update_csc( this );
}



static void vdpau_update_background( vdpau_driver_t *this_gen )
{
  if ( !this_gen->background_is_supported )
    return;

  VdpVideoMixerAttribute attributes [] = { VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR };
  VdpColor bg = { (this_gen->background >> 16) / 255.f, ((this_gen->background >> 8) & 0xff) / 255.f, (this_gen->background & 0xff) / 255.f, 1 };
  void* attribute_values[] = { &bg };
  VdpStatus st = vdp_video_mixer_set_attribute_values( this_gen->video_mixer, 1, attributes, attribute_values );
  if ( st != VDP_STATUS_OK )
    printf( "vo_vdpau: error, can't set background_color !!\n" );
  else
    printf( "vo_vdpau: background_color = %d\n", this_gen->background );
}



static void vdpau_set_background( void *this_gen, xine_cfg_entry_t *entry )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  entry->num_value &= 0xffffff;
  this->background = entry->num_value;
  vdpau_update_background( this );
}



static void vdpau_shift_queue( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  if ( this->init_queue<2 )
    ++this->init_queue;
  ++this->current_output_surface;
  if ( this->current_output_surface > (NOUTPUTSURFACE-1) )
    this->current_output_surface = 0;
}



static void vdpau_check_output_size( vo_driver_t *this_gen )
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;

  if ( (this->sc.gui_width > this->output_surface_width[this->current_output_surface]) || (this->sc.gui_height > this->output_surface_height[this->current_output_surface]) ) {
    /* recreate output surface to match window size */
    lprintf( "vo_vdpau: output_surface size update\n" );
    this->output_surface_width[this->current_output_surface] = this->sc.gui_width;
    this->output_surface_height[this->current_output_surface] = this->sc.gui_height;

    vdp_output_surface_destroy( this->output_surface[this->current_output_surface] );
    vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->output_surface_width[this->current_output_surface], this->output_surface_height[this->current_output_surface], &this->output_surface[this->current_output_surface] );
  }
}



static void vdpau_display_frame (vo_driver_t *this_gen, vo_frame_t *frame_gen)
{
  vdpau_driver_t  *this  = (vdpau_driver_t *) this_gen;
  vdpau_frame_t   *frame = (vdpau_frame_t *) frame_gen;
  VdpStatus st;
  VdpVideoSurface surface;
  VdpChromaType chroma = this->video_mixer_chroma;
  VdpColorStandard color_standard = this->color_standard;
  uint32_t mix_w = this->video_mixer_width;
  uint32_t mix_h = this->video_mixer_height;
  VdpTime stream_speed;


  if(this->reinit_needed)
    vdpau_reinit(this_gen);

  if ( (frame->width != this->sc.delivered_width) || (frame->height != this->sc.delivered_height) || (frame->ratio != this->sc.delivered_ratio) ) {
    this->sc.force_redraw = 1;    /* trigger re-calc of output size */
  }

  this->sc.delivered_height = frame->height;
  this->sc.delivered_width  = frame->width;
  this->sc.delivered_ratio  = frame->ratio;
  this->sc.crop_left        = frame->vo_frame.crop_left;
  this->sc.crop_right       = frame->vo_frame.crop_right;
  this->sc.crop_top         = frame->vo_frame.crop_top;
  this->sc.crop_bottom      = frame->vo_frame.crop_bottom;

  vdpau_redraw_needed( this_gen );

  if ( (frame->format == XINE_IMGFMT_YV12) || (frame->format == XINE_IMGFMT_YUY2) ) {
    chroma = ( frame->format==XINE_IMGFMT_YV12 )? VDP_CHROMA_TYPE_420 : VDP_CHROMA_TYPE_422;
    if ( (frame->width != this->soft_surface_width) || (frame->height != this->soft_surface_height) || (frame->format != this->soft_surface_format) ) {
      lprintf( "vo_vdpau: soft_surface size update\n" );
      /* recreate surface to match frame changes */
      this->soft_surface_width = frame->width;
      this->soft_surface_height = frame->height;
      this->soft_surface_format = frame->format;
      vdp_video_surface_destroy( this->soft_surface );
      this->soft_surface = VDP_INVALID_HANDLE;
      vdp_video_surface_create( vdp_device, chroma, this->soft_surface_width, this->soft_surface_height, &this->soft_surface );
    }
    /* FIXME: have to swap U and V planes to get correct colors !! */
    uint32_t pitches[] = { frame->vo_frame.pitches[0], frame->vo_frame.pitches[2], frame->vo_frame.pitches[1] };
    void* data[] = { frame->vo_frame.base[0], frame->vo_frame.base[2], frame->vo_frame.base[1] };
    if ( frame->format==XINE_IMGFMT_YV12 ) {
      st = vdp_video_surface_putbits_ycbcr( this->soft_surface, VDP_YCBCR_FORMAT_YV12, &data, pitches );
      if ( st != VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: vdp_video_surface_putbits_ycbcr YV12 error : %s\n", vdp_get_error_string( st ) );
    }
    else {
      st = vdp_video_surface_putbits_ycbcr( this->soft_surface, VDP_YCBCR_FORMAT_YUYV, &data, pitches );
      if ( st != VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: vdp_video_surface_putbits_ycbcr YUY2 error : %s\n", vdp_get_error_string( st ) );
    }
    surface = this->soft_surface;
    mix_w = this->soft_surface_width;
    mix_h = this->soft_surface_height;
  }
  else if (frame->format == XINE_IMGFMT_VDPAU) {
    surface = frame->vdpau_accel_data.surface;
    mix_w = frame->width;
    mix_h = frame->height;
    chroma = (frame->vo_frame.flags & VO_CHROMA_422) ? VDP_CHROMA_TYPE_422 : VDP_CHROMA_TYPE_420;
    color_standard = frame->vdpau_accel_data.color_standard;
  }
  else {
    /* unknown format */
    fprintf(stderr, "vo_vdpau: got an unknown image -------------\n" );
    frame->vo_frame.free( &frame->vo_frame );
    return;
  }

  if ( (mix_w != this->video_mixer_width) || (mix_h != this->video_mixer_height) || (chroma != this->video_mixer_chroma)) {
    vdpau_release_back_frames( this_gen ); /* empty past frames array */
    lprintf("vo_vdpau: recreate mixer to match frames: width=%d, height=%d, chroma=%d\n", mix_w, mix_h, chroma);
    vdp_video_mixer_destroy( this->video_mixer );
    this->video_mixer = VDP_INVALID_HANDLE;
    VdpVideoMixerFeature features[15];
    int features_count = 0;
    if ( this->noise_reduction_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION;
      ++features_count;
    }
    if ( this->sharpness_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_SHARPNESS;
      ++features_count;
    }
    if ( this->temporal_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
      ++features_count;
    }
    if ( this->temporal_spatial_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
      ++features_count;
    }
    if ( this->inverse_telecine_is_supported ) {
      features[features_count] = VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
      ++features_count;
    }
    int i;
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
    for ( i=0; i<this->scaling_level_max; ++i ) {
     features[features_count] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
      ++features_count;
    }
#endif
    VdpVideoMixerParameter params[] = { VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH, VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
          VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE, VDP_VIDEO_MIXER_PARAMETER_LAYERS };
    int num_layers = 3;
    void const *param_values[] = { &mix_w, &mix_h, &chroma, &num_layers };
    vdp_video_mixer_create( vdp_device, features_count, features, 4, params, param_values, &this->video_mixer );
    this->video_mixer_chroma = chroma;
    this->video_mixer_width = mix_w;
    this->video_mixer_height = mix_h;
    vdpau_set_deinterlace( this_gen );
    vdpau_set_scaling_level( this_gen );
    vdpau_set_inverse_telecine( this_gen );
    vdpau_update_noise( this );
    vdpau_update_sharpness( this );
    this->color_standard = color_standard;
    vdpau_update_csc( this );
    vdpau_update_skip_chroma( this );
    vdpau_update_background( this );
  }

  if (color_standard != this->color_standard) {
    lprintf("vo_vdpau: update color_standard: %d\n", color_standard);
    this->color_standard = color_standard;
    vdpau_update_csc( this );
  }

  VdpRect vid_source, out_dest, vid_dest;

  vdpau_check_output_size( this_gen );
  vid_source.x0 = this->sc.displayed_xoffset; vid_source.y0 = this->sc.displayed_yoffset;
  vid_source.x1 = this->sc.displayed_width+this->sc.displayed_xoffset; vid_source.y1 = this->sc.displayed_height+this->sc.displayed_yoffset;
  out_dest.x0 = out_dest.y0 = 0;
  out_dest.x1 = this->sc.gui_width; out_dest.y1 = this->sc.gui_height;
  vid_dest.x0 = this->sc.output_xoffset; vid_dest.y0 = this->sc.output_yoffset;
  vid_dest.x1 = this->sc.output_xoffset+this->sc.output_width; vid_dest.y1 = this->sc.output_yoffset+this->sc.output_height;

  stream_speed = frame->vo_frame.stream ? xine_get_param(frame->vo_frame.stream, XINE_PARAM_FINE_SPEED) : 0;

  VdpTime last_time;

  if ( this->init_queue>1 )
    vdp_queue_block( vdp_queue, this->output_surface[this->current_output_surface], &last_time );

  uint32_t layer_count;
  VdpLayer layer[3];
  VdpRect unscaledsrc;
  if ( this->has_overlay ) {
    layer_count = 2;
    layer[0].struct_version = VDP_LAYER_VERSION; layer[0].source_surface = this->overlay_output; layer[0].source_rect = &vid_source; layer[0].destination_rect = &vid_dest;
    unscaledsrc.x0 = 0; unscaledsrc.y0 = 0; unscaledsrc.x1 = this->overlay_unscaled_width; unscaledsrc.y1 = this->overlay_unscaled_height;
    layer[1].struct_version = VDP_LAYER_VERSION; layer[1].source_surface = this->overlay_unscaled; layer[1].source_rect = &unscaledsrc; layer[1].destination_rect = &unscaledsrc;
  }
  else {
    layer_count = 0;
  }

  VdpRect argb_dest;
  VdpRect argb_rect = { 0, 0, this->argb_overlay_width, this->argb_overlay_height };
  if( this->has_argb_overlay ) {
    layer_count++;
    memcpy(&argb_dest, &vid_dest, sizeof (vid_dest));
    layer[layer_count-1].destination_rect = &argb_dest;
    layer[layer_count-1].source_rect = &argb_rect;
    layer[layer_count-1].source_surface = this->argb_overlay;
    layer[layer_count-1].struct_version = VDP_LAYER_VERSION;
    /* recalculate video destination window to match osd's specified video window */
    if (this->video_window_width > 0 && this->video_window_height > 0) {
      VdpRect win_rect = { this->video_window_x, this->video_window_y, this->video_window_x + this->video_window_width, this->video_window_y + this->video_window_height };
      vid_dest.x0 = ((win_rect.x0 - argb_rect.x0) * (argb_dest.x1 - argb_dest.x0) + argb_dest.x0 * (argb_rect.x1 - argb_rect.x0)) / (argb_rect.x1 - argb_rect.x0);
      vid_dest.y0 = ((win_rect.y0 - argb_rect.y0) * (argb_dest.y1 - argb_dest.y0) + argb_dest.y0 * (argb_rect.y1 - argb_rect.y0)) / (argb_rect.y1 - argb_rect.y0);
      vid_dest.x1 = ((win_rect.x1 - argb_rect.x0) * (argb_dest.x1 - argb_dest.x0) + argb_dest.x0 * (argb_rect.x1 - argb_rect.x0)) / (argb_rect.x1 - argb_rect.x0);
      vid_dest.y1 = ((win_rect.y1 - argb_rect.y0) * (argb_dest.y1 - argb_dest.y0) + argb_dest.y0 * (argb_rect.y1 - argb_rect.y0)) / (argb_rect.y1 - argb_rect.y0);
    }
  }

  /* try to get frame duration from previous img->pts when frame->duration is 0 */
  int frame_duration = frame->vo_frame.duration;
  if ( !frame_duration && this->back_frame[0] ) {
    int duration = frame->vo_frame.pts - this->back_frame[0]->vo_frame.pts;
    if ( duration>0 && duration<4000 )
      frame_duration = duration;
  }
  int non_progressive = (this->honor_progressive && !frame->vo_frame.progressive_frame) || !this->honor_progressive;

#ifdef LOCKDISPLAY
  XLockDisplay( this->display );
#endif

  if ( frame->format==XINE_IMGFMT_VDPAU && this->deinterlace && non_progressive && !(frame->vo_frame.flags & VO_STILL_IMAGE) && frame_duration>2500 ) {
    VdpTime current_time = 0;
    VdpVideoSurface past[2];
    VdpVideoSurface future[1];
    VdpVideoMixerPictureStructure picture_structure;

    past[1] = past[0] = (this->back_frame[0] && (this->back_frame[0]->format==XINE_IMGFMT_VDPAU)) ? this->back_frame[0]->vdpau_accel_data.surface : VDP_INVALID_HANDLE;
    future[0] = surface;
    picture_structure = ( frame->vo_frame.top_field_first ) ? VDP_VIDEO_MIXER_PICTURE_STRUCTURE_TOP_FIELD : VDP_VIDEO_MIXER_PICTURE_STRUCTURE_BOTTOM_FIELD;

    st = vdp_video_mixer_render( this->video_mixer, VDP_INVALID_HANDLE, 0, picture_structure,
                               2, past, surface, 1, future, &vid_source, this->output_surface[this->current_output_surface], &out_dest, &vid_dest, layer_count, layer_count?layer:NULL );
    if ( st != VDP_STATUS_OK )
      fprintf(stderr, "vo_vdpau: vdp_video_mixer_render error : %s\n", vdp_get_error_string( st ) );

    vdp_queue_get_time( vdp_queue, &current_time );
    vdp_queue_display( vdp_queue, this->output_surface[this->current_output_surface], 0, 0, 0 ); /* display _now_ */
    vdpau_shift_queue( this_gen );

    int dm = this->deinterlacers_method[this->deinterlace_method];
    if ( (dm != DEINT_HALF_TEMPORAL) && (dm != DEINT_HALF_TEMPORAL_SPATIAL) && frame->vo_frame.future_frame ) {  /* process second field */
      if ( this->init_queue>1 ) {
#ifdef LOCKDISPLAY
        XUnlockDisplay(this->display);
#endif
        vdp_queue_block( vdp_queue, this->output_surface[this->current_output_surface], &last_time );
#ifdef LOCKDISPLAY
        XLockDisplay(this->display);
#endif
      }

      vdpau_check_output_size( this_gen );

      picture_structure = ( frame->vo_frame.top_field_first ) ? VDP_VIDEO_MIXER_PICTURE_STRUCTURE_BOTTOM_FIELD : VDP_VIDEO_MIXER_PICTURE_STRUCTURE_TOP_FIELD;
      past[0] = surface;
      if ( frame->vo_frame.future_frame!=NULL && ((vdpau_frame_t*)(frame->vo_frame.future_frame))->format==XINE_IMGFMT_VDPAU )
        future[0] = ((vdpau_frame_t*)(frame->vo_frame.future_frame))->vdpau_accel_data.surface;
      else
        future[0] = VDP_INVALID_HANDLE;

      st = vdp_video_mixer_render( this->video_mixer, VDP_INVALID_HANDLE, 0, picture_structure,
                               2, past, surface, 1, future, &vid_source, this->output_surface[this->current_output_surface], &out_dest, &vid_dest, layer_count, layer_count?layer:NULL );
      if ( st != VDP_STATUS_OK )
        fprintf(stderr, "vo_vdpau: vdp_video_mixer_render error : %s\n", vdp_get_error_string( st ) );

      if ( stream_speed > 0 )
        current_time += frame->vo_frame.duration * 1000000ull * XINE_FINE_SPEED_NORMAL / (180 * stream_speed);

      vdp_queue_display( vdp_queue, this->output_surface[this->current_output_surface], 0, 0, current_time );
      vdpau_shift_queue( this_gen );
    }
  }
  else {
    if ( frame->vo_frame.flags & VO_STILL_IMAGE )
      lprintf( "vo_vdpau: VO_STILL_IMAGE\n");
    st = vdp_video_mixer_render( this->video_mixer, VDP_INVALID_HANDLE, 0, VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME,
                               0, 0, surface, 0, 0, &vid_source, this->output_surface[this->current_output_surface], &out_dest, &vid_dest, layer_count, layer_count?layer:NULL );
    if ( st != VDP_STATUS_OK )
      fprintf(stderr, "vo_vdpau: vdp_video_mixer_render error : %s\n", vdp_get_error_string( st ) );

    vdp_queue_display( vdp_queue, this->output_surface[this->current_output_surface], 0, 0, 0 );
    vdpau_shift_queue( this_gen );
  }

#ifdef LOCKDISPLAY
  XUnlockDisplay( this->display );
#endif

  if ( stream_speed ) 
    vdpau_backup_frame( this_gen, frame_gen );
  else /* do not release past frame if paused, it will be used for redrawing */
    frame->vo_frame.free( &frame->vo_frame );
}



static int vdpau_get_property (vo_driver_t *this_gen, int property)
{
  vdpau_driver_t *this = (vdpau_driver_t*)this_gen;

  switch (property) {
    case VO_PROP_MAX_NUM_FRAMES:
      return 30;
    case VO_PROP_WINDOW_WIDTH:
      return this->sc.gui_width;
    case VO_PROP_WINDOW_HEIGHT:
      return this->sc.gui_height;
    case VO_PROP_OUTPUT_WIDTH:
      return this->sc.output_width;
    case VO_PROP_OUTPUT_HEIGHT:
      return this->sc.output_height;
    case VO_PROP_OUTPUT_XOFFSET:
      return this->sc.output_xoffset;
    case VO_PROP_OUTPUT_YOFFSET:
      return this->sc.output_yoffset;
    case VO_PROP_HUE:
      return this->hue;
    case VO_PROP_SATURATION:
      return this->saturation;
    case VO_PROP_CONTRAST:
      return this->contrast;
    case VO_PROP_BRIGHTNESS:
      return this->brightness;
    case VO_PROP_SHARPNESS:
      return this->sharpness;
    case VO_PROP_NOISE_REDUCTION:
      return this->noise;
    case VO_PROP_ZOOM_X:
      return this->zoom_x;
    case VO_PROP_ZOOM_Y:
      return this->zoom_y;
    case VO_PROP_ASPECT_RATIO:
      return this->sc.user_ratio;
  }

  return -1;
}



static int vdpau_set_property (vo_driver_t *this_gen, int property, int value)
{
  vdpau_driver_t *this = (vdpau_driver_t*)this_gen;

  fprintf(stderr,"vdpau_set_property: property=%d, value=%d\n", property, value );

  switch (property) {
    case VO_PROP_INTERLACED:
      this->deinterlace = value;
      vdpau_set_deinterlace( this_gen );
      break;
    case VO_PROP_ZOOM_X:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_x = value;
        this->sc.zoom_factor_x = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    case VO_PROP_ZOOM_Y:
      if ((value >= XINE_VO_ZOOM_MIN) && (value <= XINE_VO_ZOOM_MAX)) {
        this->zoom_y = value;
        this->sc.zoom_factor_y = (double)value / (double)XINE_VO_ZOOM_STEP;
        _x_vo_scale_compute_ideal_size( &this->sc );
        this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      }
      break;
    case VO_PROP_ASPECT_RATIO:
      if ( value>=XINE_VO_ASPECT_NUM_RATIOS )
        value = XINE_VO_ASPECT_AUTO;
      this->sc.user_ratio = value;
      this->sc.force_redraw = 1;    /* trigger re-calc of output size */
      break;
    case VO_PROP_HUE: this->hue = value; vdpau_update_csc( this ); break;
    case VO_PROP_SATURATION: this->saturation = value; vdpau_update_csc( this ); break;
    case VO_PROP_CONTRAST: this->contrast = value; vdpau_update_csc( this ); break;
    case VO_PROP_BRIGHTNESS: this->brightness = value; vdpau_update_csc( this ); break;
    case VO_PROP_SHARPNESS: this->sharpness = value; vdpau_update_sharpness( this ); break;
    case VO_PROP_NOISE_REDUCTION: this->noise = value; vdpau_update_noise( this ); break;
  }

  return value;
}



static void vdpau_get_property_min_max (vo_driver_t *this_gen, int property, int *min, int *max)
{
  switch ( property ) {
    case VO_PROP_HUE:
      *max = 314; *min = -314; break;
    case VO_PROP_SATURATION:
      *max = 1000; *min = 0; break;
    case VO_PROP_CONTRAST:
      *max = 1000; *min = 0; break;
    case VO_PROP_BRIGHTNESS:
      *max = 100; *min = -100; break;
    case VO_PROP_SHARPNESS:
      *max = 100; *min = -100; break;
    case VO_PROP_NOISE_REDUCTION:
      *max = 100; *min = 0; break;
    default:
      *max = 0; *min = 0;
  }
}



static int vdpau_gui_data_exchange (vo_driver_t *this_gen, int data_type, void *data)
{
  vdpau_driver_t *this = (vdpau_driver_t*)this_gen;

  switch (data_type) {
#ifndef XINE_DISABLE_DEPRECATED_FEATURES
    case XINE_GUI_SEND_COMPLETION_EVENT:
      break;
#endif

    case XINE_GUI_SEND_EXPOSE_EVENT: {
      if ( this->init_queue ) {
#ifdef LOCKDISPLAY
        XLockDisplay( this->display );
#endif
        int previous = this->current_output_surface - 1;
        if ( previous < 0 )
          previous = NOUTPUTSURFACE - 1;
        vdp_queue_display( vdp_queue, this->output_surface[previous], 0, 0, 0 );
#ifdef LOCKDISPLAY
        XUnlockDisplay( this->display );
#endif
      }
      break;
    }

    case XINE_GUI_SEND_DRAWABLE_CHANGED: {
      VdpStatus st;
#ifdef LOCKDISPLAY
      XLockDisplay( this->display );
#endif
      this->drawable = (Drawable) data;
      vdp_queue_destroy( vdp_queue );
      vdp_queue_target_destroy( vdp_queue_target );
      st = vdp_queue_target_create_x11( vdp_device, this->drawable, &vdp_queue_target );
      if ( st != VDP_STATUS_OK ) {
        fprintf(stderr, "vo_vdpau: FATAL !! Can't recreate presentation queue target after drawable change !!\n" );
#ifdef LOCKDISPLAY
        XUnlockDisplay( this->display );
#endif
        break;
      }
      st = vdp_queue_create( vdp_device, vdp_queue_target, &vdp_queue );
      if ( st != VDP_STATUS_OK ) {
        fprintf(stderr, "vo_vdpau: FATAL !! Can't recreate presentation queue after drawable change !!\n" );
#ifdef LOCKDISPLAY
        XUnlockDisplay( this->display );
#endif
        break;
      }
      vdp_queue_set_background_color( vdp_queue, &this->back_color );
#ifdef LOCKDISPLAY
      XUnlockDisplay( this->display );
#endif
      this->sc.force_redraw = 1;
      break;
    }

    case XINE_GUI_SEND_TRANSLATE_GUI_TO_VIDEO: {
      int x1, y1, x2, y2;
      x11_rectangle_t *rect = data;

      _x_vo_scale_translate_gui2video(&this->sc, rect->x, rect->y, &x1, &y1);
      _x_vo_scale_translate_gui2video(&this->sc, rect->x + rect->w, rect->y + rect->h, &x2, &y2);
      rect->x = x1;
      rect->y = y1;
      rect->w = x2-x1;
      rect->h = y2-y1;
      break;
    }

    default:
      return -1;
  }

  return 0;
}



static uint32_t vdpau_get_capabilities (vo_driver_t *this_gen)
{
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;

  return this->capabilities;
}



static void vdpau_dispose (vo_driver_t *this_gen)
{
  vdpau_driver_t *this = (vdpau_driver_t *) this_gen;
  int i;

  this->ovl_yuv2rgb->dispose(this->ovl_yuv2rgb);
  this->yuv2rgb_factory->dispose (this->yuv2rgb_factory);

  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i ) {
    if ( this->overlays[i].ovl_bitmap != VDP_INVALID_HANDLE )
      vdp_bitmap_destroy( this->overlays[i].ovl_bitmap );
  }

  if ( this->video_mixer!=VDP_INVALID_HANDLE )
    vdp_video_mixer_destroy( this->video_mixer );
  if ( this->soft_surface != VDP_INVALID_HANDLE )
    vdp_video_surface_destroy( this->soft_surface );

  if ( vdp_output_surface_destroy ) {
    if (this->argb_overlay != VDP_INVALID_HANDLE)
      vdp_output_surface_destroy(this->argb_overlay);
    if ( this->overlay_unscaled!=VDP_INVALID_HANDLE )
      vdp_output_surface_destroy( this->overlay_unscaled );
    if ( this->overlay_output!=VDP_INVALID_HANDLE )
      vdp_output_surface_destroy( this->overlay_output );
    for ( i=0; i<NOUTPUTSURFACE; ++i ) {
      if ( this->output_surface[i]!=VDP_INVALID_HANDLE )
        vdp_output_surface_destroy( this->output_surface[i] );
	}
  }

  if ( vdp_queue != VDP_INVALID_HANDLE )
    vdp_queue_destroy( vdp_queue );
  if ( vdp_queue_target != VDP_INVALID_HANDLE )
    vdp_queue_target_destroy( vdp_queue_target );

  for ( i=0; i<NUM_FRAMES_BACK; i++ )
    if ( this->back_frame[i] )
      this->back_frame[i]->vo_frame.dispose( &this->back_frame[i]->vo_frame );

  if ( (vdp_device != VDP_INVALID_HANDLE) && vdp_device_destroy )
    vdp_device_destroy( vdp_device );

  free (this);
}



static int vdpau_reinit_error( VdpStatus st, const char *msg )
{
  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vo_vdpau: %s : %s\n", msg, vdp_get_error_string( st ) );
    return 1;
  }
  return 0;
}



static void vdpau_reinit( vo_driver_t *this_gen )
{
  fprintf(stderr,"vo_vdpau: VDPAU was pre-empted. Reinit.\n");
  vdpau_driver_t *this = (vdpau_driver_t *)this_gen;

#ifdef LOCKDISPLAY
  XLockDisplay(guarded_display);
#endif
  vdpau_release_back_frames(this_gen);

  VdpStatus st = vdp_device_create_x11( this->display, this->screen, &vdp_device, &vdp_get_proc_address );

  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vo_vdpau: Can't create vdp device : " );
    if ( st == VDP_STATUS_NO_IMPLEMENTATION )
      fprintf(stderr, "No vdpau implementation.\n" );
    else
      fprintf(stderr, "unsupported GPU?\n" );
    return;
  }

  st = vdp_queue_target_create_x11( vdp_device, this->drawable, &vdp_queue_target );
  if ( vdpau_reinit_error( st, "Can't create presentation queue target !!" ) )
    return;
  st = vdp_queue_create( vdp_device, vdp_queue_target, &vdp_queue );
  if ( vdpau_reinit_error( st, "Can't create presentation queue !!" ) )
    return;
  vdp_queue_set_background_color( vdp_queue, &this->back_color );


  VdpChromaType chroma = VDP_CHROMA_TYPE_420;
  st = orig_vdp_video_surface_create( vdp_device, chroma, this->soft_surface_width, this->soft_surface_height, &this->soft_surface );
  if ( vdpau_reinit_error( st, "Can't create video surface !!" ) )
    return;

  this->current_output_surface = 0;
  this->init_queue = 0;
  int i;
  for ( i=0; i<NOUTPUTSURFACE; ++i ) {
    st = vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->output_surface_width[i], this->output_surface_height[i], &this->output_surface[i] );
    if ( vdpau_reinit_error( st, "Can't create output surface !!" ) ) {
      int j;
      for ( j=0; j<i; ++j )
        vdp_output_surface_destroy( this->output_surface[j] );
      vdp_video_surface_destroy( this->soft_surface );
      return;
    }
  }

  /* osd overlays need to be recreated */
  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i ) {
    this->overlays[i].ovl_bitmap = VDP_INVALID_HANDLE;
    this->overlays[i].bitmap_width = 0;
    this->overlays[i].bitmap_height = 0;
  }
  this->overlay_output = VDP_INVALID_HANDLE;
  this->overlay_output_width = this->overlay_output_height = 0;
  this->overlay_unscaled = VDP_INVALID_HANDLE;
  this->overlay_unscaled_width = this->overlay_unscaled_height = 0;
  this->ovl_changed = 0;
  this->has_overlay = 0;
  this->has_unscaled = 0;

  this->argb_overlay = VDP_INVALID_HANDLE;
  this->argb_overlay_width = this->argb_overlay_height = 0;
  this->has_argb_overlay = 0;

  VdpVideoMixerFeature features[15];
  int features_count = 0;
  if ( this->noise_reduction_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION;
    ++features_count;
  }
  if ( this->sharpness_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_SHARPNESS;
	++features_count;
  }
  if ( this->temporal_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    ++features_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    ++features_count;
  }
  if ( this->inverse_telecine_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
    ++features_count;
  }
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  for ( i=0; i<this->scaling_level_max; ++i ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
    ++features_count;
  }
#endif
  VdpVideoMixerParameter params[] = { VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH, VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT, VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE, VDP_VIDEO_MIXER_PARAMETER_LAYERS };
  int num_layers = 3;
  void const *param_values[] = { &this->video_mixer_width, &this->video_mixer_height, &chroma, &num_layers };
  st = vdp_video_mixer_create( vdp_device, features_count, features, 4, params, param_values, &this->video_mixer );
  if ( vdpau_reinit_error( st, "Can't create video mixer !!" ) ) {
    orig_vdp_video_surface_destroy( this->soft_surface );
    for ( i=0; i<NOUTPUTSURFACE; ++i )
      vdp_output_surface_destroy( this->output_surface[i] );
    return;
  }
  this->video_mixer_chroma = chroma;
  vdpau_set_deinterlace( this_gen );
  vdpau_set_scaling_level( this_gen );
  vdpau_set_inverse_telecine( this_gen );
  vdpau_update_noise( this );
  vdpau_update_sharpness( this );
  vdpau_update_csc( this );
  vdpau_update_skip_chroma( this );
  vdpau_update_background( this );

  vdp_preemption_callback_register(vdp_device, &vdp_preemption_callback, (void*)this);

  this->vdp_runtime_nr++;
  this->reinit_needed = 0;
#ifdef LOCKDISPLAY
  XUnlockDisplay(guarded_display);
#endif
  fprintf(stderr,"vo_vdpau: Reinit done.\n");
}



static void vdp_preemption_callback(VdpDevice device, void *context)
{
  fprintf(stderr,"vo_vdpau: VDPAU preemption callback\n");
  vdpau_driver_t *this = (vdpau_driver_t *)context;
  this->reinit_needed = 1;
}



static int vdpau_init_error( VdpStatus st, const char *msg, vo_driver_t *driver, int error_string )
{
  if ( st != VDP_STATUS_OK ) {
    if ( error_string )
      fprintf(stderr, "vo_vdpau: %s : %s\n", msg, vdp_get_error_string( st ) );
    else
      fprintf(stderr, "vo_vdpau: %s\n", msg );
    vdpau_dispose( driver );
    return 1;
  }
  return 0;
}



static vo_driver_t *vdpau_open_plugin (video_driver_class_t *class_gen, const void *visual_gen)
{
  vdpau_class_t       *class   = (vdpau_class_t *) class_gen;
  x11_visual_t        *visual  = (x11_visual_t *) visual_gen;
  vdpau_driver_t      *this;
  config_values_t      *config  = class->xine->config;
  int i;

  this = (vdpau_driver_t *) calloc(1, sizeof(vdpau_driver_t));

  if (!this)
    return NULL;

  guarded_display     = visual->display;
  this->display       = visual->display;
  this->screen        = visual->screen;
  this->drawable      = visual->d;

  _x_vo_scale_init(&this->sc, 1, 0, config);
  this->sc.frame_output_cb  = visual->frame_output_cb;
  this->sc.dest_size_cb     = visual->dest_size_cb;
  this->sc.user_data        = visual->user_data;
  this->sc.user_ratio       = XINE_VO_ASPECT_AUTO;

  this->zoom_x              = 100;
  this->zoom_y              = 100;

  this->xine                    = class->xine;
  this->config                  = config;

  this->vo_driver.get_capabilities     = vdpau_get_capabilities;
  this->vo_driver.alloc_frame          = vdpau_alloc_frame;
  this->vo_driver.update_frame_format  = vdpau_update_frame_format;
  this->vo_driver.overlay_begin        = vdpau_overlay_begin;
  this->vo_driver.overlay_blend        = vdpau_overlay_blend;
  this->vo_driver.overlay_end          = vdpau_overlay_end;
  this->vo_driver.display_frame        = vdpau_display_frame;
  this->vo_driver.get_property         = vdpau_get_property;
  this->vo_driver.set_property         = vdpau_set_property;
  this->vo_driver.get_property_min_max = vdpau_get_property_min_max;
  this->vo_driver.gui_data_exchange    = vdpau_gui_data_exchange;
  this->vo_driver.dispose              = vdpau_dispose;
  this->vo_driver.redraw_needed        = vdpau_redraw_needed;

  this->surface_cleared_nr = 0;

  this->video_mixer = VDP_INVALID_HANDLE;
  for ( i=0; i<NOUTPUTSURFACE; ++i )
    this->output_surface[i] = VDP_INVALID_HANDLE;
  this->soft_surface = VDP_INVALID_HANDLE;
  vdp_queue = VDP_INVALID_HANDLE;
  vdp_queue_target = VDP_INVALID_HANDLE;
  vdp_device = VDP_INVALID_HANDLE;

  vdp_output_surface_destroy = NULL;
  vdp_device_destroy = NULL;

  this->sharpness_is_supported = 0;
  this->noise_reduction_is_supported = 0;
  this->temporal_is_supported = 0;
  this->temporal_spatial_is_supported = 0;
  this->inverse_telecine_is_supported = 0;
  this->skip_chroma_is_supported = 0;
  this->background_is_supported = 0;

  for ( i=0; i<XINE_VORAW_MAX_OVL; ++i ) {
    this->overlays[i].ovl_w = this->overlays[i].ovl_h = 0;
    this->overlays[i].bitmap_width = this->overlays[i].bitmap_height = 0;
    this->overlays[i].ovl_bitmap = VDP_INVALID_HANDLE;
    this->overlays[i].ovl_x = this->overlays[i].ovl_y = 0;
  }
  this->overlay_output = VDP_INVALID_HANDLE;
  this->overlay_output_width = this->overlay_output_height = 0;
  this->overlay_unscaled = VDP_INVALID_HANDLE;
  this->overlay_unscaled_width = this->overlay_unscaled_height = 0;
  this->ovl_changed = 0;
  this->has_overlay = 0;
  this->has_unscaled = 0;

  this->argb_overlay = VDP_INVALID_HANDLE;
  this->argb_overlay_width = this->argb_overlay_height = 0;
  this->has_argb_overlay = 0;

  /*  overlay converter */
  this->yuv2rgb_factory = yuv2rgb_factory_init (MODE_24_BGR, 0, NULL);
  this->ovl_yuv2rgb = this->yuv2rgb_factory->create_converter( this->yuv2rgb_factory );

  VdpStatus st = vdp_device_create_x11( visual->display, visual->screen, &vdp_device, &vdp_get_proc_address );
  if ( st != VDP_STATUS_OK ) {
    fprintf(stderr, "vo_vdpau: Can't create vdp device : " );
    if ( st == VDP_STATUS_NO_IMPLEMENTATION )
      fprintf(stderr, "No vdpau implementation.\n" );
    else
      fprintf(stderr, "unsupported GPU?\n" );
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GET_ERROR_STRING , (void*)&vdp_get_error_string );
  if ( vdpau_init_error( st, "Can't get GET_ERROR_STRING proc address !!", &this->vo_driver, 0 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GET_API_VERSION , (void*)&vdp_get_api_version );
  if ( vdpau_init_error( st, "Can't get GET_API_VERSION proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  uint32_t tmp;
  vdp_get_api_version( &tmp );
  fprintf(stderr, "vo_vdpau: vdpau API version : %d\n", tmp );
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GET_INFORMATION_STRING , (void*)&vdp_get_information_string );
  if ( vdpau_init_error( st, "Can't get GET_INFORMATION_STRING proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  const char *s;
  st = vdp_get_information_string( &s );
  fprintf(stderr, "vo_vdpau: vdpau implementation description : %s\n", s );
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES , (void*)&vdp_video_surface_query_get_put_bits_ycbcr_capabilities );
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_QUERY_GET_PUT_BITS_Y_CB_CR_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  VdpBool ok;
  st = vdp_video_surface_query_get_put_bits_ycbcr_capabilities( vdp_device, VDP_CHROMA_TYPE_422, VDP_YCBCR_FORMAT_YUYV, &ok );
  if ( vdpau_init_error( st, "Failed to check vdpau yuy2 capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: VideoSurface doesn't support yuy2, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_video_surface_query_get_put_bits_ycbcr_capabilities( vdp_device, VDP_CHROMA_TYPE_420, VDP_YCBCR_FORMAT_YV12, &ok );
  if ( vdpau_init_error( st, "Failed to check vdpau yv12 capability", &this->vo_driver, 1 ) )
    return NULL;
  if ( !ok ) {
    fprintf(stderr, "vo_vdpau: VideoSurface doesn't support yv12, sorry.\n");
    vdpau_dispose( &this->vo_driver );
    return NULL;
  }
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DEVICE_DESTROY , (void*)&vdp_device_destroy );
  if ( vdpau_init_error( st, "Can't get DEVICE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_CREATE , (void*)&orig_vdp_video_surface_create ); vdp_video_surface_create = guarded_vdp_video_surface_create;
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_DESTROY , (void*)&orig_vdp_video_surface_destroy ); vdp_video_surface_destroy = guarded_vdp_video_surface_destroy;
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_PUT_BITS_Y_CB_CR , (void*)&vdp_video_surface_putbits_ycbcr );
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_PUT_BITS_Y_CB_CR proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_SURFACE_GET_BITS_Y_CB_CR , (void*)&vdp_video_surface_getbits_ycbcr );
  if ( vdpau_init_error( st, "Can't get VIDEO_SURFACE_GET_BITS_Y_CB_CR proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_CREATE , (void*)&vdp_output_surface_create );
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY , (void*)&vdp_output_surface_destroy );
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_RENDER_BITMAP_SURFACE , (void*)&vdp_output_surface_render_bitmap_surface );
  if ( vdpau_init_error( st, "Can't get OUTPUT_SURFACE_RENDER_BITMAP_SURFACE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE , (void*)&vdp_output_surface_put_bits );
  if ( vdpau_init_error( st, "Can't get VDP_FUNC_ID_OUTPUT_SURFACE_PUT_BITS_NATIVE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_CREATE , (void*)&vdp_video_mixer_create );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_DESTROY , (void*)&vdp_video_mixer_destroy );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_RENDER , (void*)&vdp_video_mixer_render );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_RENDER proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES , (void*)&vdp_video_mixer_set_attribute_values );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_SET_ATTRIBUTE_VALUES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES , (void*)&vdp_video_mixer_set_feature_enables );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_SET_FEATURE_ENABLES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_ENABLES , (void*)&vdp_video_mixer_get_feature_enables );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_GET_FEATURE_ENABLES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_FEATURE_SUPPORT , (void*)&vdp_video_mixer_query_feature_support );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_FEATURE_SUPPORT proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_SUPPORT , (void*)&vdp_video_mixer_query_parameter_support );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_PARAMETER_SUPPORT proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_SUPPORT , (void*)&vdp_video_mixer_query_attribute_support );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_ATTRIBUTE_SUPPORT proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_PARAMETER_VALUE_RANGE , (void*)&vdp_video_mixer_query_parameter_value_range );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_PARAMETER_VALUE_RANGE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_VIDEO_MIXER_QUERY_ATTRIBUTE_VALUE_RANGE , (void*)&vdp_video_mixer_query_attribute_value_range );
  if ( vdpau_init_error( st, "Can't get VIDEO_MIXER_QUERY_ATTRIBUTE_VALUE_RANGE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_GENERATE_CSC_MATRIX , (void*)&vdp_generate_csc_matrix );
  if ( vdpau_init_error( st, "Can't get GENERATE_CSC_MATRIX proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11 , (void*)&vdp_queue_target_create_x11 );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_TARGET_CREATE_X11 proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY , (void*)&vdp_queue_target_destroy );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_TARGET_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE , (void*)&vdp_queue_create );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY , (void*)&vdp_queue_destroy );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY , (void*)&vdp_queue_display );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_DISPLAY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE , (void*)&vdp_queue_block );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_SET_BACKGROUND_COLOR , (void*)&vdp_queue_set_background_color );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_SET_BACKGROUND_COLOR proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_GET_TIME , (void*)&vdp_queue_get_time );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_GET_TIME proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS , (void*)&vdp_queue_query_surface_status );
  if ( vdpau_init_error( st, "Can't get PRESENTATION_QUEUE_QUERY_SURFACE_STATUS proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES , (void*)&vdp_decoder_query_capabilities );
  if ( vdpau_init_error( st, "Can't get DECODER_QUERY_CAPABILITIES proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_CREATE , (void*)&orig_vdp_decoder_create ); vdp_decoder_create = guarded_vdp_decoder_create;
  if ( vdpau_init_error( st, "Can't get DECODER_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_DESTROY , (void*)&orig_vdp_decoder_destroy ); vdp_decoder_destroy = guarded_vdp_decoder_destroy;
  if ( vdpau_init_error( st, "Can't get DECODER_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_DECODER_RENDER , (void*)&orig_vdp_decoder_render ); vdp_decoder_render = guarded_vdp_decoder_render;
  if ( vdpau_init_error( st, "Can't get DECODER_RENDER proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_BITMAP_SURFACE_CREATE , (void*)&vdp_bitmap_create );
  if ( vdpau_init_error( st, "Can't get BITMAP_SURFACE_CREATE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_BITMAP_SURFACE_DESTROY , (void*)&vdp_bitmap_destroy );
  if ( vdpau_init_error( st, "Can't get BITMAP_SURFACE_DESTROY proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_BITMAP_SURFACE_PUT_BITS_NATIVE , (void*)&vdp_bitmap_put_bits );
  if ( vdpau_init_error( st, "Can't get BITMAP_SURFACE_PUT_BITS_NATIVE proc address !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_get_proc_address( vdp_device, VDP_FUNC_ID_PREEMPTION_CALLBACK_REGISTER, (void*)&vdp_preemption_callback_register );
  if ( vdpau_init_error( st, "Can't get PREEMPTION_CALLBACK_REGISTER proc address !!", &this->vo_driver, 1 ) )
    return NULL;

  st = vdp_preemption_callback_register(vdp_device, &vdp_preemption_callback, (void*)this);
  if ( vdpau_init_error( st, "Can't register preemption callback !!", &this->vo_driver, 1 ) )
    return NULL;

  st = vdp_queue_target_create_x11( vdp_device, this->drawable, &vdp_queue_target );
  if ( vdpau_init_error( st, "Can't create presentation queue target !!", &this->vo_driver, 1 ) )
    return NULL;
  st = vdp_queue_create( vdp_device, vdp_queue_target, &vdp_queue );
  if ( vdpau_init_error( st, "Can't create presentation queue !!", &this->vo_driver, 1 ) )
    return NULL;

  /* choose almost black as backcolor for color keying */
  this->back_color.red = 0.02;
  this->back_color.green = 0.01;
  this->back_color.blue = 0.03;
  this->back_color.alpha = 1;
  vdp_queue_set_background_color( vdp_queue, &this->back_color );

  this->soft_surface_width = 320;
  this->soft_surface_height = 240;
  this->soft_surface_format = XINE_IMGFMT_YV12;
  VdpChromaType chroma = VDP_CHROMA_TYPE_420;
  st = vdp_video_surface_create( vdp_device, chroma, this->soft_surface_width, this->soft_surface_height, &this->soft_surface );
  if ( vdpau_init_error( st, "Can't create video surface !!", &this->vo_driver, 1 ) )
    return NULL;

  for ( i=0; i<NOUTPUTSURFACE; ++i ) {
    this->output_surface_width[i] = 320;
    this->output_surface_height[i] = 240;
  }
  this->current_output_surface = 0;
  this->init_queue = 0;
  for ( i=0; i<NOUTPUTSURFACE; ++i ) {
    st = vdp_output_surface_create( vdp_device, VDP_RGBA_FORMAT_B8G8R8A8, this->output_surface_width[i], this->output_surface_height[i], &this->output_surface[i] );
    if ( vdpau_init_error( st, "Can't create output surface !!", &this->vo_driver, 1 ) ) {
      int j;
      for ( j=0; j<i; ++j )
        vdp_output_surface_destroy( this->output_surface[j] );
      vdp_video_surface_destroy( this->soft_surface );
      return NULL;
    }
  }

  this->scaling_level_max = this->scaling_level_current = 0;
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  VdpBool hqscaling;
  for ( i=0; i<9; ++i ) {
    st = vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i, &hqscaling );
    if ( ( st != VDP_STATUS_OK ) || !hqscaling ) {
      /*printf("unsupported scaling quality=%d\n", i);*/
      break;
    }
    else {
      /*printf("supported scaling quality=%d\n", i);*/
      ++this->scaling_level_max;
    }
  }
#endif

  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL, &this->temporal_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL, &this->temporal_spatial_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION, &this->noise_reduction_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_SHARPNESS, &this->sharpness_is_supported );
  vdp_video_mixer_query_feature_support( vdp_device, VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE, &this->inverse_telecine_is_supported );
  vdp_video_mixer_query_attribute_support( vdp_device, VDP_VIDEO_MIXER_ATTRIBUTE_SKIP_CHROMA_DEINTERLACE, &this->skip_chroma_is_supported );
  vdp_video_mixer_query_attribute_support( vdp_device, VDP_VIDEO_MIXER_ATTRIBUTE_BACKGROUND_COLOR, &this->background_is_supported );

  this->color_standard = VDP_COLOR_STANDARD_ITUR_BT_601;
  this->video_mixer_chroma = chroma;
  this->video_mixer_width = this->soft_surface_width;
  this->video_mixer_height = this->soft_surface_height;
  VdpVideoMixerFeature features[15];
  int features_count = 0;
  if ( this->noise_reduction_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_NOISE_REDUCTION;
    ++features_count;
  }
  if ( this->sharpness_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_SHARPNESS;
    ++features_count;
  }
  if ( this->temporal_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    ++features_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    ++features_count;
  }
  if ( this->inverse_telecine_is_supported ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
    ++features_count;
  }
#ifdef VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1
  for ( i=0; i<this->scaling_level_max; ++i ) {
    features[features_count] = VDP_VIDEO_MIXER_FEATURE_HIGH_QUALITY_SCALING_L1 + i;
    ++features_count;
  }
#endif
  VdpVideoMixerParameter params[] = { VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH, VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
        VDP_VIDEO_MIXER_PARAMETER_CHROMA_TYPE, VDP_VIDEO_MIXER_PARAMETER_LAYERS };
  int num_layers = 3;
  void const *param_values[] = { &this->video_mixer_width, &this->video_mixer_height, &chroma, &num_layers };
  st = vdp_video_mixer_create( vdp_device, features_count, features, 4, params, param_values, &this->video_mixer );
  if ( vdpau_init_error( st, "Can't create video mixer !!", &this->vo_driver, 1 ) ) {
    vdp_video_surface_destroy( this->soft_surface );
    for ( i=0; i<NOUTPUTSURFACE; ++i )
      vdp_output_surface_destroy( this->output_surface[i] );
    return NULL;
  }

  char deinterlacers_description[1024];
  memset( deinterlacers_description, 0, 1024 );
  int deint_count = 0;
  int deint_default = 0;
  this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[0];
  this->deinterlacers_method[deint_count] = DEINT_BOB;
  strcat( deinterlacers_description, vdpau_deinterlacer_description[0] );
  ++deint_count;
  if ( this->temporal_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[1];
    this->deinterlacers_method[deint_count] = DEINT_HALF_TEMPORAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[1] );
    ++deint_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[2];
    this->deinterlacers_method[deint_count] = DEINT_HALF_TEMPORAL_SPATIAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[2] );
    ++deint_count;
  }
  if ( this->temporal_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[3];
    this->deinterlacers_method[deint_count] = DEINT_TEMPORAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[3] );
    deint_default = deint_count;
    ++deint_count;
  }
  if ( this->temporal_spatial_is_supported ) {
    this->deinterlacers_name[deint_count] = vdpau_deinterlacer_name[4];
    this->deinterlacers_method[deint_count] = DEINT_TEMPORAL_SPATIAL;
    strcat( deinterlacers_description, vdpau_deinterlacer_description[4] );
    ++deint_count;
  }
  this->deinterlacers_name[deint_count] = NULL;

  if ( this->scaling_level_max ) {
    this->scaling_level_current = config->register_range( config, "video.output.vdpau_scaling_quality", 0,
           0, this->scaling_level_max, _("vdpau: Scaling Quality"),
           _("Scaling Quality Level"),
           10, vdpau_update_scaling_level, this );
  }

  this->deinterlace_method = config->register_enum( config, "video.output.vdpau_deinterlace_method", deint_default,
         this->deinterlacers_name, _("vdpau: HD deinterlace method"),
         deinterlacers_description,
         10, vdpau_update_deinterlace_method, this );

  if ( this->inverse_telecine_is_supported ) {
    this->enable_inverse_telecine = config->register_bool( config, "video.output.vdpau_enable_inverse_telecine", 1,
      _("vdpau: Try to recreate progressive frames from pulldown material"),
      _("Enable this to detect bad-flagged progressive content to which\n"
        "a 2:2 or 3:2 pulldown was applied.\n\n"),
        10, vdpau_update_enable_inverse_telecine, this );
  }

  this->honor_progressive = config->register_bool( config, "video.output.vdpau_honor_progressive", 0,
        _("vdpau: disable deinterlacing when progressive_frame flag is set"),
        _("Set to true if you want to trust the progressive_frame stream's flag.\n"
          "This flag is not always reliable.\n\n"),
        10, vdpau_honor_progressive_flag, this );

  if ( this->skip_chroma_is_supported ) {
    this->skip_chroma = config->register_bool( config, "video.output.vdpau_skip_chroma_deinterlace", 0,
        _("vdpau: disable advanced deinterlacers chroma filter"),
        _("Setting to true may help if your video card isn't able to run advanced deinterlacers.\n\n"),
        10, vdpau_set_skip_chroma, this );
  }

  this->studio_levels = config->register_bool( config, "video.output.vdpau_studio_levels", 0,
        _("vdpau: disable studio level"),
        _("Setting to true enables studio levels (16-219) instead of PC levels (0-255) in RGB colors.\n\n"),
        10, vdpau_set_studio_levels, this );

  if ( this->background_is_supported ) {
    this->background = config->register_num( config, "video.output.vdpau_background_color", 0,
        _("vdpau: color of none video area in output window"),
        _("Displaying 4:3 images on 16:9 plasma TV sets lets the inactive pixels outside the video age slower than the pixels in the active area. Setting a different background color (e. g. 8421504) makes all pixels age similarly. The number to enter for a certain color can be derived from its 6 digit hexadecimal RGB value.\n\n"),
        10, vdpau_set_background, this );
  }

  /* number of video frames from config - register it with the default value. */
  int frame_num = config->register_num (config, "engine.buffers.video_num_frames", 15, /* default */
       _("default number of video frames"),
       _("The default number of video frames to request "
         "from xine video out driver. Some drivers will "
         "override this setting with their own values."),
      20, NULL, this);

  /* now make sure we have at least 22 frames, to prevent
   * locks with vdpau_h264 */
  if(frame_num < 22)
    config->update_num(config,"engine.buffers.video_num_frames",22);

  this->capabilities = VO_CAP_YV12 | VO_CAP_YUY2 | VO_CAP_CROP | VO_CAP_UNSCALED_OVERLAY | VO_CAP_CUSTOM_EXTENT_OVERLAY | VO_CAP_ARGB_LAYER_OVERLAY | VO_CAP_VIDEO_WINDOW_OVERLAY;
  ok = 0;
  uint32_t mw, mh, ml, mr;
  st = vdp_decoder_query_capabilities( vdp_device, VDP_DECODER_PROFILE_H264_MAIN, &ok, &ml, &mr, &mw, &mh );
  if ( st != VDP_STATUS_OK  )
    fprintf(stderr, "vo_vdpau: getting h264_supported failed! : %s\n", vdp_get_error_string( st ) );
  else if ( !ok )
    fprintf(stderr, "vo_vdpau: this hardware doesn't support h264.\n" );
  else
    this->capabilities |= VO_CAP_VDPAU_H264;

  st = vdp_decoder_query_capabilities( vdp_device, VDP_DECODER_PROFILE_VC1_MAIN, &ok, &ml, &mr, &mw, &mh );
  if ( st != VDP_STATUS_OK  )
    fprintf(stderr, "vo_vdpau: getting vc1_supported failed! : %s\n", vdp_get_error_string( st ) );
  else if ( !ok )
    fprintf(stderr, "vo_vdpau: this hardware doesn't support vc1.\n" );
  else
    this->capabilities |= VO_CAP_VDPAU_VC1;

  st = vdp_decoder_query_capabilities( vdp_device, VDP_DECODER_PROFILE_MPEG2_MAIN, &ok, &ml, &mr, &mw, &mh );
  if ( st != VDP_STATUS_OK  )
    fprintf(stderr, "vo_vdpau: getting mpeg12_supported failed! : %s\n", vdp_get_error_string( st ) );
  else if ( !ok )
    fprintf(stderr, "vo_vdpau: this hardware doesn't support mpeg1/2.\n" );
  else
    this->capabilities |= VO_CAP_VDPAU_MPEG12;

  for ( i=0; i<NUM_FRAMES_BACK; i++)
    this->back_frame[i] = NULL;

  this->hue = 0;
  this->saturation = 100;
  this->contrast = 100;
  this->brightness = 0;
  this->sharpness = 0;
  this->noise = 0;
  this->deinterlace = 0;

  this->allocated_surfaces = 0;

  this->vdp_runtime_nr = 1;

  return &this->vo_driver;
}

/*
 * class functions
 */

static void *vdpau_init_class (xine_t *xine, void *visual_gen)
{
  vdpau_class_t *this = (vdpau_class_t *) calloc(1, sizeof(vdpau_class_t));

  this->driver_class.open_plugin     = vdpau_open_plugin;
  this->driver_class.identifier      = "vdpau";
  this->driver_class.description     = N_("xine video output plugin using VDPAU hardware acceleration");
  this->driver_class.dispose         = default_video_driver_class_dispose;
  this->xine                         = xine;

  return this;
}



static const vo_info_t vo_info_vdpau = {
  11,                    /* priority    */
  XINE_VISUAL_TYPE_X11  /* visual type */
};


/*
 * exported plugin catalog entry
 */

const plugin_info_t xine_plugin_info[] EXPORTED = {
  /* type, API, "name", version, special_info, init_function */
  { PLUGIN_VIDEO_OUT, 22, "vdpau", XINE_VERSION_CODE, &vo_info_vdpau, vdpau_init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
