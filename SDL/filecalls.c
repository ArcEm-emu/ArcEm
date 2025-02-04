/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

/* ansi includes */
#include <stdio.h>

/* SDL includes */
#include <SDL.h>

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
#if SDL_VERSION_ATLEAST(2, 0, 1)
    char *sAppData, *sPath;
    size_t sLen;
    FILE *f;

    sAppData = SDL_GetPrefPath(NULL, "arcem");
    if (!sAppData) {
        return NULL;
    }

    sLen = SDL_strlen(sAppData) + SDL_strlen(sName) + 1;
    sPath = SDL_malloc(sLen);
    if (!sPath) {
        SDL_free(sAppData);
        return NULL;
    }

    SDL_strlcpy(sPath, sAppData, sLen);
    SDL_strlcat(sPath, sName, sLen);
    f = fopen(sPath, sMode);

    SDL_free(sPath);
    SDL_free(sAppData);
    return f;
#else
    return NULL;
#endif
}
