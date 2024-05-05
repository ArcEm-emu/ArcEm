/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */
/* ansi includes */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* OS includes */
#include "kernel.h"
#include "swis.h"

/* application includes */
#include "filecalls.h"

/**
 * Directory_Open
 *
 * Open a directory so that it's contents can be scanned
 *
 * @param sPath Location of directory to scan
 * @param hDir Pointer to a Directory struct to fill in
 * @returns true on success false on failure
 */
bool Directory_Open(const char *sPath, Directory *hDirectory)
{
  assert(sPath);
  assert(*sPath);
  assert(hDirectory);

  memset(hDirectory, 0, sizeof(*hDirectory));

  hDirectory->sPathLen = strlen(sPath);
  hDirectory->sPath = malloc(hDirectory->sPathLen + 1);

  if(NULL == hDirectory->sPath) {
    return false;
  } else {
    strcpy(hDirectory->sPath, sPath);
  }

  return true;
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
  free(hDirectory.sPath);
}

/**
 * Directory_GetNextEntry
 *
 * Get the next entry in a directory
 *
 * @param hDirectory pointer to Directory to get entry from
 * @returns String of filename or NULL on EndOfDirectory
 */
char *Directory_GetNextEntry(Directory *hDirectory, FileInfo *phFileInfo)
{
  _kernel_oserror *err;
  int found;
  
  assert(hDirectory);

  err = _swix(OS_GBPB, _INR(0,6)|_OUTR(3,4), 10,
              hDirectory->sPath, &hDirectory->gbpb_buffer, 1,
              hDirectory->gbpb_entry, sizeof(hDirectory->gbpb_buffer),
              NULL, &found, &hDirectory->gbpb_entry);
  if(err || !found) {
    return NULL;
  }

  if (phFileInfo) {
    /* Initialise components */
    phFileInfo->bIsRegularFile = false;
    phFileInfo->bIsDirectory   = false;

    if (hDirectory->gbpb_buffer.type == 1 || hDirectory->gbpb_buffer.type == 3) {
      phFileInfo->bIsRegularFile = true;
    }

    if (hDirectory->gbpb_buffer.type == 2 || hDirectory->gbpb_buffer.type == 3) {
      phFileInfo->bIsDirectory = true;
    }
  }

  return hDirectory->gbpb_buffer.name;
}

/**
 * Directory_GetFullPath
 *
 * Get the full path of a file in a directory
 *
 * @param hDirectory pointer to Directory to get the base path from
 * @returns String of the full path or NULL on EndOfDirectory
 */
char *Directory_GetFullPath(Directory *hDirectory, const char *leaf) {
  size_t len = hDirectory->sPathLen + strlen(leaf) + 1;
  char *path = malloc(len + 1);
  if (!path) {
    return NULL;
  }

  strcpy(path, hDirectory->sPath);
  strcat(path, ".");
  strcat(path, leaf);
  return path;
}

/**
 * File_OpenAppData
 *
 * Open the specified file in the application data directory
 *
 * @param sName Name of file to open
 * @param sMode Mode to open the file with
 * @returns File handle or NULL on failure
 */
FILE *File_OpenAppData(const char *sName, const char *sMode)
{
    return NULL;
}
