/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#if !defined(_WIN32) && !defined(__riscos__)

/* ansi includes */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* posix includes */
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <dirent.h>

/* application includes */
#include "filecalls.h"
#include "dbugsys.h"

static bool File_GetInfo(const char *sPath, FileInfo *phFileInfo);

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

  hDirectory->sPathLen = strlen(sPath);
  hDirectory->sPath = malloc(hDirectory->sPathLen + 1);

  if(NULL == hDirectory->sPath) {
    return false;
  } else {
    strcpy(hDirectory->sPath, sPath);
  }

  hDirectory->hDir = opendir(sPath);

  if(NULL == hDirectory->hDir) {
    return false;
  } else {
    return true;
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
  struct dirent *phDirEntry;
  
  assert(hDirectory);
  
  phDirEntry = readdir(hDirectory->hDir);
  if(!phDirEntry) {
    return NULL;
  }

  if (phFileInfo) {
    phFileInfo->bIsRegularFile = false;
    phFileInfo->bIsDirectory   = false;

#ifdef _DIRENT_HAVE_D_TYPE
    if (phDirEntry->d_type == DT_REG || phDirEntry->d_type == DT_DIR) {
      phFileInfo->bIsRegularFile = (phDirEntry->d_type == DT_REG);
      phFileInfo->bIsDirectory   = (phDirEntry->d_type == DT_DIR);
    } else
#endif
    {
      char *path = Directory_GetFullPath(hDirectory, phDirEntry->d_name);
      if (path) {
        File_GetInfo(path, phFileInfo);
        free(path);
      }
    }
  }

  return phDirEntry->d_name;
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
  strcat(path, "/");
  strcat(path, leaf);
  return path;
}

/**
 * File_GetInfo
 *
 * Fills in lots of useful info about the passed in file
 *
 * @param sPath Path to file to check
 * @param phFileInfo pointer to FileInfo struct to fill in
 * @returns false on failure true on success
 */
static bool File_GetInfo(const char *sPath, FileInfo *phFileInfo)
{
  struct stat hEntryInfo;

  assert(sPath);
  assert(phFileInfo);

  if (stat(sPath, &hEntryInfo) != 0) {
    warn_data("Warning: could not stat() entry \'%s\': %s\n",
            sPath, strerror(errno));
    return false;
  }
  
  /* Initialise components */
  phFileInfo->bIsRegularFile = false;
  phFileInfo->bIsDirectory   = false;

  if (S_ISREG(hEntryInfo.st_mode)) {
    phFileInfo->bIsRegularFile = true;
  }

  if (S_ISDIR(hEntryInfo.st_mode)) {
    phFileInfo->bIsDirectory = true;
  }
  
  /* Success! */
  return true;
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
	struct statvfs s;
	int ret;

	assert(path != NULL);
	assert(d != NULL);

	if ((ret = statvfs(path, &s)) != 0) {
		return false;
	}

	d->size = (uint64_t) s.f_blocks * (uint64_t) s.f_frsize;
	d->free = (uint64_t) s.f_bavail * (uint64_t) s.f_frsize;

	return true;
}

#endif

