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
 * $Id: utils.c,v 1.29 2004/07/25 17:47:01 mroi Exp $
 *
 */
#define	_POSIX_PTHREAD_SEMANTICS 1	/* for 5-arg getpwuid_r on solaris */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xineutils.h"
#include "xineintl.h"

#include <errno.h>
#include <pwd.h>
#include <netdb.h>

#if HAVE_EXECINFO_H
#include <execinfo.h>
#endif

#if HAVE_UCONTEXT_H
#include <ucontext.h>
#endif

#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif

#if HAVE_LIBGEN_H
#include <libgen.h>
#endif


typedef struct {
  char                    *language;     /* name of the locale */
  char                    *encoding;     /* typical encoding */
  char                    *spu_encoding; /* default spu encoding */
  char                    *modifier;
} lang_locale_t;


/*
 * information about locales used in xine
 */
static lang_locale_t lang_locales[] = {
  { "af_ZA",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "ar_AE",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_BH",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_DZ",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_EG",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_IN",    "utf-8",       "utf-8",       NULL       },
  { "ar_IQ",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_JO",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_KW",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_LB",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_LY",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_MA",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_OM",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_QA",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_SA",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_SD",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_SY",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_TN",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "ar_YE",    "iso-8859-6",  "iso-8859-6",  NULL       },
  { "be_BY",    "cp1251",      "cp1251",      NULL       },
  { "bg_BG",    "cp1251",      "cp1251",      NULL       },
  { "br_FR",    "iso-8859-1",  "iso-88591",   NULL       },
  { "bs_BA",    "iso-8859-2",  "cp1250",      NULL       },
  { "ca_ES",    "iso-8859-1",  "iso-88591",   NULL       },
  { "ca_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "cs_CZ",    "iso-8859-2",  "cp1250",      NULL       },
  { "cy_GB",    "iso-8859-14", "iso-8859-14", NULL       },
  { "da_DK",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "de_AT",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "de_AT",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "de_BE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "de_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "de_CH",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "de_DE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "de_DE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "de_LU",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "de_LU",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "el_GR",    "iso-8859-7",  "iso-8859-7",  NULL       },
  { "en_AU",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_BW",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_CA",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_DK",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_GB",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_HK",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_IE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_IE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "en_IN",    "utf-8",       "utf-8",       NULL       },
  { "en_NZ",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_PH",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_SG",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_US",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_ZA",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "en_ZW",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_AR",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_BO",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_CL",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_CO",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_CR",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_DO",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_EC",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_ES",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "es_GT",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_HN",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_MX",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_NI",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_PA",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_PE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_PR",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_PY",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_SV",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_US",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_UY",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "es_VE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "et_EE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "eu_ES",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "eu_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fa_IR",    "utf-8",       "utf-8",       NULL       },
  { "fi_FI",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "fi_FI",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fo_FO",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "fr_BE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "fr_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fr_CA",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "fr_CH",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "fr_FR",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "fr_FR",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "fr_LU",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "fr_LU",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "ga_IE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "ga_IE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "gl_ES",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "gl_ES",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "gv_GB",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "he_IL",    "iso-8859-8",  "iso-8859-8",  NULL       },
  { "hi_IN",    "utf-8",       "utf-8",       NULL       },
  { "hr_HR",    "iso-8859-2",  "cp1250",      NULL       },
  { "hu_HU",    "iso-8859-2",  "cp1250",      NULL       },
  { "id_ID",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "is_IS",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "it_CH",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "it_IT",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "it_IT",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "iw_IL",    "iso-8859-8",  "iso-8859-8",  NULL       },
  { "ja_JP",    "euc-jp",      "euc-jp",      NULL       },
  { "ja_JP",    "ujis",        "ujis",        NULL       },
  { "japanese", "euc",         "euc",         NULL       },
  { "ka_GE",    "georgian-ps", "georgian-ps", NULL       },
  { "kl_GL",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "ko_KR",    "euc-kr",      "euc-kr",      NULL       },
  { "ko_KR",    "utf-8",       "utf-8",       NULL       },
  { "korean",   "euc",         "euc",         NULL       },
  { "kw_GB",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "lt_LT",    "iso-8859-13", "iso-8859-13", NULL       },
  { "lv_LV",    "iso-8859-13", "iso-8859-13", NULL       },
  { "mi_NZ",    "iso-8859-13", "iso-8859-13", NULL       },
  { "mk_MK",    "iso-8859-5",  "cp1251",      NULL       },
  { "mr_IN",    "utf-8",       "utf-8",       NULL       },
  { "ms_MY",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "mt_MT",    "iso-8859-3",  "iso-8859-3",  NULL       },
  { "nb_NO",    "ISO-8859-1",  "ISO-8859-1",  NULL       },
  { "nl_BE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "nl_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "nl_NL",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "nl_NL",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "nn_NO",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "no_NO",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "oc_FR",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "pl_PL",    "iso-8859-2",  "cp1250",      NULL       },
  { "pt_BR",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "pt_PT",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "pt_PT",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "ro_RO",    "iso-8859-2",  "cp1250",      NULL       },
  { "ru_RU",    "iso-8859-5",  "cp1251",      NULL       },
  { "ru_RU",    "koi8-r",      "cp1251",      NULL       },
  { "ru_UA",    "koi8-u",      "cp1251",      NULL       },
  { "se_NO",    "utf-8",       "utf-8",       NULL       },
  { "sk_SK",    "iso-8859-2",  "cp1250",      NULL       },
  { "sl_SI",    "iso-8859-2",  "cp1250",      NULL       },
  { "sq_AL",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "sr_YU",    "iso-8859-2",  "cp1250",      NULL       },
  { "sr_YU",    "iso-8859-5",  "cp1251",      "cyrillic" },
  { "sv_FI",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "sv_FI",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "sv_SE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "ta_IN",    "utf-8",       "utf-8",       NULL       },
  { "te_IN",    "utf-8",       "utf-8",       NULL       },
  { "tg_TJ",    "koi8-t",      "cp1251",      NULL       },
  { "th_TH",    "tis-620",     "tis-620",     NULL       },
  { "tl_PH",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "tr_TR",    "iso-8859-9",  "iso-8859-9",  NULL       },
  { "uk_UA",    "koi8-u",      "cp1251",      NULL       },
  { "ur_PK",    "utf-8",       "utf-8",       NULL       },
  { "uz_UZ",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "vi_VN",    "tcvn",        "tcvn",        NULL       },
  { "vi_VN",    "utf-8",       "utf-8",       NULL       },
  { "wa_BE",    "iso-8859-1",  "iso-8859-1",  NULL       },
  { "wa_BE",    "iso-8859-15", "iso-8859-15", "euro"     },
  { "yi_US",    "cp1255",      "cp1255",      NULL       },
  { "zh_CN",    "gb18030",     "gb18030",     NULL       },
  { "zh_CN",    "gb2312",      "gb2312",      NULL       },
  { "zh_CN",    "gbk",         "gbk",         NULL       },
  { "zh_HK",    "big5-hkscs",  "big5-hkscs",  NULL       },
  { "zh_TW",    "big-5",       "big-5",       NULL       },
  { "zh_TW",    "euc-tw",      "euc-tw",      NULL       },
  { NULL,       NULL,          NULL,          NULL       }
};


void *xine_xmalloc(size_t size) {
  void *ptr;

  /* prevent xine_xmalloc(0) of possibly returning NULL */
  if( !size )
    size++;

  if((ptr = calloc(1, size)) == NULL) {
    fprintf(stderr, "%s: malloc() failed: %s.\n",
	    __XINE_FUNCTION__, strerror(errno));
    return NULL;
  }

  return ptr;
}

void *xine_xmalloc_aligned(size_t alignment, size_t size, void **base) {

  char *ptr;
  
  *base = ptr = xine_xmalloc (size+alignment);
  
  while ((size_t) ptr % alignment)
    ptr++;
    
  return ptr;
}

#if defined(WIN32)

/* Ridiculous hack to return valid xine support
 * directories. These should be read from
 * a registry entry set at install time.
 */

char *exec_path_append_subdir(char *string) {
  static char tmp_win32_path[1024];
  char *tmpchar;
  char *cmdline;
  char *back_slash;
  char *fore_slash;
  char *last_slash;

  /* get program exec command line */
  cmdline = GetCommandLine();

  /* check for " at beginning of string */
  if( *cmdline == '\"' ) {
    /* copy command line, skip first quote */
    strncpy(tmp_win32_path, cmdline + 1, sizeof(tmp_win32_path));
    tmp_win32_path[sizeof(tmp_win32_path) - 1] = '\0';

    /* terminate at second set of quotes */
    tmpchar = strchr(tmp_win32_path, '\"');
    if (tmpchar) *tmpchar = 0;
  } else {
    /* copy command line */
    strncpy(tmp_win32_path, cmdline, sizeof(tmp_win32_path));
    tmp_win32_path[sizeof(tmp_win32_path) - 1] = '\0';

    /* terminate at first space */
    tmpchar = strchr(tmp_win32_path, ' ');
    if (tmpchar) *tmpchar = 0;
  }

  /* find the last occurance of a back
   * slash or fore slash 
   */
  back_slash = strrchr(tmp_win32_path, '\\');
  fore_slash = strrchr(tmp_win32_path, '/');

  /* make sure the last back slash was not
   * after a drive letter 
   */
  if(back_slash > tmp_win32_path)
    if(*( back_slash - 1 ) == ':')
      back_slash = 0;

  /* which slash was the latter slash */
  if(back_slash > fore_slash)
    last_slash = back_slash;
  else
    last_slash = fore_slash;

  /* detect if we had a relative or
   * distiguished path ( had a slash )
   */
  if(last_slash) {
    /* we had a slash charachter in our
     * command line
     */
    *(last_slash + 1) = 0;

    /* if had a string to append to the path */
    if(string)
      strncat(tmp_win32_path, string, sizeof(tmp_win32_path) - strlen(tmp_win32_path) - 1);
  } else {
    if(string) {
    /* if had a string to append to the path */
      strncpy(tmp_win32_path, string, sizeof(tmp_win32_path));
      tmp_win32_path[sizeof(tmp_win32_path) - 1] = '\0';
    } else {
    /* no slash, assume local directory */
      strcpy(tmp_win32_path, ".");
    }
  }

  return tmp_win32_path;
}
#endif

#ifndef BUFSIZ
#define BUFSIZ 256
#endif

const char *xine_get_homedir(void) {

#ifdef WIN32
	return XINE_HOMEDIR;
#else

  struct passwd pwd, *pw = NULL;
  static char homedir[BUFSIZ] = {0,};

  if(homedir[0])
    return homedir;

#ifdef HAVE_GETPWUID_R
  if(getpwuid_r(getuid(), &pwd, homedir, sizeof(homedir), &pw) != 0 || pw == NULL) {
#else
  if((pw = getpwuid(getuid())) == NULL) {
#endif
    char *tmp = getenv("HOME");
    if(tmp) {
      strncpy(homedir, tmp, sizeof(homedir));
      homedir[sizeof(homedir) - 1] = '\0';
    }
  } else {
    char *s = strdup(pw->pw_dir);
    strncpy(homedir, s, sizeof(homedir));
    homedir[sizeof(homedir) - 1] = '\0';
    free(s);
  }

  if(!homedir[0]) {
    printf("xine_get_homedir: Unable to get home directory, set it to /tmp.\n");
    strcpy(homedir, "/tmp");
  }

  return homedir;
#endif /* _MSC_VER */
}

char *xine_chomp(char *str) {
  char *pbuf;

  pbuf = str;

  while(*pbuf != '\0') pbuf++;

  while(pbuf > str) {
    if(*pbuf == '\r' || *pbuf == '\n' || *pbuf == '"') pbuf = '\0';
    pbuf--;
  }

  while(*pbuf == '=' || *pbuf == '"') pbuf++;

  return pbuf;
}


/*
 * a thread-safe usecond sleep
 */
void xine_usec_sleep(unsigned usec) {
#if HAVE_NANOSLEEP
  /* nanosleep is prefered on solaris, because it's mt-safe */
  struct timespec ts;

  ts.tv_sec =   usec / 1000000;
  ts.tv_nsec = (usec % 1000000) * 1000;
  nanosleep(&ts, NULL);
#else
  usleep(usec);
#endif
}


/* print a hexdump of length bytes from the data given in buf */
void xine_hexdump (const char *buf, int length) {
  int i,j;
  unsigned char c;

  /* printf ("Hexdump: %i Bytes\n", length);*/
  for(j=0; j<69; j++)
    printf ("-");
  printf ("\n");

  j=0;
  while(j<length) {
    printf ("%04X ",j);
    for (i=j; i<j+16; i++) {
      if( i<length )
        printf ("%02X ", (unsigned char) buf[i]);
      else
        printf("   ");
    }
    for (i=j;i<(j+16<length?j+16:length);i++) {
      c=buf[i];
      if ((c>=32) && (c<127))
        printf ("%c", c);
      else
        printf (".");
    }
    j=i;
    printf("\n");
  }

  for(j=0; j<69; j++)
    printf("-");
  printf("\n");
}


static const lang_locale_t *_get_first_lang_locale(char *lcal) {
  const lang_locale_t *llocale;

  if(lcal && strlen(lcal)) {
    llocale = &*lang_locales;
    
    while(llocale->language) {
      if(!strncmp(lcal, llocale->language, strlen(lcal)))
	return llocale;
      
      llocale++;
    }
  }
  return NULL;
}


static char *_get_lang(void) {
    char *lang;
    
    if(!(lang = getenv("LC_ALL")))
      if(!(lang = getenv("LC_MESSAGES")))
        lang = getenv("LANG");

  return lang;
}


/*
 * get encoding of current locale
 */
char *xine_get_system_encoding(void) {
  char *codeset = NULL;
  
#ifdef HAVE_LANGINFO_CODESET
  codeset = nl_langinfo(CODESET);
#endif
  /*
   * guess locale codeset according to shell variables
   * when nl_langinfo(CODESET) isn't available or workig
   */
  if (!codeset || strstr(codeset, "ANSI") != 0) {
    char *lang = _get_lang();

    codeset = NULL;

    if(lang) {
      char *lg, *enc, *mod;

      lg = strdup(lang);

      if((enc = strchr(lg, '.')) && (strlen(enc) > 1)) {
        enc++;

        if((mod = strchr(enc, '@')))
          *mod = '\0';

        codeset = strdup(enc);
      }
      else {
        const lang_locale_t *llocale = _get_first_lang_locale(lg);

        if(llocale && llocale->encoding)
          codeset = strdup(llocale->encoding);
      }

      free(lg);
    }
  } else
    codeset = strdup(codeset);

  return codeset;
}


/*
 * guess default encoding of subtitles
 */
const char *xine_guess_spu_encoding(void) {
  char *lang = _get_lang();

  if (lang) {
    const lang_locale_t *llocale;
    char *lg, *enc;

    lg = strdup(lang);

    if ((enc = strchr(lg, '.'))) *enc = '\0';
    llocale = _get_first_lang_locale(lg);
    free(lg);
    if (llocale && llocale->spu_encoding) return llocale->spu_encoding;
  }

  return "iso-8859-1";
}


#ifndef HAVE_BASENAME
#define FILESYSTEM_PREFIX_LEN(filename) 0
#define ISSLASH(C) ((C) == '/')
#endif

/*
 * get base name
 *
 * (adopted from sh-utils)
 */
char *xine_basename (char *name) {
#ifndef HAVE_BASENAME
  char const *base = name + FILESYSTEM_PREFIX_LEN (name);
  char const *p;

  for (p = base; *p; p++)
    {
      if (ISSLASH (*p))
	{
	  /* Treat multiple adjacent slashes like a single slash.  */
	  do p++;
	  while (ISSLASH (*p));

	  /* If the file name ends in slash, use the trailing slash as
	     the basename if no non-slashes have been found.  */
	  if (! *p)
	    {
	      if (ISSLASH (*base))
		base = p - 1;
	      break;
	    }

	  /* *P is a non-slash preceded by a slash.  */
	  base = p;
	}
    }

  return (char *) base;
#else
  return basename(name);
#endif
}

/**
 * get error descriptions in DNS lookups
 */
const char *xine_hstrerror(int err) {
#ifndef HAVE_HSTRERROR
  switch (err) {
    case 0: return _("No error");
    case HOST_NOT_FOUND: return _("Unknown host");
    case NO_DATA: return _("No address associated with name");
    case NO_RECOVERY: return _("Unknown server error");
    case TRY_AGAIN: return _("Host name lookup failure");
    default: return _("Unknown error");
  }
#else
  return hstrerror(err);
#endif
}
