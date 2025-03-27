/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

/* ansi includes */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* posix includes */
#include <sys/stat.h>

/* application includes */
#include "filecalls.h"

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
    const char *sAppData, *sDir;
    char *sPath, *sPtr;
    size_t sLen;
    FILE *f;

    /* This uses XDG_DATA_HOME to match the SDL port. */
    sAppData = getenv("XDG_DATA_HOME");
    if (sAppData) {
        sDir = "/arcem/";
    } else {
        sAppData = getenv("HOME");
        if (sAppData) {
            sDir = "/.local/share/arcem/";
        } else {
            return NULL;
        }
    }

    sLen = strlen(sAppData) + strlen(sDir) + strlen(sName) + 1;
    sPath = malloc(sLen);
    if (!sPath) {
        return NULL;
    }

    strcpy(sPath, sAppData);
    strcat(sPath, sDir);
    strcat(sPath, sName);

    for (sPtr = sPath + 1; *sPtr; sPtr++) {
        if (*sPtr != '/')
            continue;

        *sPtr = 0;
        mkdir(sPath, 0700);
        *sPtr = '/';
    }

    f = fopen(sPath, sMode);
    free(sPath);
    return f;
}
