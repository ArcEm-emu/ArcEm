/*
  Copyright (C) 2005-2010 Matthew Howkins

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define PATH_MAX 1024
#include "win/dirent.h" /* Thanks http://www.softagalleria.net/dirent.php! */
#include <direct.h>
#include <io.h>
#define rmdir _rmdir
#else
#include <dirent.h>
#include <unistd.h>
#endif
#if defined __unix || defined __MACH__ || defined __riscos__
#include <utime.h>
#else
#include <sys/utime.h>
#endif
#include <sys/stat.h>
#include <limits.h>
#include <stdint.h>
#ifdef AMIGA
#include <sys/syslimits.h>
#endif
#ifndef _MSC_VER
#include <stdbool.h>
#endif
#include <sys/types.h>

#ifdef __riscos__
#include "kernel.h"
#include "swis.h"

#ifdef __TARGET_UNIXLIB__ /* Set by GCC if we're using unixlib */
#include <unixlib/local.h>
int __riscosify_control = 0;
#endif

#define HOST_DIR_SEP_CHAR '.'
#define HOST_DIR_SEP_STR "."

#define NO_OPEN64

#else /* __riscos__ */

#define HOST_DIR_SEP_CHAR '/'
#define HOST_DIR_SEP_STR "/"

#endif /* !__riscos__ */

#include "hostfs.h"

#if defined NO_OPEN64 || defined __MACH__
/* ftello64/fseeko64 don't exist, but ftello/fseeko do. Likewise, we need to use regular fopen. */
#define ftello64 ftello
#define fseeko64 fseeko
#define fopen64 fopen
#define off64_t uint32_t
#endif

#ifdef HOSTFS_ARCEM

#include "arch/armarc.h"
#include "arch/ArcemConfig.h"
#include "arch/filecalls.h"
#include "ControlPane.h"

#define HOSTFS_ROOT hArcemConfig.sHostFSDirectory

#else /* HOSTFS_ARCEM */

#include "arm.h"
#include "mem.h"

static char HOSTFS_ROOT[512];

#endif /* !HOSTFS_ARCEM */

#define HOSTFS_PROTOCOL_VERSION 1

/* Windows mkdir() function only takes one argument name, and
   name clashes with Posix mkdir() function taking two. This
   macro allows us to use one API to work with both variants */
#if (defined _WIN32 || defined __WIN32__) && ! defined __CYGWIN__
# define mkdir(name, mode) _mkdir(name)
#endif

/* Visual Studio doesn't have ftruncate; use _chsize instead */
#if defined _MSC_VER
# define ftruncate _chsize
#endif

/** Registration states of HostFS module with backend code */
typedef enum {
  HOSTFS_STATE_UNREGISTERED,    /**< Module not yet registered */
  HOSTFS_STATE_REGISTERED,  /**< Module successfully registered */
  HOSTFS_STATE_IGNORE,      /**< Ignoring activity after failing to register */
} HostFSState;

enum OBJECT_TYPE {
  OBJECT_TYPE_NOT_FOUND = 0,
  OBJECT_TYPE_FILE      = 1,
  OBJECT_TYPE_DIRECTORY = 2,
  OBJECT_TYPE_IMAGEFILE = 3,
};

enum OPEN_MODE {
  OPEN_MODE_READ               = 0,
  OPEN_MODE_CREATE_OPEN_UPDATE = 1, /* Only used by RISC OS 2 */
  OPEN_MODE_UPDATE             = 2,
};

enum FILE_INFO_WORD {
  FILE_INFO_WORD_WRITE_OK           = 1U << 31,
  FILE_INFO_WORD_READ_OK            = 1U << 30,
  FILE_INFO_WORD_IS_DIR             = 1U << 29,
  FILE_INFO_WORD_UNBUFFERED_OK      = 1U << 28,
  FILE_INFO_WORD_STREAM_INTERACTIVE = 1U << 27,
};

enum FILECORE_ERROR {
  FILECORE_ERROR_BADRENAME   = 0xb0,
  FILECORE_ERROR_DIRNOTEMPTY = 0xb4,
  FILECORE_ERROR_ACCESS      = 0xbd,
  FILECORE_ERROR_TOOMANYOPEN = 0xc0, /* Too many open files */
  FILECORE_ERROR_OPEN        = 0xc2, /* File open */
  FILECORE_ERROR_LOCKED      = 0xc3,
  FILECORE_ERROR_EXISTS      = 0xc4, /* Already exists */
  FILECORE_ERROR_DISCFULL    = 0xc6,
  FILECORE_ERROR_NOTFOUND    = 0xd6, /* Not found */
  HOSTFS_ERROR_UNKNOWN       = 0x100, /* Not a filecore error, but causes HostFS to return 'an unknown error occured' */
};

enum RISC_OS_FILE_TYPE {
  RISC_OS_FILE_TYPE_OBEY = 0xfeb,
  RISC_OS_FILE_TYPE_DATA = 0xffd,
  RISC_OS_FILE_TYPE_TEXT = 0xfff,
};

typedef struct {
  ARMword type;
  ARMword load;
  ARMword exec;
  ARMword length;
  ARMword attribs;
} risc_os_object_info;

/**
 * Type used to cache information about a directory entry.
 * Contains name and RISC OS object info
 */
typedef struct {
  unsigned name_offset; /**< Offset within cache_names[] */
  risc_os_object_info object_info;
} cache_directory_entry;

/* TODO Avoid duplicate macro with extnrom.c */
#define ROUND_UP_TO_4(x) (((x) + 3) & (~3))

#define STREQ(x,y)     (strcmp(x,y) == 0)
#ifdef _MSC_VER
#define STRCASEEQ(x,y) (stricmp(x,y) == 0)
#else
#define STRCASEEQ(x,y) (strcasecmp(x,y) == 0)
#endif

#define MAX_OPEN_FILES 255

#define DEFAULT_ATTRIBUTES  0x03
#define DEFAULT_FILE_TYPE   RISC_OS_FILE_TYPE_TEXT
#define MINIMUM_BUFFER_SIZE 32768

static FILE *open_file[MAX_OPEN_FILES + 1]; /* array subscript 0 is never used */

static unsigned char *buffer = NULL;
static size_t buffer_size = 0;

static cache_directory_entry *cache_entries = NULL;
static unsigned cache_entries_count = 0; /**< Number of valid entries in \a cache_entries */
static char *cache_names = NULL;

/** Current registration state of HostFS module with backend code */
static HostFSState hostfs_state = HOSTFS_STATE_UNREGISTERED;

#ifdef NDEBUG
static inline void dbug_hostfs(const char *format, ...) {}
#else
static void
dbug_hostfs(const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}
#endif

static void
path_construct(const char *old_path, const char *ro_path,
               char *new_path, size_t len, ARMword load, ARMword exec);

static ARMword
errno_to_hostfs_error(const char *filename,const char *function,const char *op)
{
  switch(errno) {
  case ENOMEM: /* Out of memory */
    hostfs_error(EXIT_FAILURE,"HostFS out of memory in %s: \'%s\'\n",function,strerror(errno));
    break;

  case ENOENT: /* Object not found */
    return FILECORE_ERROR_NOTFOUND;

  case EACCES: /* Access violation */
  case EPERM:
    return FILECORE_ERROR_ACCESS;

  case EEXIST:
  case ENOTEMPTY: /* POSIX permits either error for directory not empty */
    return FILECORE_ERROR_DIRNOTEMPTY;

  case ENOSPC: /* No space for the directory (either physical or quota) */
    return FILECORE_ERROR_DISCFULL;

#ifdef __riscos__
  case EOPSYS: /* RISC OS error */
  {
    _kernel_oserror *err = _kernel_last_oserror();
    if(err && ((err->errnum >> 16) == 1) && ((err->errnum & 0xff) >= 0xb0))
    {
      /* Filesystem error, in the range HostFS will accept. Return it directly. */
      return err->errnum & 0xff;
    }
    else
    {
      fprintf(stderr,"%s() could not %s '%s': %s %d\n",function,op,filename,strerror(errno),errno);
      return HOSTFS_ERROR_UNKNOWN;
    }
  }
#endif

  default:
    fprintf(stderr,"%s() could not %s '%s': %s %d\n",function,op,filename,strerror(errno),errno);
    return HOSTFS_ERROR_UNKNOWN;
  }
}


