#ifndef AMIGA_PLATFORM_H
#define AMIGA_PLATFORM_H 1

#include <intuition/intuition.h>
#include <proto/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#ifdef __amigaos4__ /* weird _NewObject error */
#include <proto/intuition.h>
#endif
#include <proto/graphics.h>
#include <proto/utility.h>
/* #include <proto/input.h> */
/* #include <proto/picasso96api.h> */

#ifndef __amigaos4__
#define IDCMP_EXTENDEDMOUSE 0
#endif

extern void cleanup(void);
extern void sound_exit(void);

extern int force8bit;
extern int swapmousebuttons;
extern BOOL anymonitor;
extern BOOL use_ocm;
#endif
