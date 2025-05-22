/* filecalls.h abstracted interface to accesing host OS file and Directory functions
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifndef __FILECALLS_H
#define __FILECALLS_H

#include "../armdefs.h"
#include <stdio.h>

typedef struct Directory_s Directory;

typedef struct FileInfo
{
  bool bIsRegularFile;
  bool bIsDirectory;
} FileInfo;

typedef struct DiskInfo
{
  uint64_t	size;		/**< Size of disk */
  uint64_t	free;		/**< Free space on disk */
} DiskInfo;

/**
 * Directory_Open
 *
 * Open a directory so that it's contents can be scanned
 *
 * @param sPath Location of directory to scan
 * @returns Directory handle or NULL on failure
 */
Directory *Directory_Open(const char *sPath);

/**
 * Directory_Close
 *
 * Close a previously opened Directory
 *
 * @param hDirectory Directory to close
 */
void Directory_Close(Directory *hDirectory);

/**
 * Directory_GetNextEntry
 *
 * Get the next entry in a directory
 *
 * @param hDirectory pointer to Directory to get entry from
 * @returns String of filename or NULL on EndOfDirectory
 */
char *Directory_GetNextEntry(Directory *hDirectory, FileInfo *phFileInfo);

/**
 * Directory_GetFullPath
 *
 * Get the full path of a file in a directory
 *
 * @param hDirectory pointer to Directory to get the base path from
 * @returns String of the full path or NULL on EndOfDirectory
 */
char *Directory_GetFullPath(Directory *hDirectory, const char *leaf);

/**
 * Return disk space information about a file system.
 *
 * @param path Pathname of object within file system
 * @param d    Pointer to disk_info structure that will be filled in
 * @return     On success 1 is returned, on error 0 is returned
 */
bool Disk_GetInfo(const char *path, DiskInfo *d);

/**
 * File_OpenAppData
 *
 * Open the specified file in the application data directory
 *
 * @param sName Name of file to open
 * @param sMode Mode to open the file with
 * @returns File handle or NULL on failure
 */
FILE *File_OpenAppData(const char *sName, const char *sMode);

/**
 * Directory_OpenAppDir
 *
 * Open the specified directory in the application directory
 *
 * @param sName of directory to scan
 * @returns Directory handle or NULL on failure
 */
Directory *Directory_OpenAppDir(const char *sName);

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