/**
 * @param buffer_size_needed Required buffer
 */
static void
hostfs_ensure_buffer_size(size_t buffer_size_needed)
{
  if (buffer_size_needed > buffer_size) {
    buffer = realloc(buffer, buffer_size_needed);
    if (!buffer) {
      hostfs_error(EXIT_FAILURE,"HostFS could not increase buffer size to %lu bytes\n",
              (unsigned long) buffer_size_needed);
    }
    buffer_size = buffer_size_needed;
  }
}

/**
 * @param state   Emulator state
 * @param address Address in emulated memory
 * @param buf     Returned string (filled-in)
 * @param bufsize Size of passed-in buffer
 */
static void
get_string(ARMul_State *state, ARMword address, char *buf, size_t bufsize)
{
  assert(state);
  assert(buf);

  /* TODO Ensure we do not overrun the end of the passed-in space,
     using the bufsize parameter */
  while ((*buf = ARMul_LoadByte(state, address)) != '\0') {
    buf++;
    address++;
  }
}

/**
 * @param state   Emulator state
 * @param address Address in emulated memory
 * @param str     The string to store
 * @return The length of the string (including terminator) rounded up to word
 */
static ARMword
put_string(ARMul_State *state, ARMword address, const char *str)
{
  ARMword len = 0;

  assert(state);
  assert(str);

  while (*str) {
    ARMul_StoreByte(state, address++, *str++);
    len++;
  }

  /* Write terminator */
  ARMul_StoreByte(state, address, '\0');

  return ROUND_UP_TO_4(len + 1);
}

#ifndef __riscos__
/**
  * @param load RISC OS load address (assumed to be time-stamped)
  * @param exec RISC OS exec address (assumed to be time-stamped)
  * @return Time converted to time_t format
  *
  * Code adapted from fs/adfs/inode.c from Linux licensed under GPL2.
  * Copyright (C) 1997-1999 Russell King
  */
static time_t
hostfs_adfs2host_time(ARMword load, ARMword exec)
{
  ARMword high = load << 24;
  ARMword low  = exec;

  high |= low >> 8;
  low &= 0xff;

  if (high < 0x3363996a) {
    /* Too early */
    return 0;
  } else if (high >= 0x656e9969) {
    /* Too late */
    return 0x7ffffffd;
  }

  high -= 0x336e996a;
  return (((high % 100) << 8) + low) / 100 + (high / 100 << 8);
}
#endif

/**
 * Apply load/exec attributes to host file
 *
 * Note that on non-RISC OS hosts this only applies the time stamp; renaming
 * due to filetype/load/exec changes is expected to be handled by the caller
 *
 * @param host_path Full path to object (file or dir) in host format
 * @param load      RISC OS load address (may contain time-stamp)
 * @param exec      RISC OS exec address (may contain time-stamp)
 */
static void
hostfs_object_set_loadexec(const char *host_path, ARMword load, ARMword exec)
{
#ifdef __riscos__
  _swix(OS_File,_INR(0,2),2,host_path,load);
  _swix(OS_File,_INR(0,1)|_IN(3),3,host_path,exec);
#else
  /* Test if Load and Exec contain time-stamp */
  if ((load & 0xfff00000u) == 0xfff00000u) {
    struct utimbuf t;

    t.actime = t.modtime = hostfs_adfs2host_time(load, exec);
    utime(host_path, &t);
    /* TODO handle error in utime() */
  }
#endif
}

/**
 * Apply attributes to host file
 *
 * @param ro_path   RISC OS path to object
 * @param host_path Full path to object (file or dir) in host format
 * @param load      RISC OS load address (may contain time-stamp)
 * @param exec      RISC OS exec address (may contain time-stamp)
 * @param attribs   RISC OS file attributes
 */
static void
hostfs_object_set_attribs(const char *ro_path, const char *host_path, ARMword load, ARMword exec,ARMword attribs)
{
#ifdef __riscos__
  _swix(OS_File,_INR(0,3)|_IN(5),1,host_path,load,exec,attribs);
#else
  char new_pathname[PATH_MAX];

  path_construct(host_path, ro_path,
                 new_pathname, sizeof(new_pathname),
                 load, exec);

  if (rename(host_path, new_pathname)) {
    /* TODO handle error in renaming */
  }

  /* Update timestamp if necessary */
  hostfs_object_set_loadexec(new_pathname, load, exec);

  /* TODO handle attributes */
#endif
}

static void
riscos_path_to_host(const char *path, char *host_path)
{
  assert(path);
  assert(host_path);

  while (*path) {
    switch (*path) {
    case '$':
      strcpy(host_path, HOSTFS_ROOT);
      host_path += strlen(host_path);
      break;
#ifndef __riscos__ /* No translation needed on RISC OS */
    case '.':
      *host_path++ = HOST_DIR_SEP_CHAR;
      break;
    case HOST_DIR_SEP_CHAR:
      *host_path++ = '.';
      break;
#endif
    default:
      *host_path++ = *path;
      break;
    }
    path++;
  }

  *host_path = '\0';
}

#ifndef __riscos__
/**
 * @param object_name Name of Host object (file or directory)
 * @param len         Length of the part of the name to convert
 * @param riscos_name Return object name in RISC OS format (filled-in)
 */
static void
name_host_to_riscos(const char *object_name, size_t len, char *riscos_name)
{
  assert(object_name);
  assert(riscos_name);

  while (len--) {
    switch (*object_name) {
    case '.':
      *riscos_name++ = HOST_DIR_SEP_CHAR;
      break;
    case HOST_DIR_SEP_CHAR:
      *riscos_name++ = '.';
      break;
    case 32:
      *riscos_name++ = 160;
      break;
    default:
      *riscos_name++ = *object_name;
      break;
    }
    object_name++;
  }

  *riscos_name = '\0';
}
#endif

/**
 * Construct a new Host path based on an existing Host path,
 * and modifying it using the leaf of the RISC OS path, and the
 * load & exec addresses
 *
 * @param old_path Existing Host path
 * @param ro_path  New RISC OS path (of which the leaf will be extracted)
 * @param new_path New Host path (filled-in)
 * @param len      Size of buffer for new Host path
 * @param load     RISC OS load address (may also be filetyped)
 * @param exec     RISC OS exec address (may also be filetyped)
 */
static void
path_construct(const char *old_path, const char *ro_path,
               char *new_path, size_t len, ARMword load, ARMword exec)
{
  const char *ro_leaf;
#ifndef __riscos__
  char *new_suffix;
#endif

  assert(old_path);
  assert(ro_path);
  assert(new_path);

  /* TODO Ensure buffer safety is observed */

  /* Start by basing new Host path on the old one */
  strcpy(new_path, old_path);

  /* Find the leaf of the RISC OS path */
  {
    const char *dot = strrchr(ro_path, '.');

    /* A '.' must be present in the RISC OS path,
       to prevent being passed "$" */
    assert(dot);

    ro_leaf = dot + 1;
  }

  /* Calculate where to place new leaf within the new host path */
  {
    char *slash, *new_leaf;

    slash = strrchr(new_path, HOST_DIR_SEP_CHAR);
    if (slash) {
      /* New leaf immediately follows final slash */
      new_leaf = slash + 1;
    } else {
      /* No slash currently in Host path, but we need one */
      /* New leaf is then appended */
      strcat(new_path, HOST_DIR_SEP_STR);
      new_leaf = new_path + strlen(new_path);
    }

    /* Place new leaf */
    riscos_path_to_host(ro_leaf, new_leaf);
  }

#ifndef __riscos__
  /* Calculate where to place new comma suffix */
  /* New suffix appended onto existing path */
  new_suffix = new_path + strlen(new_path);

  if ((load & 0xfff00000u) == 0xfff00000u) {
    ARMword filetype = (load >> 8) & 0xfff;

    /* File has filetype and timestamp */

    /* Don't set for default filetype */
    if (filetype != DEFAULT_FILE_TYPE) {
      sprintf(new_suffix, ",%03x", filetype);
    }
  } else {
    /* File has load and exec addresses */
    sprintf(new_suffix, ",%x-%x", load, exec);
  }
#endif
}

