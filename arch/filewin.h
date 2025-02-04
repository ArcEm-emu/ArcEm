/* filewin.h
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifndef __FILEWIN_H
#define __FILEWIN_H

#include <windows.h>

struct Directory_s {
  char *sPath;
  size_t sPathLen;
  WIN32_FIND_DATAA w32fd;
  HANDLE hFile;
  bool bFirstEntry;
};

#endif /* __FILEWIN_H */
