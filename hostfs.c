/*
 * $Id$
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include "arch/armarc.h"
#include "hostfs.h"

enum OBJECT_TYPE {
  OBJECT_TYPE_NOT_FOUND = 0,
  OBJECT_TYPE_FILE      = 1,
  OBJECT_TYPE_DIRECTORY = 2,
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
  FILECORE_ERROR_TOOMANYOPEN = 0xc0, /* Too many open files */
  FILECORE_ERROR_OPEN        = 0xc2, /* File open */
  FILECORE_ERROR_LOCKED      = 0xc3,
  FILECORE_ERROR_EXISTS      = 0xc4, /* Already exists */
  FILECORE_ERROR_DISCFULL    = 0xc6,
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

/* TODO Avoid duplicate macro with extnrom.c */
#define ROUND_UP_TO_4(x) (((x) + 3) & (~3))

#define HOSTFS_ROOT "./hostfs"

#define MAX_OPEN_FILES 255

#define NOT_IMPLEMENTED 255

#define DEFAULT_ATTRIBUTES  0x03
#define DEFAULT_FILE_TYPE   RISC_OS_FILE_TYPE_TEXT
#define MINIMUM_BUFFER_SIZE 32768

static FILE *open_file[MAX_OPEN_FILES + 1]; /* array subscript 0 is never used */

static unsigned char *buffer = NULL;
static size_t buffer_size = 0;

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

/**
 * @param buffer_size_needed Required buffer
 */
static void
hostfs_ensure_buffer_size(size_t buffer_size_needed)
{
  if (buffer_size_needed > buffer_size) {
    buffer = realloc(buffer, buffer_size_needed);
    if (!buffer) {
      fprintf(stderr, "HostFS could not increase buffer size to %u bytes\n",
              buffer_size_needed);
      exit(EXIT_FAILURE);
    }
    buffer_size = buffer_size_needed;
  }
}

/**
 * @param state   Emulator state
 * @param address Address in emulated memory
 * @return Pointer to newly-allocated string
 */