/**
 * @param host_pathname Full Host path to object
 * @param ro_leaf       Optionally return RISC OS leaf
 *                      (filled-in if requested, and object found)
 * @param object_info   Return object info (filled-in)
 */
static void
hostfs_read_object_info(const char *host_pathname,
                        char *ro_leaf,
                        risc_os_object_info *object_info)
{
#ifdef __riscos__

  assert(host_pathname);
  assert(object_info);

  _kernel_oserror *err = _swix(OS_File,_INR(0,1)|_OUT(0)|_OUTR(2,5),17,host_pathname,&object_info->type,&object_info->load,&object_info->exec,&object_info->length,&object_info->attribs);

  if(err)
  {
    fprintf(stderr,"hostfs_read_object_info() could not stat '%s': %d %s\n",host_pathname,err->errnum,err->errmess);
    object_info->type = OBJECT_TYPE_NOT_FOUND;
  }

  /* Handle image files as regular files
     TODO - Have some whitelist of image file types which we want to pass through to the emulator as directories?
  */
  if(object_info->type == OBJECT_TYPE_IMAGEFILE)
    object_info->type = OBJECT_TYPE_FILE;

  if((object_info->type != OBJECT_TYPE_NOT_FOUND) && ro_leaf)
    strcpy(ro_leaf,strrchr(host_pathname,'.')+1);

#else /* __riscos__ */

  struct stat info;
  ARMword file_type;
  bool is_timestamped = true; /* Assume initially it has timestamp/filetype */
  bool truncate_name = false; /* Whether to truncate for leaf
                                 (because filetype or load-exec found) */
  const char *slash, *comma;

  assert(host_pathname);
  assert(object_info);

  if (stat(host_pathname, &info)) {
    /* Error reading info about the object */

    switch (errno) {
    case ENOENT: /* Object not found */
    case ENOTDIR: /* A path component is not a directory */
      object_info->type = OBJECT_TYPE_NOT_FOUND;
      break;

    default:
      /* Other error */
      fprintf(stderr,
              "hostfs_read_object_info() could not stat() \'%s\': %s %d\n",
              host_pathname, strerror(errno), errno);
      object_info->type = OBJECT_TYPE_NOT_FOUND;
      break;
    }

    return;
  }

  /* We were able to read about the object */
  if (S_ISREG(info.st_mode)) {
    object_info->type = OBJECT_TYPE_FILE;
  } else if (S_ISDIR(info.st_mode)) {
    object_info->type = OBJECT_TYPE_DIRECTORY;
  } else {
    /* Treat types other than file or directory as not found */
    object_info->type = OBJECT_TYPE_NOT_FOUND;
    return;
  }

  file_type = DEFAULT_FILE_TYPE;

  /* Find where the leafname starts */
  slash = strrchr(host_pathname, HOST_DIR_SEP_CHAR);

  /* Find whether there is a comma in the leafname */
  if (slash) {
    /* Start search for comma after the slash */
    comma = strrchr(slash + 1, ',');
  } else {
    comma = strrchr(host_pathname, ',');
  }

  /* Search for a filetype or load-exec after a comma */
  if (comma) {
    const char *dash = strrchr(comma + 1, '-');

    /* Determine whether we have filetype or load-exec */
    if (dash) {
      /* Check the lengths of the portions before and after the dash */
      if ((dash - comma - 1) >= 1 && (dash - comma - 1) <= 8 &&
          strlen(dash + 1) >= 1 && strlen(dash + 1) <= 8)
      {
        /* Check there is no whitespace present, as sscanf() silently
           ignores it */
        const char *whitespace = strpbrk(comma + 1, " \f\n\r\t\v");

        if (!whitespace) {
          ARMword load, exec;

          if (sscanf(comma + 1, "%8x-%8x", &load, &exec) == 2) {
            object_info->load = load;
            object_info->exec = exec;
            is_timestamped = false;
            truncate_name = true;
          }
        }
      }
    } else if (strlen(comma + 1) == 3) {
      if (isxdigit(comma[1]) && isxdigit(comma[2]) && isxdigit(comma[3])) {
        file_type = (ARMword) strtoul(comma + 1, NULL, 16);
        truncate_name = true;
      }
    }
  }

  /* If the file has timestamp/filetype, instead of load-exec, then fill in */
  if (is_timestamped) {
    ARMword low  = (ARMword) ((info.st_mtime & 255) * 100);
    ARMword high = (ARMword) ((info.st_mtime / 256) * 100 + (low >> 8) + 0x336e996a);

    object_info->load = 0xfff00000 | (file_type << 8) | (high >> 24);
    object_info->exec = (low & 255) | (high << 8);
  }

  object_info->length  = info.st_size;
  object_info->attribs = DEFAULT_ATTRIBUTES;

  if (ro_leaf) {
    /* Allocate and return leafname for RISC OS */
    size_t ro_leaf_len;

    if (truncate_name) {
      /* If a filetype or load-exec was found, we only want the part from after
         the slash to before the comma */
      ro_leaf_len = comma - slash - 1;
    } else {
      /* Return everything from after the slash to the end */
      ro_leaf_len = strlen(slash + 1);
    }

    name_host_to_riscos(slash + 1, ro_leaf_len, ro_leaf);
  }

#endif /* !__riscos__ */
}

/**
 * @param host_dir_path Full Host path to directory to scan
 * @param object        Object name to search for
 * @param host_name     Return Host name of object (filled-in if object found)
 * @param object_info   Return object info (filled-in)
 */
static void
hostfs_path_scan(const char *host_dir_path,
                 const char *object,
                 char *host_name,
                 risc_os_object_info *object_info)
{
#ifdef __riscos__
  char path[PATH_MAX];

  assert(host_dir_path && object);
  assert(host_name);
  assert(object_info);

  /* This is nice and easy */

  sprintf(path,"%s.%s",host_dir_path,object);

  hostfs_read_object_info(path,NULL,object_info);

  if(object_info->type != OBJECT_TYPE_NOT_FOUND)
    strcpy(host_name,object);

#else /* __riscos__ */

  DIR *d;
  struct dirent *entry;
  size_t c;

  assert(host_dir_path && object);
  assert(host_name);
  assert(object_info);

  d = opendir(host_dir_path);
  if (!d) {
    switch (errno) {
    case ENOENT: /* Object not found */
    case ENOTDIR: /* Object not a directory */
      object_info->type = OBJECT_TYPE_NOT_FOUND;
      break;

    default:
      fprintf(stderr, "hostfs_path_scan() could not opendir() \'%s\': %s %d\n",
              host_dir_path, strerror(errno), errno);
      object_info->type = OBJECT_TYPE_NOT_FOUND;
    }

    return;
  }

  while ((entry = readdir(d)) != NULL) {
    char entry_path[PATH_MAX], ro_leaf[PATH_MAX];

    /* Ignore the current directory and it's parent */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    strcpy(entry_path, host_dir_path);
    strcat(entry_path, HOST_DIR_SEP_STR);
    strcat(entry_path, entry->d_name);

    hostfs_read_object_info(entry_path, ro_leaf, object_info);

        for (c=0;c<strlen(ro_leaf);c++)
        {
                if (ro_leaf[c]==HOST_DIR_SEP_CHAR)
                   ro_leaf[c]='.';
        }

    /* Ignore entries we can not read information about,
       or which are neither regular files or directories */
    if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
      continue;
    }

    /* Compare leaf and object names in case-insensitive manner */
    if (!STRCASEEQ(ro_leaf, object)) {
      /* Names do not match */
      continue;
    }

    /* A match has been found - exit the function early */
    strcpy(host_name, entry->d_name);
    closedir(d);
    return;
  }

  closedir(d);

  object_info->type = OBJECT_TYPE_NOT_FOUND;

