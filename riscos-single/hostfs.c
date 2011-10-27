/*
 * $Id$
 *
 * HostFS implementation for RISC OS hosts
 * Basically the same as the standard one, but without all the filename/type
 * fiddling
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
#include <utime.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdbool.h>

#include "kernel.h"
#include "swis.h"

#include "arch/armarc.h"
#include "hostfs.h"
#include "arch/ArcemConfig.h"

#ifdef __TARGET_UNIXLIB__ /* Set by GCC */
#include <unixlib/local.h>
int __riscosify_control = 0;
#endif

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
  FILECORE_ERROR_TOOMANYOPEN = 0xc0, /* Too many open files */
  FILECORE_ERROR_OPEN        = 0xc2, /* File open */
  FILECORE_ERROR_LOCKED      = 0xc3,
  FILECORE_ERROR_EXISTS      = 0xc4, /* Already exists */
  FILECORE_ERROR_DISCFULL    = 0xc6,
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
#define STRCASEEQ(x,y) (strcasecmp(x,y) == 0)

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

#define MAX_OPEN_FILES 255

#define NOT_IMPLEMENTED 255

#define DEFAULT_ATTRIBUTES  0x03
#define MINIMUM_BUFFER_SIZE 32768

static FILE *open_file[MAX_OPEN_FILES + 1]; /* array subscript 0 is never used */

static unsigned char *buffer = NULL;
static size_t buffer_size = 0;

static cache_directory_entry *cache_entries = NULL;
static unsigned cache_entries_count = 0; /**< Number of valid entries in \a cache_entries */
static char *cache_names = NULL;

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
  while ((*buf++ = ARMul_LoadByte(state, address++)) != '\0') {
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

/**
 * Apply load/exec attributes to host file
 *
 * @param host_path Full path to object (file or dir) in host format
 * @param load      RISC OS load address (may contain time-stamp)
 * @param exec      RISC OS exec address (may contain time-stamp)
 */
static void
hostfs_object_set_loadexec(const char *host_path, ARMword load, ARMword exec)
{
  _swix(OS_File,_INR(0,2),2,host_path,load);
  _swix(OS_File,_INR(0,1)|_IN(3),3,host_path,exec);
}

/**
 * Apply attributes to host file
 *
 * @param host_path Full path to object (file or dir) in host format
 * @param load      RISC OS load address (may contain time-stamp)
 * @param exec      RISC OS exec address (may contain time-stamp)
 * @param attribs   RISC OS file attributes
 */
static void
hostfs_object_set_attribs(const char *host_path, ARMword load, ARMword exec,ARMword attribs)
{
  _swix(OS_File,_INR(0,3)|_IN(5),1,host_path,load,exec,attribs);
}

static void
riscos_path_to_host(const char *path, char *host_path)
{
  assert(path);
  assert(host_path);

  while (*path) {
    switch (*path) {
    case '$':
      strcpy(host_path, hArcemConfig.sHostFSDirectory);
      host_path += strlen(host_path);
      break;
    default:
      *host_path++ = *path;
      break;
    }
    path++;
  }

  *host_path = '\0';
}

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
    *riscos_name++ = *object_name++;
  }

  *riscos_name = '\0';
}

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

    slash = strrchr(new_path, '.');
    if (slash) {
      /* New leaf immediately follows final dot */
      new_leaf = slash + 1;
    } else {
      /* No dot currently in Host path, but we need one */
      /* New leaf is then appended */
      strcat(new_path, ".");
      new_leaf = new_path + strlen(new_path);
    }

    /* Place new leaf */
    riscos_path_to_host(ro_leaf, new_leaf);
  }
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
  char path[PATH_MAX];

  assert(host_dir_path);
  assert(object);
  assert(host_name);

  /* This is nice and easy */

  sprintf(path,"%s.%s",host_dir_path,object);

  hostfs_read_object_info(path,NULL,object_info);

  if(object_info->type != OBJECT_TYPE_NOT_FOUND)
    strcpy(host_name,object);
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
      strcat(host_pathname, hArcemConfig.sHostFSDirectory);

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
        strcat(host_pathname, ".");
        strcat(host_pathname, host_name);

        /* Reset component name ready for re-use */
        component = &component_name[0];
        *component = '\0';
      }
      break;

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
    strcat(host_pathname, ".");
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
    return;

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
      return;

    default:
      dbug_hostfs("HostFS could not open file \'%s\': %s %d\n",
                  host_pathname, strerror(errno), errno);
      state->Reg[1] = 0; /* Signal to RISC OS file not found */
      return;
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
}