static char *
get_string(ARMul_State *state, ARMword address)
{
  char *buf = malloc(1024);
  char *cptr = buf;
  ARMword *wptr = (ARMword *) buf;

  assert(state);

  if (!buf) {
    fprintf(stderr, "Failure to malloc() in hostfs/get_string(): %s",
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  /* TODO Ensure we do not overrun the end of the malloc'ed space */
  for (;;) {
    *wptr = ARMul_LoadWordS(state, address);
    if (cptr[0] == '\0' || cptr[1] == '\0' ||
        cptr[2] == '\0' || cptr[3] == '\0')
    {
      return buf;
    }
    wptr++;
    cptr += 4;
    address += 4;
  }
}

/**
 * @param state   Emulator state
 * @param address Address in emulated memory
 * @param str     The string to store
 * @return The length of the string (including terminator) rounded up to word
 */
static unsigned
put_string(ARMul_State *state, ARMword address, const char *str)
{
  unsigned len = 0;

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

static void
path_construct(const char *old_path, char *new_path, size_t len,
               ARMword load, ARMword exec)
{
  char *comma, *new_suffix;

  /* TODO Ensure buffer safety is observed */

  strcpy(new_path, old_path);

  /* Calculate where to place new comma suffix */
  comma = strrchr(new_path, ',');
  if (comma) {
    /* New suffix overwrites existing comma suffix */
    new_suffix = comma;
  } else {
    /* New suffix appended onto existing path */
    new_suffix = new_path + strlen(new_path);
  }

  if ((load & 0xfff00000u) == 0xfff00000u) {
    /* File has filetype and timestamp */
    sprintf(new_suffix, ",%03x", (load >> 8) & 0xfff);
  } else {
    /* File has load and exec addresses */
    sprintf(new_suffix, ",%x-%x", load, exec);
  }
}

static char *
riscos_path_to_host(const char *path)
{
  char *result;
  char *host_path;

  assert(path);

  result = malloc((strlen(path) * 2) + 25);
  if (!result) {
    fprintf(stderr, "Failure to malloc() in hostfs/riscos_path_to_host(): %s",
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  host_path = result;

  while (*path) {
    switch (*path) {
    case '$':
      strcpy(host_path, HOSTFS_ROOT);
      host_path += strlen(host_path);
      break;
    case '.':
      *host_path++ = '/';
      break;
    case '/':
      *host_path++ = '.';
      break;
    default:
      *host_path++ = *path;
      break;
    }
    path++;
  }

  *host_path = '\0';

  return result;
}

static char *
name_host_to_riscos(const char *object_name, size_t len)
{
  char *result;
  char *riscos_name;

  assert(object_name);

  result = malloc(len + 1);
  if (!result) {
    fprintf(stderr, "Out of memory in name_host_to_riscos()\n");
    exit(EXIT_FAILURE);
  }

  riscos_name = result;

  while (len--) {
    switch (*object_name) {
    case '.':
      *riscos_name++ = '/';
      break;
    case '/':
      *riscos_name++ = '.';
      break;
    default:
      *riscos_name++ = *object_name;
      break;
    }
    object_name++;
  }

  *riscos_name = '\0';

  return result;
}

/**
 * @param host_pathname Full Host path to object
 * @param ro_leaf       Return RISC OS leaf (allocated if object found)
 * @param object_info   Return object info
 */
static void
hostfs_read_object_info(const char *host_pathname,
                        char **ro_leaf,
                        risc_os_object_info *object_info)
{
  struct stat info;
  ARMword file_type;
  int is_timestamped = 1; /* Assume initially it has timestamp/filetype */
  int truncate_name = 0; /* Whether to truncate for leaf
                            (because filetype or load-exec found) */
  const char *slash, *comma;
  unsigned ro_leaf_len;

  assert(host_pathname);
  assert(ro_leaf);
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
  slash = strrchr(host_pathname, '/');
  assert(slash); /* A '/' should always be present */

  /* Search for a filetype or load-exec after a comma */
  comma = strrchr(slash + 1, ',');
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
            is_timestamped = 0;
            truncate_name = 1;
          }
        }
      }
    } else if (strlen(comma + 1) == 3) {
      if (isxdigit(comma[1]) && isxdigit(comma[2]) && isxdigit(comma[3])) {
        file_type = (ARMword) strtoul(comma + 1, NULL, 16);
        truncate_name = 1;
      }
    }
  }

  /* If the file has timestamp/filetype, instead of load-exec, then fill in */
  if (is_timestamped) {
    ARMword low  = (info.st_mtime & 255) * 100;
    ARMword high = (info.st_mtime / 256) * 100 + (low >> 8) + 0x336e996a;

    object_info->load = 0xfff00000 | (file_type << 8) | (high >> 24);
    object_info->exec = (low & 255) | (high << 8);
  }

  object_info->length  = info.st_size;
  object_info->attribs = DEFAULT_ATTRIBUTES;

  /* Allocate and return leafname for RISC OS */
  if (truncate_name) {
    /* If a filetype or load-exec was found, we only want the part from after
       the slash to before the comma */
    ro_leaf_len = comma - slash; /* includes space for terminator */
  } else {
    /* Return everything from after the slash to the end */
    ro_leaf_len = strlen(slash + 1) + 1; /* +1 for terminator */
  }

#if 0
  *ro_leaf = malloc(ro_leaf_len);
  if (!(*ro_leaf)) {
    fprintf(stderr, "Out of memory in hostfs_read_object_info()\n");
    exit(EXIT_FAILURE);
  }

  strncpy(*ro_leaf, slash + 1, ro_leaf_len - 1);
  (*ro_leaf)[ro_leaf_len - 1] = '\0';
#endif
#if 1
  *ro_leaf = name_host_to_riscos(slash + 1, ro_leaf_len - 1);
#endif
}

/**
 * @param host_dir_path Full Host path to directory to scan
 * @param object        Object name to search for
 * @param host_name     Return Host name of object (allocated if object found)
 * @param ro_leaf       Return RISC OS leaf (allocated if object found)
 * @param object_info   Return object info
 */
static void
hostfs_path_scan(const char *host_dir_path,
                 const char *object,
                 char **host_name,
                 char **ro_leaf,
                 risc_os_object_info *object_info)
{
  DIR *d;
  struct dirent *entry;

  assert(host_dir_path && object);
  assert(host_name && ro_leaf);
  assert(object_info);

  d = opendir(host_dir_path);
  if (!d) {
    switch (errno) {
    case ENOENT: /* Object not found */
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
    char entry_path[PATH_MAX];

    /* Hidden files are completely ignored */
    if (entry->d_name[0] == '.') {
      continue;
    }

    strcpy(entry_path, host_dir_path);
    strcat(entry_path, "/");
    strcat(entry_path, entry->d_name);

    hostfs_read_object_info(entry_path, ro_leaf, object_info);

    /* Ignore entries we can not read information about,
       or which are neither regular files or directories */
    if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
      continue;
    }

    /* Compare leaf and object names in case-insensitive manner */
    if (strcasecmp(*ro_leaf, object) != 0) {
      /* Names do not match */
      free(*ro_leaf);
      continue;
    }

    /* A match has been found - exit the function early */
    *host_name = strdup(entry->d_name);
    if (!(*host_name)) {
      fprintf(stderr, "Out of memory in hostfs_path_scan()\n");
      exit(EXIT_FAILURE);
    }
    closedir(d);
    return;
  }

  closedir(d);

  object_info->type = OBJECT_TYPE_NOT_FOUND;
}