#endif /* !__riscos__ */
}

/**
 * @param ro_path       Full RISC OS path to object
 * @param host_pathname Return full Host path to object (filled-in)
 * @param object_info   Return object info (filled-in)
 */
static void
hostfs_path_process(const char *ro_path,
                    char *host_pathname,
                    risc_os_object_info *object_info)
{
  char component_name[PATH_MAX]; /* working Host component */
  char *component;

  assert(ro_path);
  assert(host_pathname);
  assert(object_info);

  assert(ro_path[0] == '$');

  /* Initialise Host pathname */
  host_pathname[0] = '\0';

  /* Initialise working Host component */
  component = &component_name[0];
  *component = '\0';

  while (*ro_path) {
    switch (*ro_path) {
    case '$':
      strcat(host_pathname, HOSTFS_ROOT);

      hostfs_read_object_info(host_pathname, NULL, object_info);
      if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
        return;
      }

      break;

    case '.':
      if (component_name[0] != '\0') {
        /* only if not first dot, i.e. "$." */

        char host_name[PATH_MAX];

        *component = '\0'; /* add terminator */

        hostfs_path_scan(host_pathname, component_name,
                         host_name, object_info);
        if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
          /* This component of the path is invalid */
          /* Return what we have of the host_pathname */

          return;
        }

        /* Append Host's name for this component to the working Host path */
        strcat(host_pathname, HOST_DIR_SEP_STR);
        strcat(host_pathname, host_name);

        /* Reset component name ready for re-use */
        component = &component_name[0];
        *component = '\0';
      }
      break;

#ifndef __riscos__
    case HOST_DIR_SEP_CHAR:
      *component++ = '.';
      break;
#endif

    default:
      *component++ = *ro_path;
      break;
    }

    ro_path++;
  }

  if (component_name[0] != '\0') {
    /* only if not first dot, i.e. "$." */

    char host_name[PATH_MAX];

    *component = '\0'; /* add terminator */

    hostfs_path_scan(host_pathname, component_name,
                     host_name, object_info);
    if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
      /* This component of the path is invalid */
      /* Return what we have of the host_pathname */

      return;
    }

    /* Append Host's name for this component to the working Host path */
    strcat(host_pathname, HOST_DIR_SEP_STR);
    strcat(host_pathname, host_name);
  }
}

/* Search through the open_file[] array, and allocate an index.
   A valid index will be >0 and <=MAX_OPEN_FILES
   A return of 0 indicates that no array index could be allocated.
 */
static unsigned
hostfs_open_allocate_index(void)
{
  unsigned i;

  /* TODO Use the buffer in a circular manner for improved performance */

  /* Start our search at array index 1.
     Reserve a return of 0 for a special meaning: no free entry */
  for (i = 1; i < (MAX_OPEN_FILES + 1); i++) {
    if (open_file[i] == NULL) {
      return i;
    }
  }
  return 0;
}

static void
hostfs_open(ARMul_State *state)
{
  char ro_path[PATH_MAX], host_pathname[PATH_MAX];
  risc_os_object_info object_info;
  unsigned idx;

  assert(state);

  dbug_hostfs("Open\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to pathname)\n", state->Reg[1]);
  dbug_hostfs("\tr3 = %u (FileSwitch handle)\n", state->Reg[3]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
          state->Reg[6]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, host_pathname, &object_info);

  if (object_info.type == OBJECT_TYPE_NOT_FOUND) {
    /* FIXME RISC OS uses this to create files - not therefore an error if not found */
    state->Reg[1] = 0; /* Signal to RISC OS file not found */
    return;
  }

  /* TODO Handle the case that a file exists to be replaced, (and the filetype is
     not data - the recommeded default for new files) */

  idx = hostfs_open_allocate_index();
  if (idx == 0) {
    /* No more space in the open_file[] array.
       This should never occur, because RISC OS is constraining the max
       number of open files */
    abort();
  }


  switch (state->Reg[0]) {
  case OPEN_MODE_READ:
    dbug_hostfs("\tOpen for read\n");
    open_file[idx] = fopen64(host_pathname, "rb");
    state->Reg[0] = FILE_INFO_WORD_READ_OK;
    break;

  case OPEN_MODE_CREATE_OPEN_UPDATE:
    dbug_hostfs("\tCreate and open for update (only RISC OS 2)\n");
    return;

  case OPEN_MODE_UPDATE:
    dbug_hostfs("\tOpen for update\n");
    open_file[idx] = fopen64(host_pathname, "rb+");
    state->Reg[0] = (uint32_t) (FILE_INFO_WORD_READ_OK | FILE_INFO_WORD_WRITE_OK);
    break;
  }

  /* Check for errors from opening the file */
  if (open_file[idx] == NULL) {
    state->Reg[1] = 0; /* Signal to RISC OS file not found */
    state->Reg[9] = errno_to_hostfs_error(host_pathname,__FUNCTION__,"open");
    return;
  }

  /* Find the extent of the file */
  fseeko64(open_file[idx], UINT64_C(0), SEEK_END);
  state->Reg[3] = (ARMword) ftello64(open_file[idx]);
  rewind(open_file[idx]); /* Return to start */

  dbug_hostfs("\tFile opened OK, handle %u, size %u\n",idx,state->Reg[3]);

  state->Reg[1] = idx; /* Our filing system's handle */
  state->Reg[2] = 1024; /* Buffer size to use in range 64-1024.
                           Must be power of 2 */
  state->Reg[4] = 0; /* Space allocated to file */
}

static void
hostfs_getbytes(ARMul_State *state)
{
  FILE *f = open_file[state->Reg[1]];
  ARMword ptr = state->Reg[2];

  assert(state);

  dbug_hostfs("GetBytes\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (ptr to buffer)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = %u (number of bytes to read)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (file offset from which to get data)\n",
              state->Reg[4]);

  fseeko64(f, (off64_t) state->Reg[4], SEEK_SET);

  File_ReadRAM(state, f, ptr, state->Reg[3]);
}

static void
hostfs_putbytes(ARMul_State *state)
{
  FILE *f = open_file[state->Reg[1]];
  ARMword ptr = state->Reg[2];

  assert(state);

  dbug_hostfs("PutBytes\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (ptr to buffer)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = %u (number of bytes to write)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (file offset at which to put data)\n",
              state->Reg[4]);

  fseeko64(f, (off64_t) state->Reg[4], SEEK_SET);

  File_WriteRAM(state, f, ptr, state->Reg[3]);
}

static void
hostfs_args_3_write_file_extent(ARMul_State *state)
{
  FILE *f;
  int fd;

  assert(state);

  f = open_file[state->Reg[1]];

  dbug_hostfs("\tWrite file extent\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = %u (new extent)\n", state->Reg[2]);

  /* Flush any pending I/O before moving to low-level I/O functions */
  if (fflush(f)) {
    fprintf(stderr, "hostfs_args_3_write_file_extent() bad fflush(): %s %d\n",
            strerror(errno), errno);
    return;
  }

  /* Obtain underlying file descriptor for this FILE* */
  fd = fileno(f);
  if (fd < 0) {
    fprintf(stderr, "hostfs_args_3_write_file_extent() bad fd: %s %d\n",
            strerror(errno), errno);
    return;
  }

#ifndef __amigaos3__ // no ftruncate in libnix - not sure if this is a critical function
  /* Set file to required extent */
  /* FIXME Not defined if file is increased in size */
  if (ftruncate(fd, (off_t) state->Reg[2])) {
    fprintf(stderr, "hostfs_args_3_write_file_extent() bad ftruncate(): %s %d\n",
            strerror(errno), errno);
    return;
  }
#endif
}

