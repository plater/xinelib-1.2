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
 * $Id: configfile.c,v 1.65 2004/05/07 14:38:14 mroi Exp $
 *
 * config object (was: file) management - implementation
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "configfile.h"

#define LOG_MODULE "configfile"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "xineutils.h"
#include "xine_internal.h"

static int config_section_enum(const char *sect) {
  static char *known_section[] = {
    "gui",
    "audio",
    "video",
    "dxr3",
    "input",
    "codec",
    "post",
    "decoder",
    "misc",
    NULL
  };
  int i = 0;
  
  while (known_section[i])
    if (strcmp(sect, known_section[i++]) == 0)
      return i;
  return i + 1;
}

static void config_key_split(const char *key, char **base, char **section, char **plugin, char **name) {
  char *parse;
  
  *base = strdup(key);
  if ((parse = strchr(*base, '.'))) {
    *section = *base;
    *parse     = '\0';
    parse++;
    if ((*name = strchr(parse, '.'))) {
      *plugin = parse;
      **name  = '\0';
      (*name)++;
    } else {
      *plugin = NULL;
      *name   = parse;
    }
  } else {
    *section = NULL;
    *plugin  = NULL;
    *name    = parse;
  }
}

static void config_insert(config_values_t *this, cfg_entry_t *new_entry) {
  cfg_entry_t *cur, *prev;
  char *new_base, *new_section, *new_plugin, *new_name;
  char *cur_base, *cur_section, *cur_plugin, *cur_name;
  
  /* extract parts of the new key */
  config_key_split(new_entry->key, &new_base, &new_section, &new_plugin, &new_name);
  
  /* search right position */
  cur_base = NULL;
  for (cur = this->first, prev = NULL; cur; prev = cur, cur = cur->next) {
    /* extract parts of the cur key */
    free(cur_base);
    config_key_split(cur->key, &cur_base, &cur_section, &cur_plugin, &cur_name);
    
    /* sort by section name */
    if (!new_section &&  cur_section) break;
    if ( new_section && !cur_section) continue;
    if ( new_section &&  cur_section) {
      int new_sec_num = config_section_enum(new_section);
      int cur_sec_num = config_section_enum(cur_section);
      int cmp         = strcmp(new_section, cur_section);
      if (new_sec_num < cur_sec_num) break;
      if (new_sec_num > cur_sec_num) continue;
      if (cmp < 0) break;
      if (cmp > 0) continue;
    }
    /* sort by plugin name */
    if (!new_plugin &&  cur_plugin) break;
    if ( new_plugin && !cur_plugin) continue;
    if ( new_plugin &&  cur_plugin) {
      int cmp = strcmp(new_plugin, cur_plugin);
      if (cmp < 0) break;
      if (cmp > 0) continue;
    }
    /* sort by experience level */
    if (new_entry->exp_level < cur->exp_level) break;
    if (new_entry->exp_level > cur->exp_level) continue;
    /* sort by entry name */
    if (!new_name &&  cur_name) break;
    if ( new_name && !cur_name) continue;
    if ( new_name &&  cur_name) {
      int cmp = strcmp(new_name, cur_name);
      if (cmp < 0) break;
      if (cmp > 0) continue;
    }
    
    break;
  }
  free(new_base);
  free(cur_base);
  
  new_entry->next = cur;
  if (!cur)
    this->last = new_entry;
  if (prev)
    prev->next = new_entry;
  else
    this->first = new_entry;
}

static cfg_entry_t *__config_add (config_values_t *this, const char *key, int exp_level) {

  cfg_entry_t *entry;

  entry = (cfg_entry_t *) xine_xmalloc (sizeof (cfg_entry_t));
  entry->config        = this;
  entry->key           = strdup(key);
  entry->type          = XINE_CONFIG_TYPE_UNKNOWN;
  entry->unknown_value = NULL;
  entry->str_value     = NULL;
  entry->exp_level     = exp_level;
  
  config_insert(this, entry);

  lprintf ("add entry key=%s\n", key);

  return entry;
}

static void __config_lookup_entry_int (config_values_t *this, const char *key,
				       cfg_entry_t **entry, cfg_entry_t **prev) {
  *entry = this->first;
  *prev  = NULL;

  while (*entry && strcmp((*entry)->key, key)) {
    *prev  = *entry;
    *entry = (*entry)->next;
  }
}


/*
 * external interface
 */