/**
 * @param ro_path       Full RISC OS path to object
 * @param host_pathname Return full Host path to object (allocated)
 * @param ro_leaf       Return RISC OS leaf (allocated)
 * @param object_info   Return object info
 */
static void
hostfs_path_process(const char *ro_path,
                    char **host_pathname,
                    char **ro_leaf,
                    risc_os_object_info *object_info)
{
  char pathname[PATH_MAX];       /* working Host pathname */
  char component_name[PATH_MAX]; /* working Host component */
  char *component;
  const char *ro_path_orig = ro_path;

  assert(ro_path);
  assert(host_pathname && ro_leaf);
  assert(object_info);

  assert(ro_path[0] == '$');

  /* Initialise working Host pathname */
  pathname[0] = '\0';

  /* Initialise working Host component */
  component = &component_name[0];
  *component = '\0';

  while (*ro_path) {
    switch (*ro_path) {
    case '$':
      strcat(pathname, HOSTFS_ROOT);

      hostfs_read_object_info(pathname, ro_leaf, object_info);
      if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
        return;
      }

      break;

    case '.':
      if (component_name[0] != '\0') {
        /* only if not first dot, i.e. "$." */

        char *host_name;

        *component = '\0'; /* add terminator */

        free(*ro_leaf); /* Free up previous leaf before new one allocated */

        hostfs_path_scan(pathname, component_name,
                         &host_name, ro_leaf, object_info);
        if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
          /* This component of the path is invalid */
          /* Return what we have of the host_pathname */
          /* FIXME Fix the memory leak that this creates */
          *host_pathname = strdup(pathname);

          /* Return the leaf of the original passed in RISC OS path */
          *ro_leaf = strdup(strrchr(ro_path_orig, '.') + 1);

          if (!(*host_pathname) || !(*ro_leaf)) {
            fprintf(stderr, "Out of memory in hostfs_path_process()\n");
            exit(EXIT_FAILURE);
          }

          return;
        }

        /* Append Host's name for this component to the working Host path */
        strcat(pathname, "/");
        strcat(pathname, host_name);

        free(host_name);
        /* TODO Free up ro_leaf if this is not the last component part */

        /* Reset component name ready for re-use */
        component = &component_name[0];
        *component = '\0';
      }
      break;

    case '/':
      *component++ = '.';
      break;

    default:
      *component++ = *ro_path;
      break;
    }

    ro_path++;
  }

  if (component_name[0] != '\0') {
    /* only if not first dot, i.e. "$." */

    char *host_name;

    *component = '\0'; /* add terminator */

    free(*ro_leaf); /* Free up previous leaf before new one allocated */

    hostfs_path_scan(pathname, component_name,
                     &host_name, ro_leaf, object_info);
    if (object_info->type == OBJECT_TYPE_NOT_FOUND) {
      /* This component of the path is invalid */
      /* Return what we have of the host_pathname */
      /* FIXME Fix the memory leak that this creates */
      *host_pathname = strdup(pathname);

      /* Return the leaf of the original passed in RISC OS path.
         If there is not one (because original path is "$", then
         return ro_leaf as "" */
      {
        const char *last_dot = strrchr(ro_path_orig, '.');

        if (last_dot) {
          *ro_leaf = strdup(last_dot + 1);
        } else {
          *ro_leaf = strdup("");
        }
      }

      if (!(*host_pathname) || !(*ro_leaf)) {
        fprintf(stderr, "Out of memory in hostfs_path_process()\n");
        exit(EXIT_FAILURE);
      }

      return;
    }

    /* Append Host's name for this component to the working Host path */
    strcat(pathname, "/");
    strcat(pathname, host_name);

    free(host_name);
    /* TODO Free up ro_leaf if this is not the last component part */
  }

  *host_pathname = strdup(pathname);
  if (!(*host_pathname)) {
    fprintf(stderr, "Out of memory in hostfs_path_process()\n");
    exit(EXIT_FAILURE);
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
  char *ro_path, *host_pathname, *ro_leaf;
  risc_os_object_info object_info;
  unsigned idx;

  assert(state);

  dbug_hostfs("Open\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to pathname)\n", state->Reg[1]);
  dbug_hostfs("\tr3 = %u (FileSwitch handle)\n", state->Reg[3]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
          state->Reg[6]);

  ro_path = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, &host_pathname, &ro_leaf, &object_info);

  if (object_info.type == OBJECT_TYPE_NOT_FOUND) {
    /* FIXME RISC OS uses this to create files - not therefore an error if not found */
    state->Reg[1] = 0; /* Signal to RISC OS file not found */
    goto err_exit;
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
    open_file[idx] = fopen(host_pathname, "rb");
    state->Reg[0] = FILE_INFO_WORD_READ_OK;
    break;

  case OPEN_MODE_CREATE_OPEN_UPDATE:
    dbug_hostfs("\tCreate and open for update (only RISC OS 2)\n");
    goto err_exit;
    break;

  case OPEN_MODE_UPDATE:
    dbug_hostfs("\tOpen for update\n");
    open_file[idx] = fopen(host_pathname, "rb+");
    state->Reg[0] = FILE_INFO_WORD_READ_OK | FILE_INFO_WORD_WRITE_OK;
    break;
  }

  /* Check for errors from opening the file */
  if (open_file[idx] == NULL) {
    switch (errno) {
    case ENOMEM: /* Out of memory */
      fprintf(stderr, "HostFS out of memory in hostfs_open(): \'%s\'\n",
              strerror(errno));
      exit(EXIT_FAILURE);
      break;

    case ENOENT: /* File not found */
      state->Reg[1] = 0; /* Signal to RISC OS file not found */
      goto err_exit;

    default:
      dbug_hostfs("HostFS could not open file \'%s\': %s %d\n",
                  host_pathname, strerror(errno), errno);
      state->Reg[1] = 0; /* Signal to RISC OS file not found */
      goto err_exit;
    }
  }

  /* Find the extent of the file */
  fseek(open_file[idx], 0L, SEEK_END);
  state->Reg[3] = ftell(open_file[idx]);
  rewind(open_file[idx]); /* Return to start */

  state->Reg[1] = idx; /* Our filing system's handle */
  state->Reg[2] = 1024; /* Buffer size to use in range 64-1024.
                           Must be power of 2 */
  state->Reg[4] = 0; /* Space allocated to file */