static void
hostfs_getbytes(ARMul_State *state)
{
  FILE *f = open_file[state->Reg[1]];
  ARMword ptr = state->Reg[2];
  ARMword i;

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
  ARMword i;

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

  /* TODO Apply the load and exec addresses */
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
    strcat(host_pathname, ".");
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
      fprintf(stderr, "hostfs_file_7_create_file(): could not rename \'%s\'"
              " to \'%s\': %s %d\n", host_pathname, new_pathname,
              strerror(errno), errno);
      return;
    }
    break;

  case OBJECT_TYPE_DIRECTORY:
    /* TODO Find a suitable error */
    return;
  }

  hostfs_ensure_buffer_size(BUFSIZE);

  f = fopen(new_pathname, "wb");
  if (!f) {
    /* TODO handle errors */
    fprintf(stderr, "HostFS could not create file \'%s\': %s %d\n",
            new_pathname, strerror(errno), errno);
    return;
  }

  /* Fill the data buffer with 0's if we are not saving supplied data */
  if (!with_data) {
    memset(buffer, 0, BUFSIZE);
  }

  /* Save file in blocks of up to BUFSIZE */
  while (length > 0) {
    size_t buffer_amount = MIN(length, BUFSIZE);
    size_t bytes_written;

    if (with_data) {
      unsigned i;

      /* Copy the correct amount of data into the buffer */
      for (i = 0; i < buffer_amount; i++) {
        buffer[i] = ARMul_LoadByte(state, ptr++);
      }
    }

    /* TODO check for errors */
    bytes_written = fwrite(buffer, 1, buffer_amount, f);
    length -= bytes_written;
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

    hostfs_object_set_attribs(host_pathname, state->Reg[2], state->Reg[3], state->Reg[5]);
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

  /* TODO Ensure we do not try to delete the root object: i.e. $ */

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

    strcat(host_pathname, ".");
    new_leaf = host_pathname + strlen(host_pathname);

    /* Place new leaf */
    riscos_path_to_host(ro_leaf, new_leaf);
  }

  dbug_hostfs("\tHOST_PATHNAME = %s\n", host_pathname);

  /* Create directory */
  if (mkdir(host_pathname, 0777)) {
    /* An error occurred whilst creating the directory */

    switch (errno) {
    case EEXIST:
      /* The object exists (not necessarily as a directory) - does it matter that it could be a file? TODO */
      return; /* Return with no error */

    case ENOSPC: /* No space for the directory (either physical or quota) */
      state->Reg[9] = FILECORE_ERROR_DISCFULL;
      return;

    default:
      fprintf(stderr, "HostFS could not create directory \'%s\': %s\n",
              host_pathname, strerror(errno));
      return;
    }
  }
}

