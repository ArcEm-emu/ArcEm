/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

/* ansi includes */
#include <stdio.h>

/* application includes */
#include "../arch/filecalls.h"
#include "platform.h"

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
#if SDL_VERSION_ATLEAST(3, 0, 0)
    const char *sBasePath;
    char *sPath;
    size_t sLen;
    Directory *ret;

    sBasePath = SDL_GetBasePath();
    if (!sBasePath) {
        return NULL;
    }

    sLen = SDL_strlen(sBasePath) + SDL_strlen(sName) + 1;
    sPath = SDL_malloc(sLen);
    if (!sPath) {
        return NULL;
    }

    SDL_strlcpy(sPath, sBasePath, sLen);
    SDL_strlcat(sPath, sName, sLen);
    ret = Directory_Open(sPath);

    SDL_free(sPath);
    return ret;
#elif SDL_VERSION_ATLEAST(2, 0, 1)
    char *sBasePath, *sPath;
    size_t sLen;
    Directory *ret;

    sBasePath = SDL_GetBasePath();
    if (!sBasePath) {
        return NULL;
    }

    sLen = SDL_strlen(sBasePath) + SDL_strlen(sName) + 1;
    sPath = SDL_malloc(sLen);
    if (!sPath) {
        SDL_free(sBasePath);
        return NULL;
    }

    SDL_strlcpy(sPath, sBasePath, sLen);
    SDL_strlcat(sPath, sName, sLen);
    ret = Directory_Open(sPath);

    SDL_free(sPath);
    SDL_free(sBasePath);
    return ret;
#else
    return NULL;
#endif
}