err_exit:
  free(ro_path);
}

static void
hostfs_getbytes(ARMul_State *state)
{
  FILE *f = open_file[state->Reg[1]];
  ARMword ptr = state->Reg[2];
  unsigned i;

  assert(state);

  dbug_hostfs("GetBytes\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (ptr to buffer)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = %u (number of bytes to read)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (file offset from which to get data)\n",
              state->Reg[4]);

  hostfs_ensure_buffer_size(state->Reg[3]);

  fseek(f, (long) state->Reg[4], SEEK_SET);

  fread(buffer, 1, state->Reg[3], f);

  for (i = 0; i < state->Reg[3]; i++) {
    ARMul_StoreByte(state, ptr++, buffer[i]);
  }
}

static void
hostfs_putbytes(ARMul_State *state)
{
  FILE *f = open_file[state->Reg[1]];
  ARMword ptr = state->Reg[2];
  unsigned i;

  assert(state);

  dbug_hostfs("PutBytes\n");
  dbug_hostfs("\tr1 = %u (our file handle)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (ptr to buffer)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = %u (number of bytes to write)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (file offset at which to put data)\n",
              state->Reg[4]);

  hostfs_ensure_buffer_size(state->Reg[3]);

  fseek(f, (long) state->Reg[4], SEEK_SET);

  for (i = 0; i < state->Reg[3]; i++) {
    buffer[i] = ARMul_LoadByte(state, ptr++);
  }

  fwrite(buffer, 1, state->Reg[3], f);
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

  /* Set file to required extent */
  /* FIXME Not defined if file is increased in size */
  if (ftruncate(fd, (off_t) state->Reg[2])) {
    fprintf(stderr, "hostfs_args_3_write_file_extent() bad ftruncate(): %s %d\n",
            strerror(errno), errno);
    return;
  }
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

  fseek(f, 0L, SEEK_END);

  state->Reg[2] = (ARMword) ftell(f);
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
  case 9:
    hostfs_args_9_read_file_datestamp(state);
    break;
  }
}

static void
hostfs_close(ARMul_State *state)
{
  FILE *f;
  unsigned load, exec;

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

  /* TODO Apply the load and exec addresses */
}

