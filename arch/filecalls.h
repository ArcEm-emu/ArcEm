/* filecalls.h abstracted interface to accesing host OS file and Directory functions
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifndef __FILECALLS_H
#define __FILECALLS_H

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
unsigned Directory_Open(const char *sPath, Directory *hDir);

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
  unsigned long ulFilesize;      /* In bytes */
  unsigned char bIsRegularFile;
  unsigned char bIsDirectory;
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
unsigned char File_GetInfo(const char *sPath, FileInfo *phFileInfo);

#endif /* __FILECALLS_H */