static cfg_entry_t *__config_lookup_entry(config_values_t *this, const char *key) {
  cfg_entry_t *entry, *prev;
  
  pthread_mutex_lock(&this->config_lock);
  __config_lookup_entry_int(this, key, &entry, &prev);
  pthread_mutex_unlock(&this->config_lock);
  
  return entry;
}

static char *__config_register_string (config_values_t *this,
				       const char *key,
				       const char *def_value,
				       const char *description,
				       const char *help,
				       int exp_level,
				       xine_config_cb_t changed_cb,
				       void *cb_data) {

  cfg_entry_t *entry, *prev;

  _x_assert(key);
  _x_assert(def_value);

  lprintf ("registering %s\n", key);

  /* make sure this entry exists, create it if not */
  pthread_mutex_lock(&this->config_lock);

  __config_lookup_entry_int(this, key, &entry, &prev);

  if (!entry) {
    entry = __config_add (this, key, exp_level);
    entry->unknown_value = strdup(def_value);
  } else {
    if (!entry->next)
      this->last = prev;
    if (!prev)
      this->first = entry->next;
    else
      prev->next = entry->next;
    
    entry->exp_level = exp_level;
    config_insert(this, entry);
  }

  /* convert entry to string type if necessary */

  if (entry->type != XINE_CONFIG_TYPE_STRING) {
    entry->type = XINE_CONFIG_TYPE_STRING;
    /*
     * if there is no unknown_value (made with register_empty) set
     * it to default value
     */
    if(!entry->unknown_value)
      entry->unknown_value = strdup(def_value);

    entry->str_value = strdup(entry->unknown_value);

  } else
    free (entry->str_default);

  /* fill out rest of struct */

  entry->str_default    = strdup(def_value);
  entry->description    = description;
  entry->help           = help;
  entry->callback       = changed_cb;
  entry->callback_data  = cb_data;

  pthread_mutex_unlock(&this->config_lock);

  return entry->str_value;
}