static void
hostfs_args_7_ensure_file_size(ARMul_State *state)
{
  FILE *f;

  assert(state);

  f = open_file[state->Reg[1]];

  dbug_hostfs("\tEnsure file size\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = %u (size of file to ensure)\n", state->Reg[2]);

  fseeko64(f, UINT64_C(0), SEEK_END);

  state->Reg[2] = (ARMword) ftello64(f);
}

static void
hostfs_args_8_write_zeros(ARMul_State *state)
{
  const unsigned BUFSIZE = MINIMUM_BUFFER_SIZE;
  FILE *f;
  ARMword length;

  assert(state);

  f = open_file[state->Reg[1]];

  dbug_hostfs("\tWrite zeros to file\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = %u (file offset at which to write)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = %u (number of zero bytes to write)\n", state->Reg[3]);

  fseeko64(f, (off64_t) state->Reg[2], SEEK_SET);

  hostfs_ensure_buffer_size(BUFSIZE);
  memset(buffer, 0, BUFSIZE);

  length = state->Reg[3];
  while (length > 0) {
    size_t buffer_amount = MIN(length, BUFSIZE);
    size_t written;

    written = fwrite(buffer, 1, buffer_amount, f);
    if (written < buffer_amount) {
      fprintf(stderr, "fwrite(): %s\n", strerror(errno));
      return;
    }
    length -= written;
  }
}

static void
hostfs_args_9_read_file_datestamp(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tRead file datestamp\n");
}

static void
hostfs_args(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("Args %u\n", state->Reg[0]);
  switch (state->Reg[0]) {
  case 3:
    hostfs_args_3_write_file_extent(state);
    break;
  case 7:
    hostfs_args_7_ensure_file_size(state);
    break;
  case 8:
    hostfs_args_8_write_zeros(state);
    break;
  case 9:
    hostfs_args_9_read_file_datestamp(state);
    break;
  }
}

static void
hostfs_close(ARMul_State *state)
{
  FILE *f;
  ARMword load, exec;

  assert(state);

  f = open_file[state->Reg[1]];
  load = state->Reg[2];
  exec = state->Reg[3];

  dbug_hostfs("Close\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (new load address)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = 0x%08x (new exec address)\n", state->Reg[3]);

  /* Close the file */
  fclose(f);

  /* Free up the open_file[] entry */
  open_file[state->Reg[1]] = NULL;

  /* If load and exec addresses are both 0, then nothing to do */
  if (load == 0 && exec == 0) {
    return;
  }

  /* TODO Apply the load and exec addresses if file was open for write */
}

/**
 * Code common to FSEntry_File 0 (Save) and FSEntry_File 7 (Create)
 *
 * @param state     Emulator state
 * @param with_data Whether to save the data from the supplied block
 *                  (Save) or not (Create)
 */
static void
hostfs_write_file(ARMul_State *state, bool with_data)
{
  const unsigned BUFSIZE = MINIMUM_BUFFER_SIZE;
  char ro_path[PATH_MAX];
  char host_pathname[PATH_MAX], new_pathname[PATH_MAX];
  ARMword length, ptr;
  risc_os_object_info object_info;
  FILE *f;
  size_t bytes_written=0;

  assert(state);

  dbug_hostfs("\tr1 = 0x%08x (ptr to filename)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (load address)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = 0x%08x (exec address)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = 0x%08x (ptr to buffer start)\n", state->Reg[4]);
  dbug_hostfs("\tr5 = 0x%08x (ptr to buffer end)\n", state->Reg[5]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  length = state->Reg[5] - state->Reg[4];
  ptr = state->Reg[4];

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, host_pathname, &object_info);
  dbug_hostfs("\tHOST_PATHNAME = %s\n", host_pathname);

  switch (object_info.type) {
  case OBJECT_TYPE_NOT_FOUND:
    strcat(host_pathname, HOST_DIR_SEP_STR);
    path_construct(host_pathname, ro_path,
                   new_pathname, sizeof(new_pathname),
                   state->Reg[2], state->Reg[3]);
    break;

  case OBJECT_TYPE_FILE:
    /* If the hostfs_path_process() reported object found,
       rename to the new name */
    path_construct(host_pathname, ro_path,
                   new_pathname, sizeof(new_pathname),
                   state->Reg[2], state->Reg[3]);
    if (rename(host_pathname, new_pathname)) {
      state->Reg[9] = errno_to_hostfs_error(host_pathname,__FUNCTION__,"rename");
      return;
    }
    break;

  case OBJECT_TYPE_DIRECTORY:
    /* TODO Find a suitable error */
    state->Reg[9] = HOSTFS_ERROR_UNKNOWN;
    return;
  }

  f = fopen64(new_pathname, "wb");
  if (!f) {
    state->Reg[9] = errno_to_hostfs_error(new_pathname,__FUNCTION__,"open");
    return;
  }

  if (with_data) {
    bytes_written = File_WriteRAM(state,f,ptr,length);
  } else {
    /* Fill the data buffer with 0's if we are not saving supplied data */
    hostfs_ensure_buffer_size(BUFSIZE);
    memset(buffer, 0, BUFSIZE);
    while(length > 0) {
      size_t buffer_amount = MIN(length,BUFSIZE);
      /* TODO check for errors */
      size_t temp = fwrite(buffer, 1, buffer_amount, f);
      length -= temp;
      bytes_written += temp;
      if(temp != buffer_amount)
        break;
    }
  }

  if(bytes_written != (state->Reg[5] - state->Reg[4]))
  {
    fprintf(stderr,"hostfs_write_file(): Failed to write full extent of file\n");
    /* TODO - Examine errno? */
    state->Reg[9] = HOSTFS_ERROR_UNKNOWN;
  }

  fclose(f); /* TODO check for errors */

  state->Reg[6] = 0; /* TODO */

  hostfs_object_set_loadexec(new_pathname, state->Reg[2], state->Reg[3]);
}

static void
hostfs_file_0_save_file(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tSave file\n");

  hostfs_write_file(state, true);
}

static void
hostfs_file_1_write_cat_info(ARMul_State *state)
{
  char ro_path[PATH_MAX], host_pathname[PATH_MAX];
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tWrite catalogue information\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to wildcarded filename)\n",
              state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (new load address)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = 0x%08x (new exec address)\n", state->Reg[3]);
  dbug_hostfs("\tr5 = 0x%08x (new attribs)\n", state->Reg[5]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  /* TODO Ensure we do not try to modify the root object: i.e. $ */

  hostfs_path_process(ro_path, host_pathname, &object_info);

  switch (object_info.type) {
  case OBJECT_TYPE_NOT_FOUND:
    /* We must not return an error if the object does not exist */
    return;

  case OBJECT_TYPE_FILE:
    dbug_hostfs("\thost_pathname = \"%s\"\n", host_pathname);

    hostfs_object_set_attribs(ro_path, host_pathname, state->Reg[2], state->Reg[3], state->Reg[5]);
    break;

  case OBJECT_TYPE_DIRECTORY:
    /* Do nothing for now - TODO Filecore systems normally handle this */
    return;
    break;

  default:
    abort();
  }
}

static void
hostfs_file_5_read_cat_info(ARMul_State *state)
{
  char ro_path[PATH_MAX], host_pathname[PATH_MAX];
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tRead catalogue information\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to pathname)\n", state->Reg[1]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, host_pathname, &object_info);

  state->Reg[0] = object_info.type;

  if (object_info.type != OBJECT_TYPE_NOT_FOUND) {
    state->Reg[2] = object_info.load;
    state->Reg[3] = object_info.exec;
    state->Reg[4] = object_info.length;
    state->Reg[5] = object_info.attribs;
  }
}

