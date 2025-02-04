/* filero.h
   Copyright (c) 2005 Peter Howkins, covered under the GNU GPL see file COPYING for more
   details */

#ifndef __FILERO_H
#define __FILERO_H

#include "kernel.h"

struct Directory_s {
  char *sPath;
  size_t sPathLen;
  int gbpb_entry;

  struct {
    ARMword load;
    ARMword exec;
    ARMword length;
    ARMword attribs;
    ARMword type;
    char name[80];
  } gbpb_buffer;
};

#endif /* __FILERO_H */
