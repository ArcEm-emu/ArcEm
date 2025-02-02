/* filecalls.c win32 implimentatation of the bstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */
/* ansi includes */
#include <assert.h>
#include <stdio.h>

/* windows includes */
#include <windows.h>
#include <shlobj.h>

/* application includes */
#include "filecalls.h"


/* The windows version of the directory struct */
#if 0
struct Directory_s {
  char *sPath;
  WIN32_FIND_DATA w32fd;
  HANDLE hFile;  
  unsigned char bFirstEntry;
};
#endif

/* Compatibility wrapper for Windows 95 without Internet Explorer 4 */
static BOOL SHGetSpecialFolderPathAFunc(HWND hwnd, LPSTR pszPath, int csidl, BOOL fCreate) {
  static bool loaded = false;
  static HMODULE shell32 = NULL;

  typedef BOOL(WINAPI* Fn)(HWND hwnd, LPSTR pszPath, int csidl, BOOL fCreate);
  Fn pFn;

  if (!loaded) {
    shell32 = LoadLibrary(TEXT("shell32.dll"));
    loaded = true;
  }

  if (!shell32)
    return FALSE;

  pFn = (Fn)(void*)GetProcAddress(shell32, "SHGetSpecialFolderPathA");
  if (pFn)
    return pFn(hwnd, pszPath, csidl, fCreate);

  return FALSE;
}

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
    const char *sDir = "\\ArcEm\\";
    char sAppData[MAX_PATH];
    char *sPath;
    FILE *f;

    if (!SHGetSpecialFolderPathAFunc(NULL, sAppData, CSIDL_APPDATA, TRUE)) {
        return NULL;
    }

    sPath = malloc(strlen(sAppData) + strlen(sDir) + strlen(sName) + 1);
    if (!sPath) {
        return NULL;
    }

    strcpy(sPath, sAppData);
    strcat(sPath, sDir);
    CreateDirectoryA(sPath, NULL);

    strcat(sPath, sName);
    f = fopen(sPath, sMode);

    free(sPath);
    return f;
}