static void
hostfs_file_6_delete(ARMul_State *state)
{
  char ro_path[PATH_MAX], host_pathname[PATH_MAX];
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tDelete object\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to pathname)\n", state->Reg[1]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  /* Prevent being asked to delete root directory */
  if (STREQ(ro_path, "$")) {
    return;
  }

  hostfs_path_process(ro_path, host_pathname, &object_info);

  state->Reg[0] = object_info.type;

  if (object_info.type == OBJECT_TYPE_NOT_FOUND) {
    return;
  }

  state->Reg[2] = object_info.load;
  state->Reg[3] = object_info.exec;
  state->Reg[4] = object_info.length;
  state->Reg[5] = object_info.attribs;

  switch (object_info.type) {
  case OBJECT_TYPE_FILE:
    if (unlink(host_pathname)) {
      state->Reg[9] = errno_to_hostfs_error(host_pathname,__FUNCTION__,"delete file");
    }
    break;

  case OBJECT_TYPE_DIRECTORY:
    if (rmdir(host_pathname)) {
      state->Reg[9] = errno_to_hostfs_error(host_pathname,__FUNCTION__,"delete directory");
    }
    break;

  default:
    abort();
  }
}

static void
hostfs_file_7_create_file(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tCreate file\n");

  hostfs_write_file(state, false);
}

static void
hostfs_file_8_create_dir(ARMul_State *state)
{
  char ro_path[PATH_MAX];
  char host_pathname[PATH_MAX];
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tCreate directory\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to dirname)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (load address)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = 0x%08x (exec address)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (number of entries)\n", state->Reg[4]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  /* Prevent being asked to create root directory */
  if (STREQ(ro_path, "$")) {
    return;
  }

  hostfs_path_process(ro_path, host_pathname, &object_info);

  if (object_info.type != OBJECT_TYPE_NOT_FOUND) {
    /* The object already exists (not necessarily as a directory).
       Return with no error */
    return;
  }

  /* Construct path to new directory */
  {
    const char *dot = strrchr(ro_path, '.');
    const char *ro_leaf;
    char *new_leaf;

    /* A '.' must be present in the RISC OS path */
    if (!dot) {
      return;
    }

    /* Location of leaf in supplied RISC OS path */
    ro_leaf = dot + 1;

    strcat(host_pathname, HOST_DIR_SEP_STR);
    new_leaf = host_pathname + strlen(host_pathname);

    /* Place new leaf */
    riscos_path_to_host(ro_leaf, new_leaf);
  }

  dbug_hostfs("\tHOST_PATHNAME = %s\n", host_pathname);

  /* Create directory */
  if (mkdir(host_pathname, 0777)) {
    state->Reg[9] = errno_to_hostfs_error(host_pathname,__FUNCTION__,"create directory");
  }
}

static void
hostfs_file_255_load_file(ARMul_State *state)
{
  char ro_path[PATH_MAX], host_pathname[PATH_MAX];
  risc_os_object_info object_info;
  FILE *f;
  ARMword ptr;
  ARMword bytes_read;

  assert(state);

  ptr = state->Reg[2];

  dbug_hostfs("\tLoad file\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to wildcarded filename)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (address to load at)\n", state->Reg[2]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, host_pathname, &object_info);

  state->Reg[2] = object_info.load;
  state->Reg[3] = object_info.exec;
  state->Reg[4] = object_info.length;
  state->Reg[5] = object_info.attribs;
  state->Reg[6] = 0; /* TODO */

  f = fopen64(host_pathname, "rb");
  if (!f) {
    state->Reg[9] = errno_to_hostfs_error(host_pathname,__FUNCTION__,"open");
    return;
  }

  bytes_read = File_ReadRAM(state, f,ptr,state->Reg[4]);
  if(bytes_read != state->Reg[4])
  {
    fprintf(stderr,"hostfs_file_255_load_file(): Failed to read full extent of file\n");
    /* TODO - Examine errno? */
    state->Reg[9] = HOSTFS_ERROR_UNKNOWN;
  }

  fclose(f);
}

static void
hostfs_file(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("File %u\n", state->Reg[0]);
  switch (state->Reg[0]) {
  case 0:
    hostfs_file_0_save_file(state);
    break;
  case 1:
    hostfs_file_1_write_cat_info(state);
    break;
  case 5:
    hostfs_file_5_read_cat_info(state);
    break;
  case 6:
    hostfs_file_6_delete(state);
    break;
  case 7:
    hostfs_file_7_create_file(state);
    break;
  case 8:
    hostfs_file_8_create_dir(state);
    break;
  case 255:
    hostfs_file_255_load_file(state);
    break;
  }
}

static void
hostfs_func_0_chdir(ARMul_State *state)
{
  char ro_path[PATH_MAX];
  char host_path[PATH_MAX];

  assert(state);

  dbug_hostfs("\tSet current directory\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to wildcarded dir. name)\n", state->Reg[1]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  riscos_path_to_host(ro_path, host_path);
  dbug_hostfs("\tPATH = %s\n", ro_path);
  dbug_hostfs("\tPATH2 = %s\n", host_path);
}

static void
hostfs_func_8_rename(ARMul_State *state)
{
  char ro_path1[PATH_MAX], host_pathname1[PATH_MAX];
  char ro_path2[PATH_MAX], host_pathname2[PATH_MAX];
  risc_os_object_info object_info1, object_info2;
  char new_pathname[PATH_MAX];

  assert(state);

  dbug_hostfs("\tRename object\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to old name)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (ptr to new name)\n", state->Reg[2]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to 1st special field if present)\n",
              state->Reg[6]);
  dbug_hostfs("\tr7 = 0x%08x (pointer to 2nd special field if present)\n",
              state->Reg[7]);

  /* TODO When we support multiple virtual disks, check that rename would be
     'simple' */

  /* Process old path */
  get_string(state, state->Reg[1], ro_path1, sizeof(ro_path1));
  dbug_hostfs("\tPATH_OLD = %s\n", ro_path1);

  hostfs_path_process(ro_path1, host_pathname1, &object_info1);

  dbug_hostfs("\tHOST_PATH_OLD = %s\n", host_pathname1);

  /* Process new path */
  get_string(state, state->Reg[2], ro_path2, sizeof(ro_path2));
  dbug_hostfs("\tPATH_NEW = %s\n", ro_path2);

  hostfs_path_process(ro_path2, host_pathname2, &object_info2);

  dbug_hostfs("\tHOST_PATH_NEW = %s\n", host_pathname2);


  if (object_info1.type == OBJECT_TYPE_NOT_FOUND) {
    /* TODO Check if we need to handle this better */
    state->Reg[1] = 1; /* non-zero indicates could not rename */
    return;
  }

  if (object_info2.type != OBJECT_TYPE_NOT_FOUND) {
    /* The new named object does exist - check it is similar to the old
       name */
    if (!STRCASEEQ(ro_path1, ro_path2)) {
      state->Reg[1] = 1; /* non-zero indicates could not rename */
      return;
    }
  } else {
    strcat(host_pathname2, HOST_DIR_SEP_STR);
  }

  path_construct(host_pathname2, ro_path2,
                 new_pathname, sizeof(new_pathname),
                 object_info1.load, object_info1.exec);

  dbug_hostfs("\tNEW_PATHNAME = %s\n", new_pathname);

  if (rename(host_pathname1, new_pathname)) {
    /* An error occurred */

    fprintf(stderr, "HostFS could not rename \'%s\' to \'%s\': %s %d\n",
            host_pathname1, new_pathname, strerror(errno), errno);
    state->Reg[1] = 1; /* non-zero indicates could not rename */
    return;
  }

  state->Reg[1] = 0; /* zero indicates successful rename */
}

/**
 * Compare two elements of type \a cache_directory_entry by comparing their
 * names in a case-insensitive manner.
 *
 * @param e1 Pointer to first \a cache_directory_entry
 * @param e2 Pointer to second \a cache_directory_entry
 * @return Returns an integer less than, equal to, or greater than zero if
 *         e1's name is found, respectively, to be earlier than, to match, or
 *         be later than e2's name.
 */
