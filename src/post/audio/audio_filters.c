/*
 * Copyright (C) 2000-2004 the xine project
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
 * $Id: audio_filters.c,v 1.2 2004/07/27 17:59:58 mroi Exp $
 *
 * catalog for audio filter plugins
 */


#include "xine_internal.h"
#include "xineutils.h"
#include "post.h"

#include "audio_filters.h"


post_info_t upmix_special_info   = { XINE_POST_TYPE_AUDIO_FILTER };
post_info_t stretch_special_info = { XINE_POST_TYPE_AUDIO_FILTER };


plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_POST, 9, "upmix", XINE_VERSION_CODE, &upmix_special_info, &upmix_init_plugin },
  { PLUGIN_POST, 9, "stretch", XINE_VERSION_CODE, &stretch_special_info, &stretch_init_plugin },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
