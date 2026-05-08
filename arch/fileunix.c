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
#ifndef __amigaos3__
#include <sys/statvfs.h>
#endif
#include <sys/types.h>
#include <dirent.h>

/* application includes */
#include "filecalls.h"
#include "dbugsys.h"

/* The POSIX version of the directory struct */
struct DirEntry_s {
  struct dirent *hDirent;
  Directory *hDirectory;

  struct stat hStat;
  bool bHasStat;
};

struct Directory_s {
  DIR *hDir;
  char *sPath;
  size_t sPathLen;

  DirEntry hDirEntry;
};

Directory *Directory_Open(const char *sPath)
{
  Directory *hDirectory;
  size_t sPathLen;

  assert(sPath);
  assert(*sPath);

  sPathLen = strlen(sPath);
  hDirectory = malloc(sizeof(Directory) + sPathLen + 1);

  if(NULL == hDirectory)
    return NULL;

  hDirectory->hDirEntry.hDirectory = hDirectory;
  hDirectory->sPathLen = sPathLen;
  hDirectory->sPath = (char *)(hDirectory + 1);
  strcpy(hDirectory->sPath, sPath);

  hDirectory->hDir = opendir(sPath);

  if(NULL == hDirectory->hDir) {
    free(hDirectory);
    return NULL;
  } else {
    return hDirectory;
  }
}

void Directory_Close(Directory *hDirectory)
{
  closedir(hDirectory->hDir);
  free(hDirectory);
}

DirEntry *Directory_GetNextEntry(Directory *hDirectory)
{
  DirEntry *hDirEntry;
  assert(hDirectory);

  hDirEntry = &hDirectory->hDirEntry;
  hDirEntry->hDirent = readdir(hDirectory->hDir);
  if(!hDirEntry->hDirent) {
    return NULL;
  }

  hDirEntry->bHasStat = false;
  return &hDirectory->hDirEntry;
}

static char *Directory_GetEntryPath(DirEntry *hDirEntry)
{
  Directory *hDirectory = hDirEntry->hDirectory;
  const char *leaf = hDirEntry->hDirent->d_name;
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

static struct stat *Directory_Stat(DirEntry *hDirEntry)
{
  char *sPath;

  if (hDirEntry->bHasStat)
    return &hDirEntry->hStat;

  sPath = Directory_GetEntryPath(hDirEntry);
  if (!sPath) {
    return NULL;
  }

  if (stat(sPath, &hDirEntry->hStat) != 0) {
    warn_data("Warning: could not stat() entry \'%s\': %s\n",
              sPath, strerror(errno));
    free(sPath);
    return NULL;
  }

  free(sPath);

  hDirEntry->bHasStat = true;
  return &hDirEntry->hStat;
}

FILE *Directory_OpenEntryFile(DirEntry *hDirEntry, const char *sMode)
{
  char *sPath;
  FILE *f;

  sPath = Directory_GetEntryPath(hDirEntry);
  if (!sPath) {
    return NULL;
  }

  f = fopen(sPath, sMode);
  if (!f) {
    warn_data("Warning: could not fopen() entry \'%s\': %s\n",
              sPath, strerror(errno));
    free(sPath);
    return NULL;
  }

  free(sPath);
  return f;
}

const char *Directory_GetEntryName(DirEntry *hDirEntry)
{
  assert(hDirEntry);
  return hDirEntry->hDirent->d_name;
}

ObjectType Directory_GetEntryType(DirEntry *hDirEntry)
{
  struct stat *hStat;
  assert(hDirEntry);

#ifdef _DIRENT_HAVE_D_TYPE
  if (hDirEntry->hDirent->d_type == DT_REG) {
    return OBJECT_TYPE_FILE;
  } else if (hDirEntry->hDirent->d_type == DT_DIR) {
    return OBJECT_TYPE_DIRECTORY;
  } else
#endif
  {
    if ((hStat = Directory_Stat(hDirEntry)) == NULL)
      return OBJECT_TYPE_NOT_FOUND;

    if (S_ISREG(hStat->st_mode)) {
      return OBJECT_TYPE_FILE;
    } else if (S_ISDIR(hStat->st_mode)) {
      return OBJECT_TYPE_DIRECTORY;
    } else {
      return OBJECT_TYPE_NOT_FOUND;
    }
  }
}

bool Directory_GetEntrySize(DirEntry *hDirEntry, Offset *ulFilesize)
{
  struct stat *hStat;
  assert(hDirEntry);
  assert(ulFilesize);

  if ((hStat = Directory_Stat(hDirEntry)) == NULL)
    return false;
  if (hStat->st_size < 0)
    return false;

  *ulFilesize = hStat->st_size;
  return true;
}

bool Disk_GetInfo(const char *path, DiskInfo *d)
{
#ifndef __amigaos3__
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
#else
	UNUSED_VAR(path);
	UNUSED_VAR(d);
	return false;
#endif
}

#endif

