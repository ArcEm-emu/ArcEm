/* (c) Matthew Howkins 2004 - see Readme file for copying info */

/* Support for Extension ROM (aka 5th Column ROM) */

/* References:

   RISC OS 3 PRM's (Chapter 77 in Volume 4)

   "Acorn Enhanced Expansion Card Specification (formerly Acorn expansion
    card specification)", 1994, Acorn Computers Ltd.

   "RISC OS Support for extension ROMs", a text file
*/

#if defined(SYSTEM_X) || defined(MACOSX)  

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "extnrom.h"

#define PRODUCT_TYPE_EXTENSION_ROM 0x0087 /* Allocated type for Extension Roms */
#define MANUFACTURER_CODE 0x0000 /* Any 16-bit value suitable, 0 = Acorn */
#define COUNTRY_CODE 0x00 /* In most recent docs, all countries should be 0 */

#define DESCRIPTION_STRING "ArcEm Support"

#define EXTNROM_DIRECTORY "extnrom"

#define ROUND_UP_TO_4(x) (((x) + 3) & (~3))

enum OS_ID_BYTE {
  OS_ID_BYTE_RISCOS_MODULE = 0x81,
  OS_ID_BYTE_DEVICE_DESCR  = 0xf5,
};

static void
store_16bit_le(void *address, unsigned data)
{
  unsigned char *addr = address;

  assert(address != NULL);

  addr[0] = data;
  addr[1] = data >> 8;
}

static void
store_24bit_le(void *address, unsigned data)
{
  unsigned char *addr = address;

  assert(address != NULL);

  addr[0] = data;
  addr[1] = data >> 8;
  addr[2] = data >> 16;
}

static void
store_32bit_le(void *address, unsigned data)
{
  unsigned char *addr = address;

  assert(address != NULL);

  addr[0] = data;
  addr[1] = data >> 8;
  addr[2] = data >> 16;
  addr[3] = data >> 24;
}

static unsigned
get_32bit_le(const void *address)
{
  const unsigned char *addr = address;
  unsigned result;

  assert(address != NULL);

  result = (unsigned) addr[0];
  result |= ((unsigned) addr[1]) << 8;
  result |= ((unsigned) addr[2]) << 16;
  result |= ((unsigned) addr[3]) << 24;

  return result;
}

static unsigned
extnrom_calculate_checksum(const unsigned char *start_addr,
                           unsigned size)
{
  unsigned checksum = 0;
  unsigned word;

  assert(start_addr != NULL);
  assert(size > 0);
  assert((size & 0xffff) == 0); /* size is multiple of 64KB */

  for (word = 0; word < (size - 12); word += 4) {
    checksum += get_32bit_le(start_addr + word);
  }

  return checksum;
}

unsigned
extnrom_calculate_size(unsigned *entry_count)
{
  DIR *d;
  struct dirent *entry;
  unsigned required_size = 0;

  assert(entry_count != NULL);

  *entry_count = 0;

  /* Read list of files and calculate total size */
  d = opendir(EXTNROM_DIRECTORY);
  if (!d) {
    fprintf(stderr, "Could not open Extension Rom directory \'%s\': %s\n",
            EXTNROM_DIRECTORY, strerror(errno));
    return 0;
  }

  while ((entry = readdir(d)) != NULL) {
    char path[PATH_MAX];
    struct stat entry_info;

    /* Ignore hidden entries - those starting with '.' */
    if (entry->d_name[0] == '.') {
      continue;
    }

    /* Construct relative path to the entry */
    strcpy(path, EXTNROM_DIRECTORY);
    strcat(path, "/");
    strcat(path, entry->d_name);

    /* Read information about the entry */
    if (stat(path, &entry_info) != 0) {
      fprintf(stderr, "Warning: could not stat() entry \'%s\': %s\n",
              path, strerror(errno));
      continue;
    }

    if (!S_ISREG(entry_info.st_mode)) {
      /* Not a regular file - skip it */
      continue;
    }

    /* Add on size of file */
    required_size += ROUND_UP_TO_4(entry_info.st_size);

    /* Add on overhead for each file */
    required_size += 12; /* 8 for Chunk directory info, 4 for size prefix */

    /* Add one to file count */
    (*entry_count) ++;
  }

  closedir(d);

  /* If no files, then no space required */
  if (*entry_count == 0) {
    return 0;
  }

  /* Add fixed overhead:
     8 bytes for extended Expansion Card identity
     8 bytes for Interrupt Status Pointers
     4 bytes for Chunk Directory Terminator
     16 bytes for 'end header' */
  required_size += 8 + 8 + 4 + 16;

  /* Add overhead for description string:
     8 bytes for Chunk Directory item
     + bytes for the string and its terminator */
  required_size += 8 + ROUND_UP_TO_4(strlen(DESCRIPTION_STRING) + 1);

  /* Round size up to a multiple of 64KB */
  return (required_size + 65535) & (~0xffff);
}

