/* (c) Matthew Howkins 2004 - see Readme file for copying info */

/* Support for Extension ROM (aka 5th Column ROM) */

/* References:

   RISC OS 3 PRM's (Chapter 77 in Volume 4)

   "Acorn Enhanced Expansion Card Specification (formerly Acorn expansion
    card specification)", 1994, Acorn Computers Ltd.

   "RISC OS Support for extension ROMs", a text file
*/

#if defined(EXTNROM_SUPPORT)

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../armdefs.h"
#include "filecalls.h"
#include "extnrom.h"
#include "ArcemConfig.h"

#ifdef AMIGA
#include <sys/syslimits.h>
#endif

#define PRODUCT_TYPE_EXTENSION_ROM 0x0087 /* Allocated type for Extension Roms */
#define MANUFACTURER_CODE 0x0000 /* Any 16-bit value suitable, 0 = Acorn */
#define COUNTRY_CODE 0x00 /* In most recent docs, all countries should be 0 */

#define DESCRIPTION_STRING "ArcEm Support"

#define ROUND_UP_TO_4(x) (((x) + 3) & (~3))

enum OS_ID_BYTE {
  OS_ID_BYTE_RISCOS_MODULE = 0x81,
  OS_ID_BYTE_DEVICE_DESCR  = 0xf5,
};

static void
store_16bit_le(void *address, uint16_t data)
{
  uint8_t *addr = address;

  assert(address != NULL);

  addr[0] = data & 255;
  addr[1] = data >> 8;
}

static void
store_24bit_le(void *address, uint32_t data)
{
  uint8_t *addr = address;

  assert(address != NULL);

  addr[0] = data;
  addr[1] = data >> 8;
  addr[2] = data >> 16;
}

static void
store_32bit_le(void *address, uint32_t data)
{
  uint8_t *addr = address;

  assert(address != NULL);

  addr[0] = data;
  addr[1] = data >> 8;
  addr[2] = data >> 16;
  addr[3] = data >> 24;
}

static uint32_t
get_32bit_le(const void *address)
{
  const uint8_t *addr = address;
  uint32_t result;

  assert(address != NULL);

  result = (uint32_t) addr[0];
  result |= ((uint32_t) addr[1]) << 8;
  result |= ((uint32_t) addr[2]) << 16;
  result |= ((uint32_t) addr[3]) << 24;

  return result;
}

#ifdef HOST_BIGENDIAN
static void
extnrom_endian_correct(ARMword *start_addr, uint32_t size)
{
  uint32_t size_in_words = ROUND_UP_TO_4(size) / 4;
  uint32_t word_num;
  ARMword word;

  assert(start_addr != NULL);
  assert(size > 0);

  for (word_num = 0; word_num < size_in_words; word_num++) {
    word = get_32bit_le(start_addr + word_num); /* Read little-endian */
    start_addr[word_num] = word;                /* Write host-endian */
  }
}
#else
#define extnrom_endian_correct(A,B) ((void)0)
#endif

static uint32_t
extnrom_calculate_checksum(const ARMword *start_addr, uint32_t size)
{
  uint32_t checksum = 0;
  uint32_t size_in_words = ROUND_UP_TO_4(size) / 4;
  uint32_t word_num;

  assert(start_addr != NULL);
  assert(size > 0);
  assert((size & 0xffff) == 0); /* size is multiple of 64KB */

  for (word_num = 0; word_num < (size_in_words - 3); word_num++) {
    checksum += start_addr[word_num];
  }

  return checksum;
}

