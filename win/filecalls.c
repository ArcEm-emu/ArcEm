/* filecalls.c win32 implimentatation of the bstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

/* ansi includes */
#include <stdio.h>

/* windows includes */
#include <windows.h>
#include <shlobj.h>

/* application includes */
#include "filecalls.h"


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

/**
 * Directory_OpenAppDir
 *
 * Open the specified directory in the application directory
 *
 * @param sName of directory to scan
 * @param hDir Pointer to a Directory struct to fill in
 * @returns true on success false on failure
 */
bool Directory_OpenAppDir(const char *sName, Directory *hDirectory)
{
    return false;
}