static void
hostfs_file_0_save_file(ARMul_State *state)
{
  const unsigned BUFSIZE = MINIMUM_BUFFER_SIZE;
  char *ro_path, *host_pathname, *ro_leaf;
  risc_os_object_info object_info;
  char *host_path;
  FILE *f;
  ARMword ptr, length;
  unsigned i;

  assert(state);

  ptr = state->Reg[4];
  length = state->Reg[5] - state->Reg[4];

  dbug_hostfs("\tSave file\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to filename)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (load address)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = 0x%08x (exec address)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = 0x%08x (ptr to buffer start)\n", state->Reg[4]);
  dbug_hostfs("\tr5 = 0x%08x (ptr to buffer end)\n", state->Reg[5]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  ro_path = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH = %s\n", ro_path);

#if 1
  host_path = riscos_path_to_host(ro_path);
  dbug_hostfs("\tPATH2 = %s\n", host_path);
#endif

  hostfs_path_process(ro_path, &host_pathname, &ro_leaf, &object_info);
  dbug_hostfs("\tro_path = \"%s\"\n", ro_path);
  if (object_info.type != OBJECT_TYPE_NOT_FOUND) {
    dbug_hostfs("\thost_pathname = \"%s\"\n\tro_leaf = \"%s\"\n",
                host_pathname, ro_leaf);
  }

  if (object_info.type == OBJECT_TYPE_DIRECTORY) {
    /* This should never occur as RISC OS checks whether a directory exists
       before saving a file of the same name */
    abort();
  }

  /* TODO Handle case where file already exists, and needs renaming */

  {
    char new_pathname[PATH_MAX];

    path_construct(host_path, new_pathname, sizeof(new_pathname),
                   state->Reg[2], state->Reg[3]);

    dbug_hostfs("\tnew_pathname = \"%s\"\n", new_pathname);

    hostfs_ensure_buffer_size(BUFSIZE);

    f = fopen(new_pathname, "wb");
    if (!f) {
      /* TODO handle errors */
      fprintf(stderr, "HostFS could not create file \'%s\': %s %d\n",
              new_pathname, strerror(errno), errno);
      goto err_exit;
    }
  }

  while (length >= BUFSIZE) {
    for (i = 0; i < BUFSIZE; i++) {
      buffer[i] = ARMul_LoadByte(state, ptr++);
    }
    /* TODO check for errors */
    fwrite(buffer, 1, BUFSIZE, f);
    length -= BUFSIZE;
  }

  for (i = 0; i < length; i++) {
    buffer[i] = ARMul_LoadByte(state, ptr++);
  }
  fwrite(buffer, 1, length, f);

  fclose(f); /* TODO check for errors */

  state->Reg[6] = 0; /* TODO */

  /* TODO set load and exec address */

err_exit:
  free(host_path);
  free(ro_path);
}

static void
hostfs_file_1_write_cat_info(ARMul_State *state)
{
  char *ro_path, *host_pathname, *ro_leaf;
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

  ro_path = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH = %s\n", ro_path);

  /* TODO Ensure we do not try to modify the root object: i.e. $ */

  hostfs_path_process(ro_path, &host_pathname, &ro_leaf, &object_info);

  free(ro_path);

  switch (object_info.type) {
  case OBJECT_TYPE_NOT_FOUND:
    /* We must not return an error if the object does not exist */
    return;

  case OBJECT_TYPE_FILE:
    dbug_hostfs("\thost_pathname = \"%s\"\n\tro_leaf = \"%s\"\n",
                host_pathname, ro_leaf);
    {
      char new_pathname[PATH_MAX];

      path_construct(host_pathname, new_pathname, sizeof(new_pathname),
                     state->Reg[2], state->Reg[3]);

      /* TODO - update timestamp if necessary */

      if (rename(host_pathname, new_pathname)) {
        /* TODO handle error in renaming */
      }
    }

    /* TODO handle new attribs */
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
  char *ro_path, *host_pathname, *ro_leaf;
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tRead catalogue information\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to pathname)\n", state->Reg[1]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  ro_path = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, &host_pathname, &ro_leaf, &object_info);

  state->Reg[0] = object_info.type;

  if (object_info.type != OBJECT_TYPE_NOT_FOUND) {
    state->Reg[2] = object_info.load;
    state->Reg[3] = object_info.exec;
    state->Reg[4] = object_info.length;
    state->Reg[5] = object_info.attribs;
    free(host_pathname);
    free(ro_leaf);
  }

  free(ro_path);
}

static void
hostfs_file_6_delete(ARMul_State *state)
{
  char *ro_path, *host_pathname, *ro_leaf;
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tDelete object\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to pathname)\n", state->Reg[1]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  ro_path = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH = %s\n", ro_path);

  /* TODO Ensure we do not try to delete the root object: i.e. $ */

  hostfs_path_process(ro_path, &host_pathname, &ro_leaf, &object_info);

  state->Reg[0] = object_info.type;

  if (object_info.type == OBJECT_TYPE_NOT_FOUND) {
    goto err_exit;
  }

  state->Reg[2] = object_info.load;
  state->Reg[3] = object_info.exec;
  state->Reg[4] = object_info.length;
  state->Reg[5] = object_info.attribs;

  switch (object_info.type) {
  case OBJECT_TYPE_FILE:
    if (unlink(host_pathname)) {
      /* Error while deleting the file */
      fprintf(stderr, "HostFS: Error deleting file \'%s\': %s %d\n",
              host_pathname, strerror(errno), errno);
    }
    break;

  case OBJECT_TYPE_DIRECTORY:
    if (rmdir(host_pathname)) {
      /* Error while deleting the directory */
      fprintf(stderr, "HostFS: Error deleting directory \'%s\': %s %d\n",
              host_pathname, strerror(errno), errno);
    }
    break;

  default:
    abort();
  }

  free(host_pathname);
  free(ro_leaf);

