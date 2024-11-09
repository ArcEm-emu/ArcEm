/* fileunix.h
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifndef __FILEUNIX_H
#define __FILEUNIX_H

#include <sys/types.h>
#include <dirent.h>

struct Directory_s {
  DIR *hDir;
  char *sPath;
  size_t sPathLen;
};

#endif /* __FILEUNIX_H */
