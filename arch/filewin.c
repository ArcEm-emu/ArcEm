/* filecalls.c win32 implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifdef _WIN32

/* ansi includes */
#include <assert.h>
#include <stdio.h>

/* windows includes */
#include <windows.h>

/* application includes */
#include "filecalls.h"


/* The windows version of the directory struct */
#if 0
struct Directory_s {
  char *sPath;
  WIN32_FIND_DATA w32fd;
  HANDLE hFile;
  bool bFirstEntry;
};
#endif

/**
 * Directory_Open
 *
 * Open a directory so that it's contents can be scanned
 *
 * @param sPath Location of directory to scan
 * @param hDir Pointer to a Directory struct to fill in
 * @returns true on success false on failure
 */
bool Directory_Open(const char *sPath, Directory *hDir)
{
  bool bNeedsEndSlash = 0;

  assert(sPath);
  assert(*sPath);

  if(sPath[strlen(sPath)] != '/') {
    bNeedsEndSlash = true;
  }

  hDir->bFirstEntry = true;
  hDir->hFile       = NULL;

  hDir->sPathLen = strlen(sPath) + (bNeedsEndSlash ? 1 : 0 ) + 3; /* Path + Endslash (if needed) + *.* + terminator */
  hDir->sPath = calloc(1, hDir->sPathLen + 1);
  if(NULL == hDir->sPath) {
    fprintf(stderr, "Failed to allocate memory for directory handle path\n");
    return false;
  }

  strcpy(hDir->sPath, sPath);
  if(bNeedsEndSlash) {
    strcat(hDir->sPath, "/");
  }
  strcat(hDir->sPath, "*.*");

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

  FindClose(hDirectory.hFile);
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

  if(hDirectory->bFirstEntry) {
    hDirectory->hFile = FindFirstFileA(hDirectory->sPath, &hDirectory->w32fd);
    hDirectory->bFirstEntry = false;

    if(INVALID_HANDLE_VALUE == hDirectory->hFile) {
      return NULL;
    }
  } else {
    /* second or later entry */
    if(0 == FindNextFileA(hDirectory->hFile, &hDirectory->w32fd)) {
      return NULL;
    }
  }

  if (phFileInfo) {
    if (hDirectory->w32fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      phFileInfo->bIsDirectory   = true;
      phFileInfo->bIsRegularFile = false;
    } else {
      /* IMPROVE guess, if not a directory then we are a regular file */
      phFileInfo->bIsDirectory   = false;
      phFileInfo->bIsRegularFile = true;
    }
  }

  return hDirectory->w32fd.cFileName;
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
  size_t len = hDirectory->sPathLen - 3 + strlen(leaf);
  char *path = malloc(len + 1);
  if (!path) {
    return NULL;
  }

  strcpy(path, hDirectory->sPath);
  path[hDirectory->sPathLen - 3] = 0;
  strcat(path, leaf);
  return path;
}

/**
 * Return disk space information about a file system.
 *
 * @param path Pathname of object within file system
 * @param d    Pointer to disk_info structure that will be filled in
 * @return     On success true is returned, on error false is returned
 */
bool Disk_GetInfo(const char *path, DiskInfo *d)
{
	ULARGE_INTEGER free, total;

	assert(path != NULL);
	assert(d != NULL);

	if (GetDiskFreeSpaceEx(path, &free, &total, NULL) == 0) {
		return false;
	}

	d->size = (uint64_t) total.QuadPart;
	d->free = (uint64_t) free.QuadPart;

	return true;
}

#endif

