/* filecalls.c win32 implimentatation of the bstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */
/* ansi includes */
#include <assert.h>
#include <stdio.h>

/* windows includes */
#include <windows.h>

/* application includes */
#include "filecalls.h"


/* The windows version of the directory struct */
//struct Directory_s {
//  char *sPath;
//  WIN32_FIND_DATA w32fd;
//  HANDLE hFile;  
//  unsigned char bFirstEntry;
//};

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
  WIN32_FIND_DATA w32fd = {0};

  assert(sPath);
  assert(*sPath);

  if(sPath[strlen(sPath)] != '/') {
    bNeedsEndSlash = true;
  }

  hDir->bFirstEntry = true;
  hDir->hFile       = NULL;

  hDir->sPath = calloc(strlen(sPath) + (bNeedsEndSlash ? 1 : 0 ) + 3 + 1, 1); /* Path + Endslash (if needed) + *.* + terminator */
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
char *Directory_GetNextEntry(Directory *hDirectory)
{

  if(hDirectory->bFirstEntry) {
  	hDirectory->hFile = FindFirstFile(hDirectory->sPath, &hDirectory->w32fd);
    hDirectory->bFirstEntry = false;

    if(INVALID_HANDLE_VALUE == hDirectory->hFile) {
      return NULL;
    } else {
      return hDirectory->w32fd.cFileName;
    }
  } else {
    /* second or later entry */
    if(0 == FindNextFile(hDirectory->hFile, &hDirectory->w32fd)) {
      return NULL;
    } else {
      return hDirectory->w32fd.cFileName;    
    }
  }
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
bool File_GetInfo(const char *sPath, FileInfo *phFileInfo)
{
  WIN32_FILE_ATTRIBUTE_DATA w32fad = {0};

  assert(sPath);
  assert(*sPath);
  assert(phFileInfo);

  /* Initialise contents */
  phFileInfo->bIsDirectory   = false;
  phFileInfo->bIsRegularFile = false;

  /* Read the details */
  if (!GetFileAttributesEx(sPath, GetFileExInfoStandard, &w32fad))
  {
    fprintf(stderr, "Failed to stat '%s'\n", sPath);
    return false;
  }
  
  /* Fill in entries */
  phFileInfo->ulFilesize = w32fad.nFileSizeLow;

  if (w32fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    phFileInfo->bIsDirectory = true;
  } else {
    /* IMPROVE guess, if not a directory then we are a regular file */
    phFileInfo->bIsRegularFile = true;
  }
  
  /* Success! */
  return true;
}

