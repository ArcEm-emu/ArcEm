#ifndef AMIGA_PLATFORM_H
#define AMIGA_PLATFORM_H 1

#include <intuition/intuition.h>
#include <proto/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
//#include <proto/input.h>
//#include <proto/picasso96api.h>

struct Library *ExecBase;
struct ExecIFace *IExec;
struct Library *IntuitionBase;
struct IntuitionIFace *IIntuition;
//struct Library *P96Base;
//struct P96IFace *IP96;
struct Library *GfxBase;
struct GraphicsIFace *IGraphics;
struct Library *DOSBase;
struct DOSIFace *IDOS;
struct Library *AslBase;
struct AslIFace *IAsl;
struct Library *IconBase;
struct IconIFace *IIcon;
struct Library *UtilityBase;
struct UtilityIFace *IUtility;
//struct Device *inputdevice;

extern void cleanup(void);
extern void sound_exit(void);

int force8bit;
#endif
