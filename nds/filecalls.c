/* filecalls.c posix implimentatation of the abstracted interface to accesing host
   OS file and Directory functions.
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

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
    return NULL;
}
