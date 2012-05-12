/* filecalls_internal.h
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifndef __FILECALLS_INTERNAL_H
#define __FILECALLS_INTERNAL_H

#include <Windows.h>

struct Directory_s {
  char *sPath;
  WIN32_FIND_DATA w32fd;
  HANDLE hFile;  
  bool bFirstEntry;
};

#define ARCEM_PATH_MAX MAX_PATH

#endif /* __FILECALLS_H */
