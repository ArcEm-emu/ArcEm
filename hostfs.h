/*
 * $Id$
 */

#ifndef HOSTFS_H
#define HOSTFS_H

#include "armdefs.h"

#define ARCEM_SWI_CHUNK    0x56ac0
#define ARCEM_SWI_SHUTDOWN  (ARCEM_SWI_CHUNK + 0)
#define ARCEM_SWI_HOSTFS    (ARCEM_SWI_CHUNK + 1)
#define ARCEM_SWI_DEBUG     (ARCEM_SWI_CHUNK + 2)
#define ARCEM_SWI_NANOSLEEP (ARCEM_SWI_CHUNK + 3)
#define ARCEM_SWI_NETWORK   (ARCEM_SWI_CHUNK + 4)

#define rpclog(...) fprintf(stderr,__VA_ARGS__)
#define error(...) fprintf(stderr,__VA_ARGS__)

#define HOSTFS_ARCEM /* Build ArcEm version, not RPCEmu */

extern void hostfs(ARMul_State *state);
extern void hostfs_init(void);
extern void hostfs_reset(void);

#ifdef __amigaos4__
#include <sys/_types.h>
typedef _off64_t off64_t;
#endif

#ifdef __amigaos3__
typedef long off64_t;

#define ftello64 ftell
#define fseeko64 fseek
#define fopen64 fopen
#endif


#endif