static int __config_register_num (config_values_t *this,
				  const char *key, int def_value,
				  const char *description,
				  const char *help,
				  int exp_level,
				  xine_config_cb_t changed_cb,
				  void *cb_data) {

  cfg_entry_t *entry, *prev;
  _x_assert(key);

  lprintf ("registering %s\n", key);

  /* make sure this entry exists, create it if not */
  pthread_mutex_lock(&this->config_lock);

  __config_lookup_entry_int(this, key, &entry, &prev);

  if (!entry) {
    entry = __config_add (this, key, exp_level);
    entry->unknown_value = NULL;
  } else {
    if (!entry->next)
      this->last = prev;
    if (!prev)
      this->first = entry->next;
    else
      prev->next = entry->next;
    
    entry->exp_level = exp_level;
    config_insert(this, entry);
  }

  /* convert entry to num type if necessary */

  if (entry->type != XINE_CONFIG_TYPE_NUM) {

    if (entry->type == XINE_CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = XINE_CONFIG_TYPE_NUM;

    if (entry->unknown_value)
      sscanf (entry->unknown_value, "%d", &entry->num_value);
    else
      entry->num_value = def_value;
  }


  /* fill out rest of struct */

  entry->num_default    = def_value;
  entry->description    = description;
  entry->help           = help;
  entry->callback       = changed_cb;
  entry->callback_data  = cb_data;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static int __config_register_bool (config_values_t *this,
				   const char *key,
				   int def_value,
				   const char *description,
				   const char *help,
				   int exp_level,
				   xine_config_cb_t changed_cb,
				   void *cb_data) {

  cfg_entry_t *entry, *prev;
  _x_assert(key);

  lprintf ("registering %s\n", key);

  /* make sure this entry exists, create it if not */
  pthread_mutex_lock(&this->config_lock);

  __config_lookup_entry_int(this, key, &entry, &prev);

  if (!entry) {
    entry = __config_add (this, key, exp_level);
    entry->unknown_value = NULL;
  } else {
    if (!entry->next)
      this->last = prev;
    if (!prev)
      this->first = entry->next;
    else
      prev->next = entry->next;
    
    entry->exp_level = exp_level;
    config_insert(this, entry);
  }

  /* convert entry to bool type if necessary */

  if (entry->type != XINE_CONFIG_TYPE_BOOL) {

    if (entry->type == XINE_CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = XINE_CONFIG_TYPE_BOOL;

    if (entry->unknown_value)
      sscanf (entry->unknown_value, "%d", &entry->num_value);
    else
      entry->num_value = def_value;
  }


  /* fill out rest of struct */

  entry->num_default    = def_value;
  entry->description    = description;
  entry->help           = help;
  entry->callback       = changed_cb;
  entry->callback_data  = cb_data;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static int __config_register_range (config_values_t *this,
				    const char *key,
				    int def_value,
				    int min, int max,
				    const char *description,
				    const char *help,
				    int exp_level,
				    xine_config_cb_t changed_cb,
				    void *cb_data) {

  cfg_entry_t *entry, *prev;
  _x_assert(key);

  lprintf ("registering range %s\n", key);

  /* make sure this entry exists, create it if not */
  pthread_mutex_lock(&this->config_lock);

  __config_lookup_entry_int(this, key, &entry, &prev);

  if (!entry) {
    entry = __config_add (this, key, exp_level);
    entry->unknown_value = NULL;
  } else {
    if (!entry->next)
      this->last = prev;
    if (!prev)
      this->first = entry->next;
    else
      prev->next = entry->next;
    
    entry->exp_level = exp_level;
    config_insert(this, entry);
  }

  /* convert entry to range type if necessary */

  if (entry->type != XINE_CONFIG_TYPE_RANGE) {

    if (entry->type == XINE_CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = XINE_CONFIG_TYPE_RANGE;

    if (entry->unknown_value)
      sscanf (entry->unknown_value, "%d", &entry->num_value);
    else
      entry->num_value = def_value;
  }

  /* fill out rest of struct */

  entry->num_default   = def_value;
  entry->range_min     = min;
  entry->range_max     = max;
  entry->description   = description;
  entry->help          = help;
  entry->callback      = changed_cb;
  entry->callback_data = cb_data;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static int __config_parse_enum (const char *str, char **values) {

  char **value;
  int    i;


  value = values;
  i = 0;

  while (*value) {

    lprintf ("parse enum, >%s< ?= >%s<\n", *value, str);

    if (!strcmp (*value, str))
      return i;

    value++;
    i++;
  }

  lprintf ("warning, >%s< is not a valid enum here, using 0\n", str);

  return 0;
}

static int __config_register_enum (config_values_t *this,
				   const char *key,
				   int def_value,
				   char **values,
				   const char *description,
				   const char *help,
				   int exp_level,
				   xine_config_cb_t changed_cb,
				   void *cb_data) {

  cfg_entry_t *entry, *prev;
  _x_assert(key);
  _x_assert(values);

  lprintf ("registering enum %s\n", key);

  /* make sure this entry exists, create it if not */
  pthread_mutex_lock(&this->config_lock);

  __config_lookup_entry_int(this, key, &entry, &prev);

  if (!entry) {
    entry = __config_add (this, key, exp_level);
    entry->unknown_value = NULL;
  } else {
    if (!entry->next)
      this->last = prev;
    if (!prev)
      this->first = entry->next;
    else
      prev->next = entry->next;
    
    entry->exp_level = exp_level;
    config_insert(this, entry);
  }

  /* convert entry to enum type if necessary */

  if (entry->type != XINE_CONFIG_TYPE_ENUM) {

    if (entry->type == XINE_CONFIG_TYPE_STRING) {
      free (entry->str_value);
      free (entry->str_default);
    }

    entry->type      = XINE_CONFIG_TYPE_ENUM;

    if (entry->unknown_value)
      entry->num_value = __config_parse_enum (entry->unknown_value, values);
    else
      entry->num_value = def_value;

  }

  /* fill out rest of struct */

  entry->num_default   = def_value;
  entry->enum_values   = values;
  entry->description   = description;
  entry->help          = help;
  entry->callback      = changed_cb;
  entry->callback_data = cb_data;

  pthread_mutex_unlock(&this->config_lock);

  return entry->num_value;
}

static void __config_shallow_copy(xine_cfg_entry_t *dest, cfg_entry_t *src)
{
  dest->key           = src->key;
  dest->type          = src->type;
  dest->unknown_value = src->unknown_value;
  dest->str_value     = src->str_value;
  dest->str_default   = src->str_default;
  dest->num_value     = src->num_value;
  dest->num_default   = src->num_default;
  dest->range_min     = src->range_min;
  dest->range_max     = src->range_max;
  dest->enum_values   = src->enum_values;
  dest->description   = src->description;
  dest->help          = src->help;
  dest->exp_level     = src->exp_level;
  dest->callback      = src->callback;
  dest->callback_data = src->callback_data;
}

static void __config_update_num (config_values_t *this,
				 const char *key, int value) {
  
  cfg_entry_t *entry;

  entry = this->lookup_entry (this, key);

  lprintf ("updating %s to %d\n", key, value);

  if (!entry) {

    lprintf ("WARNING! tried to update unknown key %s (to %d)\n", key, value);

    return;

  }

  if ((entry->type == XINE_CONFIG_TYPE_UNKNOWN)
      || (entry->type == XINE_CONFIG_TYPE_STRING)) {
    printf ("configfile: error - tried to update non-num type %d (key %s, value %d)\n",
	    entry->type, entry->key, value);
    return;
  }

  pthread_mutex_lock(&this->config_lock);
  entry->num_value = value;

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;
    __config_shallow_copy(&cb_entry, entry);
    /* do not enter the callback from within a locked context */
    pthread_mutex_unlock(&this->config_lock);
    entry->callback (entry->callback_data, &cb_entry);
  } else
    pthread_mutex_unlock(&this->config_lock);
}

static void __config_update_string (config_values_t *this,
				       const char *key,
				       const char *value) {

  cfg_entry_t *entry;
  char *str_free = NULL;

  lprintf ("updating %s to %s\n", key, value);

  entry = this->lookup_entry (this, key);

  if (!entry) {

    printf ("configfile: error - tried to update unknown key %s (to %s)\n",
	    key, value);
    return;

  }
  
  /* if an enum is updated with a string, we convert the string to
   * its index and use update number */
  if (entry->type == XINE_CONFIG_TYPE_ENUM) {
    __config_update_num(this, key, __config_parse_enum(value, entry->enum_values));
    return;
  }

  if (entry->type != XINE_CONFIG_TYPE_STRING) {
    printf ("configfile: error - tried to update non-string type %d (key %s, value %s)\n",
	    entry->type, entry->key, value);
    return;
  }

  pthread_mutex_lock(&this->config_lock);
  if (value != entry->str_value) {
    str_free = entry->str_value;
    entry->str_value = strdup(value);
  }

  if (entry->callback) {
    xine_cfg_entry_t cb_entry;
    __config_shallow_copy(&cb_entry, entry);
    /* FIXME: find a solution which does not enter the callback with the lock acquired,
     * but does also handle the char* leak- and race-free without unnecessary string copying */
    entry->callback (entry->callback_data, &cb_entry);
  }

  if (str_free) free(str_free);
  pthread_mutex_unlock(&this->config_lock);
}

/*
 * load/save config data from/to afile (e.g. $HOME/.xine/config)
 */
void xine_config_load (xine_t *xine, const char *filename) {

  config_values_t *this = xine->config;
  FILE *f_config;

  lprintf ("reading from file '%s'\n", filename);

  f_config = fopen (filename, "r");

  if (f_config) {

    char line[1024];
    char *value;

    while (fgets (line, 1023, f_config)) {
      line[strlen(line)-1]= (char) 0; /* eliminate lf */

      if (line[0] == '#')
	continue;
      
      if (line[0] == '.') {
	if (strncmp(line, ".version:", 9) == 0) {
	  sscanf(line + 9, "%d", &this->current_version);
	  if (this->current_version > CONFIG_FILE_VERSION)
	    xine_log(xine, XINE_LOG_MSG,
		     _("The current config file has been modified by a newer version of xine."));
	}
	continue;
      }

      if ((value = strchr (line, ':'))) {

	cfg_entry_t *entry;

	*value = (char) 0;
	value++;

	if (!(entry = __config_lookup_entry(this, line))) {
	  pthread_mutex_lock(&this->config_lock);
	  entry = __config_add (this, line, 50);
	  entry->unknown_value = strdup(value);
	  pthread_mutex_unlock(&this->config_lock);
	} else {
          switch (entry->type) {
          case XINE_CONFIG_TYPE_RANGE:
          case XINE_CONFIG_TYPE_NUM:
          case XINE_CONFIG_TYPE_BOOL:
            __config_update_num (this, entry->key, atoi(value));
            break;
          case XINE_CONFIG_TYPE_ENUM:
          case XINE_CONFIG_TYPE_STRING:
            __config_update_string (this, entry->key, value);
            break;
          case XINE_CONFIG_TYPE_UNKNOWN:
	    pthread_mutex_lock(&this->config_lock);
	    free(entry->unknown_value);
	    entry->unknown_value = strdup(value);
	    pthread_mutex_unlock(&this->config_lock);
	    break;
          default:
            printf ("xine_interface: error, unknown config entry type %d\n", entry->type);
            _x_abort();
          }
	}
      }
    }

    fclose (f_config);
  }
}

void xine_config_save (xine_t *xine, const char *filename) {

  config_values_t *this = xine->config;
  char             temp[XINE_PATH_MAX];
  int              backup = 0;
  struct stat      backup_stat, config_stat;
  FILE            *f_config, *f_backup;

  sprintf(temp, "%s~", filename);
  unlink (temp);

  if (stat(temp, &backup_stat) != 0) {
    
    lprintf("backing up configfile to %s\n", temp);

    f_backup = fopen(temp, "w");
    f_config = fopen(filename, "r");
    
    if (f_config && f_backup && (stat(filename, &config_stat) == 0) && (config_stat.st_size > 0)) {
      char    *buf = NULL;
      size_t   rlen;
      
      buf = (char *) xine_xmalloc(config_stat.st_size + 1);
      if((rlen = fread(buf, 1, config_stat.st_size, f_config)) && (rlen == config_stat.st_size)) {
	(void) fwrite(buf, 1, rlen, f_backup);
      }
      free(buf);
      
      fclose(f_config);
      fclose(f_backup);
      stat(temp, &backup_stat);
      
      if (config_stat.st_size == backup_stat.st_size)
	backup = 1;
      else
	unlink(temp);
      
    } 
    else {

      if (f_config)
        fclose(f_config);
      else
	backup = 1;

      if (f_backup)
        fclose(f_backup);

    }
  }
  
  if (!backup && (stat(filename, &config_stat) == 0)) {
    xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: backing up configfile to %s failed\n"), temp);
    xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: your configuration will not be saved\n"));
    return;
  }
  
  lprintf ("writing config file to %s\n", filename);

  f_config = fopen(filename, "w");
      
  if (f_config) {

    cfg_entry_t *entry;

    fprintf (f_config, "#\n# xine config file\n#\n");
    fprintf (f_config, ".version:%d\n\n", CONFIG_FILE_VERSION);
    fprintf (f_config, "# Entries which are still set to their default values are commented out.\n");
    fprintf (f_config, "# Remove the \'#\' at the beginning of the line, if you want to change them.\n\n");

    pthread_mutex_lock(&this->config_lock);
    entry = this->first;

    while (entry) {

      lprintf ("saving key '%s'\n", entry->key);

      if (entry->description)
	fprintf (f_config, "# %s\n", entry->description);

      switch (entry->type) {
      case XINE_CONFIG_TYPE_UNKNOWN:

/*#if 0*/
	/* discard unclaimed values */
	fprintf (f_config, "%s:%s\n",
		 entry->key, entry->unknown_value);
	fprintf (f_config, "\n");
/*#endif*/

	break;
      case XINE_CONFIG_TYPE_RANGE:
	fprintf (f_config, "# [%d..%d], default: %d\n",
		 entry->range_min, entry->range_max, entry->num_default);
	if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      case XINE_CONFIG_TYPE_STRING:
	fprintf (f_config, "# string, default: %s\n",
		 entry->str_default);
	if (strcmp(entry->str_value, entry->str_default) == 0) fprintf (f_config, "#");
	fprintf (f_config, "%s:%s\n", entry->key, entry->str_value);
	fprintf (f_config, "\n");
	break;
      case XINE_CONFIG_TYPE_ENUM: {
	char **value;

	fprintf (f_config, "# {");
	value = entry->enum_values;
	while (*value) {
	  fprintf (f_config, " %s ", *value);
	  value++;
	}

	fprintf (f_config, "}, default: %d\n",
		 entry->num_default);

	if (entry->enum_values[entry->num_value] != NULL) {
	  if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	  fprintf (f_config, "%s:", entry->key);
	  fprintf (f_config, "%s\n", entry->enum_values[entry->num_value]);
	}

	fprintf (f_config, "\n");
	break;
      }
      case XINE_CONFIG_TYPE_NUM:
	fprintf (f_config, "# numeric, default: %d\n",
		 entry->num_default);
	if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      case XINE_CONFIG_TYPE_BOOL:
	fprintf (f_config, "# bool, default: %d\n",
		 entry->num_default);
	if (entry->num_value == entry->num_default) fprintf (f_config, "#");
	fprintf (f_config, "%s:%d\n", entry->key, entry->num_value);
	fprintf (f_config, "\n");
	break;
      }

      entry = entry->next;
    }
    pthread_mutex_unlock(&this->config_lock);
    
    if (fclose(f_config) != 0) {
      xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: writing configuration to %s failed\n"), filename);
      xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: removing possibly broken config file %s\n"), filename);
      xprintf(xine, XINE_VERBOSITY_LOG, _("configfile: WARNING: you should check the backup file %s\n"), temp);
      /* writing config failed -> remove file, it might be broken ... */
      unlink(filename);
      /* ... but keep the backup */
      backup = 0;
    }
  }
  
  if (backup)
    unlink(temp);
}

static void __config_dispose (config_values_t *this) {

  cfg_entry_t *entry, *last;

  pthread_mutex_lock(&this->config_lock);
  entry = this->first;

  lprintf ("dispose\n");

  while (entry) {
    last = entry;
    entry = entry->next;

    if (last->key)
      free (last->key);
    if (last->unknown_value)
      free (last->unknown_value);

    if (last->type == XINE_CONFIG_TYPE_STRING) {
      free (last->str_value);
      free (last->str_default);
    }

    free (last);
  }
  pthread_mutex_unlock(&this->config_lock);

  pthread_mutex_destroy(&this->config_lock);
  free (this);
}


static void __config_unregister_cb (config_values_t *this, const char *key) {

  cfg_entry_t *entry;

  _x_assert(key);
  _x_assert(this);

  entry = __config_lookup_entry (this, key);
  if (entry) {
    pthread_mutex_lock(&this->config_lock);
    entry->callback = NULL;
    entry->callback_data = NULL;
    pthread_mutex_unlock(&this->config_lock);
  }
}


config_values_t *_x_config_init (void) {

#ifdef HAVE_IRIXAL
  volatile /* is this a (old, 2.91.66) irix gcc bug?!? */
#endif
  config_values_t *this;

  if (!(this = xine_xmalloc(sizeof(config_values_t)))) {

    printf ("configfile: could not allocate config object\n");
    _x_abort();
  }

  this->first = NULL;
  this->last  = NULL;
  this->current_version = 0;

  pthread_mutex_init(&this->config_lock, NULL);

  this->register_string     = __config_register_string;
  this->register_range      = __config_register_range;
  this->register_enum       = __config_register_enum;
  this->register_num        = __config_register_num;
  this->register_bool       = __config_register_bool;
  this->update_num          = __config_update_num;
  this->update_string       = __config_update_string;
  this->parse_enum          = __config_parse_enum;
  this->lookup_entry        = __config_lookup_entry;
  this->unregister_callback = __config_unregister_cb;
  this->dispose             = __config_dispose;

  return this;
}

int _x_config_change_opt(config_values_t *config, const char *opt) {
  cfg_entry_t *entry;
  int          handled = 0;

  lprintf ("change_opt '%s'\n", opt);
  
  if ((entry = config->lookup_entry(config, "misc.implicit_config")) &&
      entry->type == XINE_CONFIG_TYPE_BOOL) {
    if (!entry->num_value)
      /* changing config entries implicitly is denied */
      return -1;
  } else
    /* someone messed with the config entry */
    return -1;

  if(config && opt) {
    char *key, *value;

    key = strdup(opt);
    value = strrchr(key, ':');

    if(key && strlen(key) && value && strlen(value)) {

      *value++ = '\0';

      entry = config->lookup_entry(config, key);

      if(entry->exp_level >= XINE_CONFIG_SECURITY) {
        printf(_("configfile: entry '%s' mustn't be modified from MRL\n"), key);
        free(key);
        return -1;
      }
      
      if(entry) {

	switch(entry->type) {

	case XINE_CONFIG_TYPE_STRING:
	  config->update_string(config, key, value);
	  handled = 1;
	  break;

	case XINE_CONFIG_TYPE_RANGE:
	case XINE_CONFIG_TYPE_ENUM:
	case XINE_CONFIG_TYPE_NUM:
	case XINE_CONFIG_TYPE_BOOL:
	  config->update_num(config, key, (atoi(value)));
	  handled = 1;
	  break;

	case XINE_CONFIG_TYPE_UNKNOWN:
	  entry->unknown_value = strdup(value);
	  handled = 1;
	  break;

	}
      }
    }
    free(key);
  }

  return handled;
}

