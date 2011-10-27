/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */
/* ansi includes */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* posix includes */
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

/* application includes */
#include "filecalls.h"

/**
 * Directory_Open
 *
 * Open a directory so that it's contents can be scanned
 *
 * @param sPath Location of directory to scan
 * @param hDir Pointer to a Directory struct to fill in
 * @returns 1 on success 0 on failure
 */
unsigned Directory_Open(const char *sPath, Directory *hDirectory)
{
  assert(sPath);
  assert(*sPath);
  assert(hDirectory);

  hDirectory->hDir = opendir(sPath);

  if(NULL == hDirectory->hDir) {
    return 0;
  } else {
    return 1;
  }
}

/**
 * Directory_Close
 *
 * Close a previously opened Directory
 *
 * @param hDirectory Directory to close
 */
void Directory_Close(Directory hDirectory)
{
  closedir(hDirectory.hDir);
}

/**
 * Directory_GetNextEntry
 *
 * Get the next entry in a directory
 *
 * @param hDirectory pointer to Directory to get entry from
 * @returns String of filename or NULL on EndOfDirectory
 */
char *Directory_GetNextEntry(Directory *hDirectory)
{
  struct dirent *phDirEntry;
  
  assert(hDirectory);
  
  phDirEntry = readdir(hDirectory->hDir);
  if(phDirEntry) {
    return phDirEntry->d_name;
  } else {
    return NULL;
  }
}

/**
 * File_GetInfo
 *
 * Fills in lots of useful info about the passed in file
 *
 * @param sPath Path to file to check
 * @param phFileInfo pointer to FileInfo struct to fill in
 * @returns 0 on failure 1 on success
 */
unsigned char File_GetInfo(const char *sPath, FileInfo *phFileInfo)
{
  struct stat hEntryInfo;

  assert(sPath);
  assert(phFileInfo);

  if (stat(sPath, &hEntryInfo) != 0) {
    fprintf(stderr, "Warning: could not stat() entry \'%s\': %s\n",
            sPath, strerror(errno));
    return 0;
  }
  
  /* Initialise components */
  phFileInfo->bIsRegularFile = 0;
  phFileInfo->bIsDirectory   = 0;

  if (S_ISREG(hEntryInfo.st_mode)) {
    phFileInfo->bIsRegularFile = 1;
  }

  if (S_ISDIR(hEntryInfo.st_mode)) {
    phFileInfo->bIsDirectory = 1;
  }
  
  /* Fill in Size */
  phFileInfo->ulFilesize = hEntryInfo.st_size;
  
  /* Success! */
  return 1;
}

