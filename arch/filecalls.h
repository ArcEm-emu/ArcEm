/* filecalls.h abstracted interface to accesing host OS file and Directory functions
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifndef __FILECALLS_H
#define __FILECALLS_H

#include "../armdefs.h"
/* Include this platforms required parts, note the platform directory
   must be on the compilers include path */
#include "filecalls_internal.h"

typedef struct Directory_s Directory;

/**
 * Directory_Open
 *
 * Open a directory so that it's contents can be scanned
 *
 * @param sPath Location of directory to scan
 * @param hDir Pointer to a Directory struct to fill in
 * @returns 1 on success 0 on failure
 */
bool Directory_Open(const char *sPath, Directory *hDir);

/**
 * Directory_Close
 *
 * Close a previously opened Directory
 *
 * @param hDirectory Directory to close
 */
void Directory_Close(Directory hDirectory);

/**
 * Directory_GetNextEntry
 *
 * Get the next entry in a directory
 *
 * @param hDirectory pointer to Directory to get entry from
 * @returns String of filename or NULL on EndOfDirectory
 */
char *Directory_GetNextEntry(Directory *hDirectory);

typedef struct FileInfo
{
  uint32_t ulFilesize;      /* In bytes */
  bool bIsRegularFile;
  bool bIsDirectory;
} FileInfo;

/**
 * File_GetInfo
 *
 * Fills in lots of useful info about the passed in file
 *
 * @param sPath Path to file to check
 * @param phFileInfo pointer to FileInfo struct to fill in
 * @returns 0 on failure 1 on success
 */
bool File_GetInfo(const char *sPath, FileInfo *phFileInfo);

/* These next few are implemented in arch/filecommon.c */

/**
 * File_ReadEmu
 *
 * Reads from the given file handle into the given buffer
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to read from
 * @param pBuffer Buffer to write to
 * @param uCount Number of bytes to read
 * @returns Number of bytes read
 */
size_t File_ReadEmu(FILE *pFile,uint8_t *pBuffer,size_t uCount);

/**
 * File_WriteEmu
 *
 * Writes data to the given file handle
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to write to
 * @param pBuffer Buffer to read from
 * @param uCount Number of bytes to write
 * @returns Number of bytes written
 */
size_t File_WriteEmu(FILE *pFile,const uint8_t *pBuffer,size_t uCount);

/**
 * File_ReadRAM
 *
 * Reads from the given file handle into emulator memory
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to read from
 * @param uAddress Logical address of buffer to write to
 * @param uCount Number of bytes to read
 * @returns Number of bytes read
 */
size_t File_ReadRAM(ARMul_State *state,FILE *pFile,ARMword uAddress,size_t uCount);

/**
 * File_WriteRAM
 *
 * Writes data from emulator memory to the given file handle
 * Data will be endian swapped to match the byte order of the emulated RAM
 *
 * @param pFile File to write to
 * @param uAddress Logical address of buffer to read from
 * @param uCount Number of bytes to write
 * @returns Number of bytes written
 */
size_t File_WriteRAM(ARMul_State *state,FILE *pFile,ARMword uAddress,size_t uCount);

#endif /* __FILECALLS_H */