static void
hostfs_file_255_load_file(ARMul_State *state)
{
  const unsigned BUFSIZE = MINIMUM_BUFFER_SIZE;
  char ro_path[PATH_MAX], host_pathname[PATH_MAX];
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

  get_string(state, state->Reg[1], ro_path, sizeof(ro_path));
  dbug_hostfs("\tPATH = %s\n", ro_path);

  hostfs_path_process(ro_path, host_pathname, &object_info);

  state->Reg[2] = object_info.load;
  state->Reg[3] = object_info.exec;
  state->Reg[4] = object_info.length;
  state->Reg[5] = object_info.attribs;
  state->Reg[6] = 0; /* TODO */

  f = fopen(host_pathname, "rb");
  if (!f) {
    fprintf(stderr, "HostFS could not open file (File_255) \'%s\': %s %d\n",
            host_pathname, strerror(errno), errno);
    return;
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
    strcat(host_pathname2, ".");
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

  return strcasecmp(name1, name2);
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
  const struct dirent *entry;

  assert(directory_name);

  /* Allocate memory initially */
  if (!cache_entries) {
    cache_entries = malloc(cache_entries_capacity * sizeof(cache_directory_entry));
  }
  if (!cache_names) {
    cache_names = malloc(cache_names_capacity);
  }
  if ((!cache_entries) || (!cache_names)) {
    fprintf(stderr, "hostfs_cache_dir(): Out of memory\n");
    exit(1);
  }

  /* Read each of the directory entries one at a time.
   * Fill in the cache_entries[] and cache_names[] arrays,
   *    resizing these dynamically if required.
   */
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
          fprintf(stderr, "hostfs_cache_dir(): Out of memory\n");
          exit(1);
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
          fprintf(stderr, "hostfs_cache_dir(): Out of memory\n");
          exit(1);
        }
      }
    }
  } while(gbpb_entry != -1);

  /* Sort the directory entries, case-insensitive */
  qsort(cache_entries, entry_ptr, sizeof(cache_directory_entry),
        hostfs_directory_entry_compare);

  /* Store the number of directory entries found */
  cache_entries_count = entry_ptr;
}

/**
 * Return directory information for FSEntry_Func 14 and 15.
 * Uses and updates the cached directory information.
 *
 * @param state Emulator state
 * @param with_info Whether the returned data should include information with
 *                  each entry, or just names.
 */

static void
hostfs_read_dir(ARMul_State *state, bool with_info)
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
    state->Reg[4] = -1;
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
      string_space = strlen(cache_names + cache_entries[offset].name_offset) + 1;
      if (with_info) {
        /* Space required for info, everything word-aligned */
        string_space = ROUND_UP_TO_4(string_space);
        entry_space = (5 * sizeof(ARMword)) + string_space;
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
        ptr += 20;
      }
      put_string(state, ptr, cache_names + cache_entries[offset].name_offset);

      ptr += string_space;
      buffer_remaining -= entry_space;
      count ++;
      offset ++;
    }

    /* Find out whether we have now completed the directory */
    if (offset >= cache_entries_count) {
      /* We have completed the directory - return this fact */
      dbug_hostfs("HostFS completed directory\n");
      state->Reg[4] = -1;
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

  hostfs_read_dir(state, false);
}

static void
hostfs_func_15_read_dir_info(ARMul_State *state)
{
  assert(state);

  dbug_hostfs("\tRead directory entries and information\n");

  hostfs_read_dir(state, true);
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
hostfs(ARMul_State *state, ARMword fs_op)
{
  assert(state);
  assert(fs_op <= 7);

  dbug_hostfs("*** HostFS Call ***\n");

#ifdef __TARGET_UNIXLIB__
  /* Temporarily disable filename translation so we can use ANSI file functions
     (avoids me having to convert the code to use OS calls directly, and means
     we don't need an atexit() handler to close the files on exit) */
  int old_riscosify = __riscosify_control;
  __riscosify_control = __RISCOSIFY_NO_PROCESS;
#endif

  switch (fs_op) {
  case 0: hostfs_open(state);     break;
  case 1: hostfs_getbytes(state); break;
  case 2: hostfs_putbytes(state); break;
  case 3: hostfs_args(state);     break;
  case 4: hostfs_close(state);    break;
  case 5: hostfs_file(state);     break;
  case 6: hostfs_func(state);     break;
  case 7: hostfs_gbpb(state);     break;
  }

#ifdef __TARGET_UNIXLIB__
  __riscosify_control = old_riscosify;
#endif
}