uint32_t
extnrom_calculate_size(uint32_t *entry_count)
{
  Directory hDir;
  char *sFilename;
  uint32_t required_size = 0;

  assert(entry_count != NULL);

  *entry_count = 0;

  /* Read list of files and calculate total size */
  if(!Directory_Open(hArcemConfig.sEXTNDirectory, &hDir)) {
    fprintf(stderr, "Could not open Extension Rom directory \'%s\': %s\n",
            hArcemConfig.sEXTNDirectory, strerror(errno));
    return 0;
  }

  while ((sFilename = Directory_GetNextEntry(&hDir)) != NULL) {
    char path[ARCEM_PATH_MAX];
    FileInfo hFileInfo;

    /* Ignore hidden entries - those starting with '.' */
    if (sFilename[0] == '.') {
      continue;
    }

    /* Construct relative path to the entry */
    strcpy(path, hArcemConfig.sEXTNDirectory);
    strcat(path, "/");
    strcat(path, sFilename);

    /* Read information about the entry */
    if (!File_GetInfo(path, &hFileInfo)) {
      fprintf(stderr, "Warning: could not get info on file \'%s\'\n",
              path);
      continue;
    }

    if (!hFileInfo.bIsRegularFile) {
      /* Not a regular file - skip it */
      continue;
    }

    /* Add on size of file */
    required_size += ROUND_UP_TO_4(hFileInfo.ulFilesize);

    /* Add on overhead for each file */
    required_size += 12; /* 8 for Chunk directory info, 4 for size prefix */

    /* Add one to file count */
    (*entry_count) ++;
  }

  Directory_Close(hDir);

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
extnrom_load(uint32_t size, uint32_t entry_count, void *address)
{
  ARMword *start_addr = address;
  ARMword *chunk, *modules;
  Directory hDir;
  char *sFilename;
  uint32_t size_in_words = size / 4;

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
    uint32_t simple_ecid = 0x00; /* Acorn conformant, extended Expansion Card
                                    identity present, no Interrupts */
    uint32_t flags = 0x03; /* 8-bit wide, Interrupt Status Pointers present,
                              Chunk Directory present */
    uint32_t reserved = 0x00; /* All values reserved - must be 0 */

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
  if(!Directory_Open(hArcemConfig.sEXTNDirectory, &hDir)) {
    fprintf(stderr, "Could not open Extension Rom directory \'%s\'\n",
            hArcemConfig.sEXTNDirectory);
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
  while ((sFilename = Directory_GetNextEntry(&hDir)) != NULL) {
    char path[ARCEM_PATH_MAX];
    FileInfo hFileInfo;
    uint32_t offset;
    FILE *f;

    /* Ignore hidden entries - those starting with '.' */
    if (sFilename[0] == '.') {
      continue;
    }

    /* Construct relative path to the entry */
    strcpy(path, hArcemConfig.sEXTNDirectory);
    strcat(path, "/");
    strcat(path, sFilename);

    /* Read information about the entry */
    if (!File_GetInfo(path, &hFileInfo)) {
      continue;
    }

    if (!hFileInfo.bIsRegularFile) {
      /* Not a regular file - skip it */
      continue;
    }

    /* Offset of where this module will be placed in the ROM */
    offset = ((modules - start_addr) * 4) + 4;

    /* Prepare Chunk Directory information for this entry */
    chunk[0] = OS_ID_BYTE_RISCOS_MODULE |
               (hFileInfo.ulFilesize << 8);
    chunk[1] = offset;

    /* Prepare undocumented size prefix - size includes the size data */
    modules[0] = hFileInfo.ulFilesize + 4;

    /* Point to next word within module area - after the size */
    modules++;

    /* Load module */
    f = fopen(path, "rb");
    if (!f) {
      fprintf(stderr, "Could not open file \'%s\': %s\n",
              path, strerror(errno));
      continue;
    }

    if (fread(modules, 1, hFileInfo.ulFilesize, f) != hFileInfo.ulFilesize) {
      fprintf(stderr, "Error while loading file \'%s\': %s\n",
              path, strerror(errno));
      fclose(f);
      continue;
    }

    fclose(f);

    /* Byte-swap module from little-endian to host processor */
    extnrom_endian_correct(modules, hFileInfo.ulFilesize);

    /* Move chunk and module pointers on */
    chunk += 2;
    modules += ROUND_UP_TO_4(hFileInfo.ulFilesize) / 4;
  }

  Directory_Close(hDir);

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

#endif /* defined(EXTNROM_SUPPORT) */
