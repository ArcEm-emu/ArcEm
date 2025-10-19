/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#include <stdlib.h>
#include <string.h>

/* application includes */
#include "../arch/filecalls.h"

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

/**
 * Directory_OpenAppDir
 *
 * Open the specified directory in the application directory
 *
 * @param sName of directory to scan
 * @returns Directory handle or NULL on failure
 */
Directory *Directory_OpenAppDir(const char *sName)
{
    static const char sPrefix[] = "nitro:/";
    Directory *dir;
    size_t sLen;
    char *sPath;

    sLen = strlen(sPrefix) + strlen(sName) + 1;
    sPath = malloc(sLen);
    if (!sPath) {
        return NULL;
    }

    strcpy(sPath, sPrefix);
    strcat(sPath, sName);
    dir = Directory_Open(sPath);

    free(sPath);
    return dir;
}