void
extnrom_load(unsigned size, unsigned entry_count, void *address)
{
  unsigned char *start_addr = address;
  unsigned char *chunk, *modules;
  DIR *d;
  struct dirent *entry;

  assert(((size != 0) && (entry_count != 0)) ||
         ((size == 0) && (entry_count == 0)));

  /* If size == 0 then nothing to do here */
  if (size == 0) {
    return;
  }

  assert((size & 0xffff) == 0); /* size is multiple of 64KB */
  assert(address != NULL);

  /* Fill in Expansion Card identity */
  start_addr[0] = 0x00; /* Acorn conformant, extended Expansion Card identity
                           present, no Interrupts */
  start_addr[1] = 0x03; /* 8-bit wide, Interrupt Status Pointers prsent,
                           Chunk Directory present */
  start_addr[2] = 0x00; /* All values reserved - must be 0 */
  store_16bit_le(start_addr + 3, PRODUCT_TYPE_EXTENSION_ROM);
  store_16bit_le(start_addr + 5, MANUFACTURER_CODE);
  start_addr[7] = COUNTRY_CODE;

  /* Interrupt Status Pointers */
  /* "Extension ROMs must provide Interrupt Status Pointers. However,
      extension ROMs generate neither FIQ nor IRQ: consequently their
      Interrupt Status Pointers always consist of eight zero bytes */
  memset(start_addr + 8, 0, 8);

  /* Read list of files, create Chunk Directory and load them in */
  d = opendir(EXTNROM_DIRECTORY);
  if (!d) {
    fprintf(stderr, "Could not open Extension Rom directory \'%s\': %s\n",
            EXTNROM_DIRECTORY, strerror(errno));
    return;
  }

  chunk = start_addr + 16; /* points to where the Chunk Directory will be made */

  /* Initialise pointer to where the actual module data will be loaded.
     These reside after the Header (16),
     Chunk Directory (8 per entry plus 8 for the descriptor entry)
     and Chunk Directory Terminator (4) */
  modules = chunk + (entry_count * 8) + 8 + 4;

  /* The First Chunk: A simple description string */
  {
    unsigned offset = modules - start_addr;

    chunk[0] = OS_ID_BYTE_DEVICE_DESCR;
    store_24bit_le(chunk + 1, strlen(DESCRIPTION_STRING) + 1);
    store_32bit_le(chunk + 4, offset);

    strcpy(modules, DESCRIPTION_STRING);

    /* Move chunk and module pointers on */
    chunk += 8;
    modules += ROUND_UP_TO_4(strlen(DESCRIPTION_STRING) + 1);
  }

  while ((entry = readdir(d)) != NULL) {
    char path[PATH_MAX];
    struct stat entry_info;
    unsigned offset;
    FILE *f;

    /* Ignore hidden entries - those starting with '.' */
    if (entry->d_name[0] == '.') {
      continue;
    }

    /* Construct relative path to the entry */
    strcpy(path, EXTNROM_DIRECTORY);
    strcat(path, "/");
    strcat(path, entry->d_name);

    /* Read information about the entry */
    if (stat(path, &entry_info) != 0) {
      continue;
    }

    if (!S_ISREG(entry_info.st_mode)) {
      /* Not a regular file - skip it */
      continue;
    }

    /* Offset of where this module will be placed in the ROM */
    offset = (modules - start_addr) + 4;

    /* Prepare Chunk Directory information for this entry */
    chunk[0] = OS_ID_BYTE_RISCOS_MODULE;
    store_24bit_le(chunk + 1, entry_info.st_size);
    store_32bit_le(chunk + 4, offset);

    /* Prepare undocumented size prefix - size includes the size data */
    store_32bit_le(modules, entry_info.st_size + 4);

    /* Load module */
    f = fopen(path, "rb");
    if (!f) {
      fprintf(stderr, "Could not open file \'%s\': %s\n",
              path, strerror(errno));
      continue;
    }

    if (fread(modules + 4, 1, entry_info.st_size, f) != entry_info.st_size) {
      fprintf(stderr, "Error while loading file \'%s\': %s\n",
              path, strerror(errno));
      fclose(f);
      continue;
    }

    fclose(f);

    /* Move chunk and module pointers on */
    chunk += 8;
    modules += ROUND_UP_TO_4(4 + entry_info.st_size);
  }

  closedir(d);

  /* Fill in chunk directory terminator */
  memset(chunk, 0, 4);

  /* Fill in end header */
  store_32bit_le(start_addr + size - 16, size);

  /* Calculate and fill in checksum */
  {
    unsigned checksum = extnrom_calculate_checksum(start_addr, size);

    store_32bit_le(start_addr + size - 12, checksum);
  }

  memcpy(start_addr + size - 8, "ExtnROM0", 8); /* memcpy used to ensure no zero-terminator */
}

#endif /* SYSTEM_X */
