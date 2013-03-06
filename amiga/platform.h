#ifndef AMIGA_PLATFORM_H
#define AMIGA_PLATFORM_H 1

#include <intuition/intuition.h>
#include <proto/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#ifdef __amigaos4__ // weird _NewObject error
#include <proto/intuition.h>
#endif
#include <proto/graphics.h>
#include <proto/utility.h>
//#include <proto/input.h>
//#include <proto/picasso96api.h>

#ifdef __amigaos4__
struct Library *ExecBase;
struct Library *IntuitionBase;
struct Library *GfxBase;
struct Library *DOSBase;
struct Library *AslBase;
struct Library *IconBase;
struct Library *UtilityBase;
//struct Device *inputdevice;

struct ExecIFace *IExec;
struct IntuitionIFace *IIntuition;
struct GraphicsIFace *IGraphics;
struct DOSIFace *IDOS;
struct AslIFace *IAsl;
struct IconIFace *IIcon;
struct UtilityIFace *IUtility;
#else
#define IDCMP_EXTENDEDMOUSE 0
#endif

extern void cleanup(void);
extern void sound_exit(void);

int force8bit;
int swapmousebuttons;
BOOL anymonitor;
BOOL use_ocm;
#endif
