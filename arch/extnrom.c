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

#include "../armdefs.h"
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

/* TODO: Make this a no-op on a little-endian host */
static void
extnrom_endian_correct(ARMword *start_addr, unsigned size)
{
  unsigned size_in_words = ROUND_UP_TO_4(size) / 4;
  unsigned word_num;
  ARMword word;

  assert(start_addr != NULL);
  assert(size > 0);

  for (word_num = 0; word_num < size_in_words; word_num++) {
    word = get_32bit_le(start_addr + word_num); /* Read little-endian */
    start_addr[word_num] = word;                /* Write host-endian */
  }
}

static unsigned
extnrom_calculate_checksum(const ARMword *start_addr, unsigned size)
{
  unsigned checksum = 0;
  unsigned size_in_words = ROUND_UP_TO_4(size) / 4;
  unsigned word_num;

  assert(start_addr != NULL);
  assert(size > 0);
  assert((size & 0xffff) == 0); /* size is multiple of 64KB */

  for (word_num = 0; word_num < (size_in_words - 3); word_num++) {
    checksum += start_addr[word_num];
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
  ARMword *start_addr = address;
  ARMword *chunk, *modules;
  DIR *d;
  struct dirent *entry;
  unsigned size_in_words = size / 4;

  assert(((size != 0) && (entry_count != 0)) ||
         ((size == 0) && (entry_count == 0)));

  /* If size == 0 then nothing to do here */
  if (size == 0) {
    return;
  }

  assert((size & 0xffff) == 0); /* size is multiple of 64KB */
  assert(address != NULL);

  /* Fill in Expansion Card identity */
  {
    unsigned simple_ecid = 0x00; /* Acorn conformant, extended Expansion Card
                                    identity present, no Interrupts */
    unsigned flags = 0x03; /* 8-bit wide, Interrupt Status Pointers present,
                              Chunk Directory present */
    unsigned reserved = 0x00; /* All values reserved - must be 0 */

    start_addr[0] = simple_ecid |
                    (flags << 8) |
                    (reserved << 16) |
                    ((PRODUCT_TYPE_EXTENSION_ROM & 0xff) << 24);
    start_addr[1] = (PRODUCT_TYPE_EXTENSION_ROM >> 8) |
                    (MANUFACTURER_CODE << 8) |
                    (COUNTRY_CODE << 24);
  }

  /* Interrupt Status Pointers */
  /* "Extension ROMs must provide Interrupt Status Pointers. However,
      extension ROMs generate neither FIQ nor IRQ: consequently their
      Interrupt Status Pointers always consist of eight zero bytes */
  start_addr[2] = 0;
  start_addr[3] = 0;

  /* Read list of files, create Chunk Directory and load them in */
  d = opendir(EXTNROM_DIRECTORY);
  if (!d) {
    fprintf(stderr, "Could not open Extension Rom directory \'%s\': %s\n",
            EXTNROM_DIRECTORY, strerror(errno));
    return;
  }

  chunk = start_addr + 4; /* points to where the Chunk Directory will be made */

  /* Initialise pointer to where the actual module data will be loaded.
     These reside after the Header (16 bytes, 4 words),
     Chunk Directory (8 bytes (2 words) per entry
                      plus 8 bytes (2 words) for the descriptor entry)
     and Chunk Directory Terminator (4 bytes, 1 word) */
  modules = chunk + (entry_count * 2) + 2 + 1;

  /* The First Chunk: A simple description string */
  chunk[0] = OS_ID_BYTE_DEVICE_DESCR |
             ((strlen(DESCRIPTION_STRING) + 1) << 8);
  chunk[1] = (modules - start_addr) * 4; /* offset in bytes */

  strcpy((char *) modules, DESCRIPTION_STRING);
  extnrom_endian_correct(modules, strlen(DESCRIPTION_STRING) + 1);

  /* Move chunk and module pointers on */
  chunk += 2;
  modules += ROUND_UP_TO_4(strlen(DESCRIPTION_STRING) + 1) / 4;

  /* Process the modules */
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
    offset = ((modules - start_addr) * 4) + 4;

    /* Prepare Chunk Directory information for this entry */
    chunk[0] = OS_ID_BYTE_RISCOS_MODULE |
               (entry_info.st_size << 8);
    chunk[1] = offset;

    /* Prepare undocumented size prefix - size includes the size data */
    modules[0] = entry_info.st_size + 4;

    /* Point to next word within module area - after the size */
    modules++;

    /* Load module */
    f = fopen(path, "rb");
    if (!f) {
      fprintf(stderr, "Could not open file \'%s\': %s\n",
              path, strerror(errno));
      continue;
    }

    if (fread(modules, 1, entry_info.st_size, f) != entry_info.st_size) {
      fprintf(stderr, "Error while loading file \'%s\': %s\n",
              path, strerror(errno));
      fclose(f);
      continue;
    }

    fclose(f);

    /* Byte-swap module from little-endian to host processor */
    extnrom_endian_correct(modules, entry_info.st_size);

    /* Move chunk and module pointers on */
    chunk += 2;
    modules += ROUND_UP_TO_4(entry_info.st_size) / 4;
  }

  closedir(d);

  /* Fill in chunk directory terminator */
  chunk[0] = 0;

  /* Fill in end header */
  start_addr[size_in_words - 4] = size;

  /* Calculate and fill in checksum */
  start_addr[size_in_words - 3] = extnrom_calculate_checksum(start_addr, size);

  /* Fill in Extension ROM id */
  /* memcpy() used to ensure no zero-terminator */
  memcpy(start_addr + size_in_words - 2, "ExtnROM0", 8);
  extnrom_endian_correct(start_addr + size_in_words - 2, 8);

  /*{
    FILE *f = fopen("extnrom_dump", "wb");
    fwrite(start_addr, 1, size, f);
    fclose(f);
  }*/
}

#endif /* defined(SYSTEM_X) || defined(MACOSX) */