static int
hostfs_directory_entry_compare(const void *e1, const void *e2)
{
  const cache_directory_entry *entry1 = e1;
  const cache_directory_entry *entry2 = e2;
  const char *name1 = cache_names + entry1->name_offset;
  const char *name2 = cache_names + entry2->name_offset;

  return STRCASEEQ(name1, name2);
}

/**
 * Reads the entries in the directory \a directory_name. Stores them in the
 * cache, sorted in case-insensitive order of name.
 *
 * @param directory_name Full path to host directory to be read and cached
 */
static void
hostfs_cache_dir(const char *directory_name)
{
  static unsigned cache_entries_capacity = 128; /* Initial capacity of cache_entries[] */
  static unsigned cache_names_capacity = 2048; /* Initial capacity of cache_names[] */

  unsigned entry_ptr = 0;
  unsigned name_ptr = 0;

#ifdef __riscos__
  struct {
    ARMword load;
    ARMword exec;
    ARMword length;
    ARMword attribs;
    ARMword type;
    char name[PATH_MAX];
  } gbpb_buffer;
  int gbpb_entry = 0;
  int found;
#else
  DIR *d;
  const struct dirent *entry;
#endif

  assert(directory_name);

  /* Allocate memory initially */
  if (!cache_entries) {
    cache_entries = malloc(cache_entries_capacity * sizeof(cache_directory_entry));
  }
  if (!cache_names) {
    cache_names = malloc(cache_names_capacity);
  }
  if ((!cache_entries) || (!cache_names)) {
    hostfs_error(1,"hostfs_cache_dir(): Out of memory\n");
  }

  /* Read each of the directory entries one at a time.
   * Fill in the cache_entries[] and cache_names[] arrays,
   *    resizing these dynamically if required.
   */
#ifdef __riscos__
  do {

    _kernel_oserror *err = _swix(OS_GBPB,_INR(0,6)|_OUTR(3,4),10,directory_name,&gbpb_buffer,1,gbpb_entry,sizeof(gbpb_buffer),NULL,&found,&gbpb_entry);

    if(err)
    {
      /* Hmm, this shouldn't happen. just give up. */
      fprintf(stderr,"hostfs_cache_dir(): Error %d %s\n",err->errnum,err->errmess);
      break;
    }

    if(found)
    {
      /* Copy over attributes */
      cache_entries[entry_ptr].object_info.type = (gbpb_buffer.type == OBJECT_TYPE_IMAGEFILE?OBJECT_TYPE_FILE:gbpb_buffer.type);
      cache_entries[entry_ptr].object_info.load = gbpb_buffer.load;
      cache_entries[entry_ptr].object_info.exec = gbpb_buffer.exec;
      cache_entries[entry_ptr].object_info.length = gbpb_buffer.length;
      cache_entries[entry_ptr].object_info.attribs = gbpb_buffer.attribs;

      unsigned string_space;

      /* Calculate space required to store name (+ terminator) */
      string_space = strlen(gbpb_buffer.name) + 1;

      /* Check whether cache_names[] is large enough; increase if required */
      if (string_space > (cache_names_capacity - name_ptr)) {
        cache_names_capacity *= 2;
        cache_names = realloc(cache_names, cache_names_capacity);
        if (!cache_names) {
          hostfs_error(1,"hostfs_cache_dir(): Out of memory\n");
        }
      }

      /* Copy string into cache_names[]. Put offset ptr into local_entries[] */
      strcpy(cache_names + name_ptr, gbpb_buffer.name);
      cache_entries[entry_ptr].name_offset = name_ptr;

      /* Advance name_ptr */
      name_ptr += string_space;

      /* Advance entry_ptr, increasing space of cache_entries[] if required */
      entry_ptr++;
      if (entry_ptr == cache_entries_capacity) {
        cache_entries_capacity *= 2;
        cache_entries = realloc(cache_entries, cache_entries_capacity * sizeof(cache_directory_entry));
        if (!cache_entries) {
          hostfs_error(1,"hostfs_cache_dir(): Out of memory\n");
        }
      }
    }
  } while(gbpb_entry != -1);

#else /* __riscos__ */

  d = opendir(directory_name);
  assert(d); /* FIXME */

  while ((entry = readdir(d)) != NULL) {
    char entry_path[PATH_MAX], ro_leaf[PATH_MAX];
    unsigned string_space;

    /* Ignore the current directory and it's parent */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    strcpy(entry_path, directory_name);
    strcat(entry_path, HOST_DIR_SEP_STR);
    strcat(entry_path, entry->d_name);

    hostfs_read_object_info(entry_path, ro_leaf,
                            &cache_entries[entry_ptr].object_info);

    /* Ignore entries we can not read information about,
       or which are neither regular files or directories */
    if (cache_entries[entry_ptr].object_info.type == OBJECT_TYPE_NOT_FOUND) {
      continue;
    }

    /* Calculate space required to store name (+ terminator) */
    string_space = strlen(ro_leaf) + 1;

    /* Check whether cache_names[] is large enough; increase if required */
    if (string_space > (cache_names_capacity - name_ptr)) {
      cache_names_capacity *= 2;
      cache_names = realloc(cache_names, cache_names_capacity);
      if (!cache_names) {
        hostfs_error(1,"hostfs_cache_dir(): Out of memory\n");
      }
    }

    /* Copy string into cache_names[]. Put offset ptr into local_entries[] */
    strcpy(cache_names + name_ptr, ro_leaf);
    cache_entries[entry_ptr].name_offset = name_ptr;

    /* Advance name_ptr */
    name_ptr += string_space;

    /* Advance entry_ptr, increasing space of cache_entries[] if required */
    entry_ptr++;
    if (entry_ptr == cache_entries_capacity) {
      cache_entries_capacity *= 2;
      cache_entries = realloc(cache_entries, cache_entries_capacity * sizeof(cache_directory_entry));
      if (!cache_entries) {
        hostfs_error(1,"hostfs_cache_dir(): Out of memory\n");
      }
    }
  }

  closedir(d);

#endif /* !__riscos__ */

  /* Sort the directory entries, case-insensitive */
  qsort(cache_entries, entry_ptr, sizeof(cache_directory_entry),
        hostfs_directory_entry_compare);

  /* Store the number of directory entries found */
  cache_entries_count = entry_ptr;
}

/**
 * Return directory information for FSEntry_Func 14, 15 and 19.
 * Uses and updates the cached directory information.
 *
 * @param state Emulator state
 * @param with_info Whether the returned data should include information with
 *                  each entry, or just names.
 * @param with_timestamp Whether the returned information should also include
 *                       the timestamp.
 */