err_exit:
  free(ro_path);
}

static void
hostfs_file_7_create_file(ARMul_State *state)
{
  const unsigned BUFSIZE = MINIMUM_BUFFER_SIZE;
  char *path, *host_path;
  FILE *f;
  ARMword length;

  assert(state);

  length = state->Reg[5] - state->Reg[4];

  dbug_hostfs("\tCreate file\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to filename)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (load address)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = 0x%08x (exec address)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = 0x%08x (ptr to buffer start)\n", state->Reg[4]);
  dbug_hostfs("\tr5 = 0x%08x (ptr to buffer end)\n", state->Reg[5]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  path = get_string(state, state->Reg[1]);
  host_path = riscos_path_to_host(path);
  dbug_hostfs("\tPATH = %s\n", path);
  dbug_hostfs("\tPATH2 = %s\n", host_path);

  hostfs_ensure_buffer_size(BUFSIZE);

  f = fopen(host_path, "wb");
  if (!f) {
    /* TODO handle errors */
    fprintf(stderr, "HostFS could not create file \'%s\': %s %d\n",
            host_path, strerror(errno), errno);
    goto err_exit;
  }

  while (length >= BUFSIZE) {
    /* TODO check for errors */
    fwrite(buffer, 1, BUFSIZE, f);
    length -= BUFSIZE;
  }

  fwrite(buffer, 1, length, f);

  fclose(f); /* TODO check for errors */

  state->Reg[6] = 0; /* TODO */

  /* TODO set load and exec address */

err_exit:
  free(host_path);
  free(path);
}

static void
hostfs_file_8_create_dir(ARMul_State *state)
{
  char *path, *host_path;

  assert(state);

  dbug_hostfs("\tCreate directory\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to dirname)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (load address)\n", state->Reg[2]);
  dbug_hostfs("\tr3 = 0x%08x (exec address)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (number of entries)\n", state->Reg[4]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  path = get_string(state, state->Reg[1]);
  host_path = riscos_path_to_host(path);
  dbug_hostfs("\tPATH = %s\n", path);
  dbug_hostfs("\tPATH2 = %s\n", host_path);

  if (mkdir(host_path, 0777)) {
    /* An error occurred whilst creating the directory */

    switch (errno) {
    case EEXIST:
      /* The object exists (not necessarily as a directory) - does it matter that it could be a file? TODO */
      goto err_exit; /* Return with no error */

    case ENOSPC: /* No space for the directory (either physical or quota) */
      state->Reg[9] = FILECORE_ERROR_DISCFULL;
      goto err_exit;

    default:
      fprintf(stderr, "HostFS could not create directory \'%s\': %s\n",
              host_path, strerror(errno));
      goto err_exit;
    }
  }

err_exit:
  free(host_path);
  free(path);
}

static void
hostfs_file_255_load_file(ARMul_State *state)
{
  const unsigned BUFSIZE = MINIMUM_BUFFER_SIZE;
  char *ro_path, *host_pathname, *ro_leaf;
  risc_os_object_info object_info;
  FILE *f;
  size_t bytes_read;
  ARMword ptr;

  assert(state);

  ptr = state->Reg[2];

  dbug_hostfs("\tLoad file\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to wildcarded filename)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (address to load at)\n", state->Reg[2]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  ro_path = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, &host_pathname, &ro_leaf, &object_info);

  state->Reg[2] = object_info.load;
  state->Reg[3] = object_info.exec;
  state->Reg[4] = object_info.length;
  state->Reg[5] = object_info.attribs;
  state->Reg[6] = 0; /* TODO */

  f = fopen(host_pathname, "rb");
  if (!f) {
    fprintf(stderr, "HostFS could not open file (File_255) \'%s\': %s %d\n",
            host_pathname, strerror(errno), errno);
    goto err_exit;
  }

  hostfs_ensure_buffer_size(BUFSIZE);

  do {
    unsigned i;

    bytes_read = fread(buffer, 1, BUFSIZE, f);

    for (i = 0; i < bytes_read; i++) {
      ARMul_StoreByte(state, ptr++, buffer[i]);
    }
  } while (bytes_read == BUFSIZE);

  fclose(f);

err_exit:
  free(ro_path);
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
  char *path, *host_path;

  assert(state);

  dbug_hostfs("\tSet current directory\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to wildcarded dir. name)\n", state->Reg[1]);

  path = get_string(state, state->Reg[1]);
  host_path = riscos_path_to_host(path);
  dbug_hostfs("\tPATH = %s\n", path);
  dbug_hostfs("\tPATH2 = %s\n", host_path);

  free(host_path);
  free(path);
}

static void
hostfs_func_8_rename(ARMul_State *state)
{
  char *ro_path1, *host_pathname1, *ro_leaf1;
  char *ro_path2, *host_pathname2, *ro_leaf2;
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
  ro_path1 = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH_OLD = %s\n", ro_path1);

  hostfs_path_process(ro_path1, &host_pathname1, &ro_leaf1, &object_info1);

  /* Process new path */
  ro_path2 = get_string(state, state->Reg[2]);
  dbug_hostfs("\tPATH_NEW = %s\n", ro_path2);

  hostfs_path_process(ro_path2, &host_pathname2, &ro_leaf2, &object_info2);


  if (object_info1.type == OBJECT_TYPE_NOT_FOUND) {
    /* TODO Check if we need to handle this better */
    state->Reg[1] = 1; /* non-zero indicates could not rename */
    goto err_exit;
  }

  if (object_info2.type != OBJECT_TYPE_NOT_FOUND) {
    /* The new named object does exist - check it is similar to the old
       name */
    if (strcasecmp(ro_path1, ro_path2) != 0) {
      state->Reg[1] = 1; /* non-zero indicates could not rename */
      goto err_exit;
    }
  }

  strcat(host_pathname2, "/");
  strcat(host_pathname2, ro_leaf2);

  path_construct(host_pathname2, new_pathname, sizeof(new_pathname),
                 object_info1.load, object_info1.exec);

  if (rename(host_pathname1, new_pathname)) {
    /* An error occurred */

    fprintf(stderr, "HostFS could not rename \'%s\' to \'%s\': %s %d\n",
            host_pathname1, new_pathname, strerror(errno), errno);
    state->Reg[1] = 1; /* non-zero indicates could not rename */
    goto err_exit;
  }


  state->Reg[1] = 0; /* zero indicates successful rename */

err_exit:
  free(ro_path1);
  free(ro_path2);
}

static void
hostfs_func_14_read_dir(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tRead directory entries\n");
  state->Reg[3] = 0;
  state->Reg[4] = -1;
}

static void
hostfs_func_15_read_dir_info(ARMul_State *state)
{
  char *ro_path, *host_pathname, *ro_leaf;
  risc_os_object_info object_info;

  assert(state);

  dbug_hostfs("\tRead directory entries and information\n");
  dbug_hostfs("\tr1 = 0x%08x (ptr to wildcarded dir. name)\n", state->Reg[1]);
  dbug_hostfs("\tr2 = 0x%08x (ptr to buffer for returned data)\n",
              state->Reg[2]);
  dbug_hostfs("\tr3 = %u (number of object names to read)\n", state->Reg[3]);
  dbug_hostfs("\tr4 = %u (offset of first item to read in dir)\n",
              state->Reg[4]);
  dbug_hostfs("\tr5 = %u (length of buffer)\n", state->Reg[5]);
  dbug_hostfs("\tr6 = 0x%08x (pointer to special field if present)\n",
              state->Reg[6]);

  ro_path = get_string(state, state->Reg[1]);
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, &host_pathname, &ro_leaf, &object_info);

  if (object_info.type != OBJECT_TYPE_DIRECTORY) {
    /* TODO Improve error return */
    state->Reg[3] = 0;
    state->Reg[4] = -1;
    goto err_exit;
  }

  free(ro_leaf);

  {
    const ARMword num_objects_to_read = state->Reg[3];
    ARMword buffer_remaining = state->Reg[5]; /* buffer size given */
    ARMword count = 0; /* Number of objects returned from this call */
    ARMword offset = state->Reg[4]; /* Offset of item to read */
    ARMword ptr = state->Reg[2]; /* Pointer to return buffer */
    DIR *d;
    struct dirent *entry = NULL;

    d = opendir(host_pathname);
    if (!d) {
      fprintf(stderr, "HostFS could not open directory \'%s\': %s\n",
              host_pathname, strerror(errno));
      state->Reg[3] = 0;
      state->Reg[4] = -1;
      goto err_exit;
    }

    /* Skip a number of directory entries according to the offset */
    while ((count < offset) && ((entry = readdir(d)) != NULL)) {
      char entry_path[PATH_MAX];

      /* Hidden files are completely ignored */
      if (entry->d_name[0] == '.') {
        continue;
      }

      strcpy(entry_path, host_pathname);
      strcat(entry_path, "/");
      strcat(entry_path, entry->d_name);

      hostfs_read_object_info(entry_path, &ro_leaf, &object_info);

      /* Ignore entries we can not read information about,
         or which are neither regular files or directories */
      if (object_info.type == OBJECT_TYPE_NOT_FOUND) {
        continue;
      }

      free(ro_leaf);

      /* Count this entry */
      count ++;
    }

    if (count < offset) {
      /* There were not enough entries to skip on the host OS.
         Return no further items */
      dbug_hostfs("HostFS not enough entries to skip - returning no more\n");
      state->Reg[3] = 0;
      state->Reg[4] = -1;
      goto err_exit;
    }

    /* So far we have skipped a number of directory entries according to the
       variable 'offset' */

    /* Reset the counter, so we can keep track of number of objects returned */
    count = 0;

    while ((count < num_objects_to_read) && ((entry = readdir(d)) != NULL)) {
      char entry_path[PATH_MAX];
      unsigned string_space;

      /* Hidden files are completely ignored */
      if (entry->d_name[0] == '.') {
        continue;
      }

      strcpy(entry_path, host_pathname);
      strcat(entry_path, "/");
      strcat(entry_path, entry->d_name);

      hostfs_read_object_info(entry_path, &ro_leaf, &object_info);

      /* Ignore entries we can not read information about,
         or which are neither regular files or directories */
      if (object_info.type == OBJECT_TYPE_NOT_FOUND) {
        continue;
      }

      /* See whether there is space left in the buffer to return this entry */
      string_space = ROUND_UP_TO_4(strlen(ro_leaf) + 1);
      if (((5 * sizeof(ARMword)) + string_space) > buffer_remaining) {
        break;
      }

      /* Fill in this entry */
      ARMul_StoreWordS(state, ptr + 0,  object_info.load);
      ARMul_StoreWordS(state, ptr + 4,  object_info.exec);
      ARMul_StoreWordS(state, ptr + 8,  object_info.length);
      ARMul_StoreWordS(state, ptr + 12, object_info.attribs);
      ARMul_StoreWordS(state, ptr + 16, object_info.type);

      string_space = put_string(state, ptr + 20, ro_leaf);
      ptr += ((5 * sizeof(ARMword)) + string_space);
      buffer_remaining -= ((5 * sizeof(ARMword)) + string_space);
      count ++;

      free(ro_leaf);
    }

    /* Increase offset by the number of items read this time */
    offset += count;

    /* Find out whether we have now completed the directory */
    if (!entry) {
      /* We have completed the directory - return this fact */
      dbug_hostfs("HostFS completed directory\n");
      state->Reg[4] = -1;
    } else {
      /* We have not yet finished - return the offset for next time */
      state->Reg[4] = offset;
    }

    closedir(d);

    state->Reg[3] = count;	/* Number of objects returned at this point */
  }

err_exit:
  free(ro_path);
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
    state->Reg[9] = NOT_IMPLEMENTED;
    break;

  case 14:
    hostfs_func_14_read_dir(state);
    break;

  case 15:
    hostfs_func_15_read_dir_info(state);
    break;
  }
}

static void
hostfs_gbpb(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("GBPB\n");
}

void
hostfs(ARMul_State *state)
{
  assert(state);
  assert(state->Reg[9] <= 7);

  dbug_hostfs("*** HostFS Call ***\n");

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
    fprintf(stderr, "!!! ERROR !!! - unknown op in R9\n");
    break;
  }
}