static void
hostfs_read_dir(ARMul_State *state, bool with_info, bool with_timestamp)
{
  static char cached_directory[PATH_MAX] = { '\0' }; /* Directory stored in the cache */
  char ro_path[PATH_MAX], host_pathname[PATH_MAX];
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tr1 = 0x%08x (ptr to wildcarded dir. name)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (ptr to buffer for returned data)\n",
              state->Reg[2]);
  dbug_hostfs("\tr3 = %u (number of object names to read)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (offset of first item to read in dir)\n",
              state->Reg[4]);
  dbug_hostfs("\tr5 = %u (length of buffer)\n", state->Reg[5]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, host_pathname, &object_info);

  if (object_info.type != OBJECT_TYPE_DIRECTORY) {
    /* TODO Improve error return */
    state->Reg[3] = 0;
    state->Reg[4] = ~(UINT32_C(0));
    return;
  }

  /* Determine if we should use the cached directory contents or should re-read */
  if (!STREQ(host_pathname, cached_directory) || (state->Reg[4] == 0)) {
    hostfs_cache_dir(host_pathname);
  }

  {
    const ARMword num_objects_to_read = state->Reg[3];
    ARMword buffer_remaining = state->Reg[5]; /* buffer size given */
    ARMword count = 0; /* Number of objects returned from this call */
    ARMword offset = state->Reg[4]; /* Offset of item to read */
    ARMword ptr = state->Reg[2]; /* Pointer to return buffer */

    while ((count < num_objects_to_read) && (offset < cache_entries_count)) {
      unsigned string_space, entry_space;

      /* Calculate space required to return name and (optionally) info */
      string_space = (unsigned) strlen(cache_names + cache_entries[offset].name_offset) + 1;
      if (with_info) {
        if (with_timestamp) {
          /* Space required for info with timestamp:
             6 words of info, a 5-byte timestamp, and the string, rounded up */
          entry_space = ROUND_UP_TO_4((6 * sizeof(ARMword)) + 5 + string_space);
        } else {
          /* Space required for info:
             5 words of info, and the string, rounded up */
          entry_space = ROUND_UP_TO_4((5 * sizeof(ARMword)) + string_space);
        }
      } else {
        entry_space = string_space;
      }

      /* See whether there is space left in the buffer to return this entry */
      if (entry_space > buffer_remaining) {
        break;
      }

      /* Fill in this entry */
      if (with_info) {
        ARMul_StoreWordS(state, ptr + 0,  cache_entries[offset].object_info.load);
        ARMul_StoreWordS(state, ptr + 4,  cache_entries[offset].object_info.exec);
        ARMul_StoreWordS(state, ptr + 8,  cache_entries[offset].object_info.length);
        ARMul_StoreWordS(state, ptr + 12, cache_entries[offset].object_info.attribs);
        ARMul_StoreWordS(state, ptr + 16, cache_entries[offset].object_info.type);

        if (with_timestamp) {
          ARMul_StoreWordS(state, ptr + 20, 0); /* Always 0 */
          /* Test if Load and Exec contain timestamp */
          if ((cache_entries[offset].object_info.load & UINT32_C(0xfff00000)) == UINT32_C(0xfff00000)) {
            ARMul_StoreWordS(state, ptr + 24,
                             (cache_entries[offset].object_info.load << 24) |
                             (cache_entries[offset].object_info.exec >> 8));
            ARMul_StoreByte(state, ptr + 28,
                            cache_entries[offset].object_info.exec & 0xff);
          } else {
            ARMul_StoreWordS(state, ptr + 24, 0);
            ARMul_StoreByte(state, ptr + 28, 0);
          }
          ptr += 29;
        } else {
          ptr += 20;
        }
      }
      put_string(state, ptr, cache_names + cache_entries[offset].name_offset);

      ptr += string_space;
      if (with_info) {
        ptr = ROUND_UP_TO_4(ptr);
      }
      buffer_remaining -= entry_space;
      count ++;
      offset ++;
    }

    /* Find out whether we have now completed the directory */
    if (offset >= cache_entries_count && count == 0) {
      /* We have completed the directory - return this fact */
      dbug_hostfs("HostFS completed directory\n");
      state->Reg[4] = (uint32_t) -1;
    } else {
      /* We have not yet finished - return the offset for next time */
      state->Reg[4] = offset;
    }

    state->Reg[3] = count; /* Number of objects returned at this point */
  }
}

static void
hostfs_func_14_read_dir(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tRead directory entries\n");

  hostfs_read_dir(state, false, false);
}

static void
hostfs_func_15_read_dir_info(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tRead directory entries and information\n");

  hostfs_read_dir(state, true, false);
}

static void
hostfs_func_19_read_dir_info_timestamp(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tRead directory entries and information with timestamp\n");

  hostfs_read_dir(state, true, true);
}

static void
hostfs_func(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("Func %u\n", state->Reg[0]);
  switch (state->Reg[0]) {
  case 0:
    hostfs_func_0_chdir(state);
    break;

  case 8:
    hostfs_func_8_rename(state);
    break;

  case 11:
    dbug_hostfs("\tRead disc name and boot option\n");
    state->Reg[9] = HOSTFS_ERROR_UNKNOWN;
    break;

  case 14:
    hostfs_func_14_read_dir(state);
    break;

  case 15:
    hostfs_func_15_read_dir_info(state);
    break;

  case 19:
    hostfs_func_19_read_dir_info_timestamp(state);
    break;
  }
}

static void
hostfs_gbpb(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("GBPB\n");
}

/**
 * Entry point for module to register with emulator. This enables the backend
 * to verify support for the correct protocol and features.
 *
 * If the module requests a protocol version that is not supported this will
 * be logged and further actions ignored.
 *
 * @param state Emulator state
 */
static void
hostfs_register(ARMul_State *state)
{
  assert(state);

  /* Does R0 contain the supported protocol? */
  if (state->Reg[0] == HOSTFS_PROTOCOL_VERSION) {
    /* Successful registration - acknowledge by setting R0 to 0xffffffff */
    rpclog("HostFS: Registration request version %u accepted\n", state->Reg[0]);
    state->Reg[0] = 0xffffffff;
    hostfs_reset();
    hostfs_state = HOSTFS_STATE_REGISTERED;

  } else {
    /* Failed registration due to an unsupported version */
    rpclog("HostFS: Registration request version %u rejected\n", state->Reg[0]);
    hostfs_state = HOSTFS_STATE_IGNORE;
  }
}

/**
 * Initialise HostFS module. Called on program startup.
 */
void
hostfs_init(void)
{
#ifdef HOSTFS_ARCEM
  /* Do nothing for ArcEm */
#else
  int c;

  append_filename(HOSTFS_ROOT, rpcemu_get_datadir(), "hostfs", 511);
  for (c = 0; c < 511; c++) {
    if (HOSTFS_ROOT[c] == '\\') {
      HOSTFS_ROOT[c] = '/';
    }
  }
#endif
}

/**
 * Reset the HostFS state to initial values.
 */
void
hostfs_reset(void)
{
  unsigned i;

  hostfs_state = HOSTFS_STATE_UNREGISTERED;

  /* Close any open files */
  for (i = 1; i < (MAX_OPEN_FILES + 1); i++) {
    if (open_file[i]) {
      fclose(open_file[i]);
      open_file[i] = NULL;
    }
  }
}

/**
 * Entry point when the HostFS SWI is issued. The ARM register R0 must contain
 * the HostFS operation.
 *
 * @param state Emulator state
 */
void
hostfs(ARMul_State *state)
{
  assert(state);

  /* Allow attempts to register regardless of current state */
  if (state->Reg[9] == 0xffffffff) {
    hostfs_register(state);
    return;
  }

#if defined(__riscos__) && defined(__TARGET_UNIXLIB__)
  /* Temporarily disable the unixlib filename translation so we can safely
     use ANSI/POSIX file functions with native RISC OS paths */
  int old_riscosify = __riscosify_control;
  __riscosify_control = __RISCOSIFY_NO_PROCESS;
#endif

  /* Other HostFS operations depend on the current registration state */
  switch (hostfs_state) {
  case HOSTFS_STATE_REGISTERED:
    switch (state->Reg[9]) {
    case 0: hostfs_open(state);     break;
    case 1: hostfs_getbytes(state); break;
    case 2: hostfs_putbytes(state); break;
    case 3: hostfs_args(state);     break;
    case 4: hostfs_close(state);    break;
    case 5: hostfs_file(state);     break;
    case 6: hostfs_func(state);     break;
    case 7: hostfs_gbpb(state);     break;
    default:
      error("!!! ERROR !!! - unknown op in R9\n");
      break;
    }
    break;

  case HOSTFS_STATE_UNREGISTERED:
    /* Log attempt to use HostFS without registration and ignore further
       operations */
    rpclog("HostFS: Attempt to use HostFS without registration - ignoring\n");
    hostfs_state = HOSTFS_STATE_IGNORE;
    break;

  case HOSTFS_STATE_IGNORE:
    /* Ignore further HostFS operations after logging once */
    break;
  }

  if(state->Reg[9] >= 0xb0)
    fprintf(stderr,"\tReturning with error %x\n",state->Reg[9]);

#if defined(__riscos__) && defined(__TARGET_UNIXLIB__)
  __riscosify_control = old_riscosify;
#endif
}
