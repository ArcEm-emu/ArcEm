#include <stdio.h>
#include <stdlib.h>
#include <string.h>



#include "../armdefs.h"
#include "../arch/armarc.h"
#include "../arch/ArcemConfig.h"
#include "../arch/dbugsys.h"
#include "../arch/keyboard.h"
#include "../arch/displaydev.h"
#include "../eventq.h"
#include "win.h"
#include "KeyTable.h"
#include "../arch/ControlPane.h"


#define MinimumWidth 512
#define MonitorWidth 1600
#define MonitorHeight 1200

extern void *dibbmp;
extern void *curbmp;
extern void *mskbmp;

extern size_t dibstride;
extern size_t curstride;
extern size_t mskstride;

extern ArcemConfig_DisplayDriver requestedDriver;
extern bool requestedAspect;
extern bool requestedUpscale;



int rMouseX = 0;
int rMouseY = 0;
int rMouseWidth = 0;
int rMouseHeight = 0;
int oldMouseX = 0;
int oldMouseY = 0;



/* Standard display device */

#define SDD_BitsPerPixel 16
#if SDD_BitsPerPixel == 24
typedef struct { uint8_t b, g, r; } SDD_HostColour;
#elif SDD_BitsPerPixel == 32
typedef uint32_t SDD_HostColour;
#else
typedef uint16_t SDD_HostColour;
#endif

#define SDD_Name(x) sdd_##x
static const int SDD_RowsAtOnce = 1;
typedef SDD_HostColour *SDD_Row;


static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col)
{
  SDD_HostColour hostCol;
  uint8_t r, g, b;

#if SDD_BitsPerPixel >= 24
  /* Convert to 8-bit component values */
  r = (col & 0x00f);
  g = (col & 0x0f0);
  b = (col & 0xf00) >> 8;
  /* May want to tweak this a bit at some point? */
  r |= r<<4;
  g |= g>>4;
  b |= b<<4;
#else
  /* Convert to 5-bit component values */
  r = (col & 0x00f) << 1;
  g = (col & 0x0f0) >> 3;
  b = (col & 0xf00) >> 7;
  /* May want to tweak this a bit at some point? */
  r |= r>>4;
  g |= g>>4;
  b |= b>>4;
#endif

#if SDD_BitsPerPixel == 24
  hostCol.b = b;
  hostCol.g = g;
  hostCol.r = r;
#elif SDD_BitsPerPixel == 32
  hostCol = (r<<16) | (g<<8) | (b);
#else
  hostCol = (r<<10) | (g<<5) | (b);
#endif

  return hostCol;
}

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  return ((SDD_Row)(void *) ((uint8_t *)dibbmp + dibstride*row))+offset;
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ }

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count) { /* nothing */ }

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ }

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { (*row) += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix) { *(*row)++ = pix; }

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count) { while(count--) *(*row)++ = pix; }

void SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  if((width > MonitorWidth) || (height > MonitorHeight))
  {
    ControlPane_Error(true,"Mode %dx%d too big",width,height);
  }
  HD.Width = width;
  HD.Height = height;
  HD.XScale = 1;
  HD.YScale = 1;
  /* Try and detect rectangular pixel modes */
  if(CONFIG.bAspectRatioCorrection && (width >= height*2) && (height*2 <= MonitorHeight))
  {
    HD.YScale = 2;
    HD.Height *= 2;
  }
  else if(CONFIG.bAspectRatioCorrection && (height >= width) && (width*2 <= MonitorWidth))
  {
    HD.XScale = 2;
    HD.Width *= 2;
  }
  /* Try and detect small screen resolutions */
  else if (CONFIG.bUpscale && (width < MinimumWidth) && (width * 2 <= MonitorWidth) && (height * 2 <= MonitorHeight))
  {
    HD.XScale  = 2;
    HD.YScale  = 2;
    HD.Width  *= 2;
    HD.Height *= 2;
  }
  createBitmaps(HD.Width,HD.Height,SDD_BitsPerPixel,HD.XScale);
  resizeWindow(HD.Width,HD.Height);
  /* Screen is expected to be cleared */
  memset(dibbmp,0,dibstride*HD.Height);
}

/* Refresh the mouse's image */
static void SDD_Name(RefreshMouse)(ARMul_State *state) {
  int x,y,xs,offset, pix, repeat;
  int memptr;
  int HorizPos;
  int Width = 32*HD.XScale;
  int Height = ((int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart)*HD.YScale;
  int VertPos;
  int diboffs;
  SDD_HostColour cursorPal[4];
  SDD_HostColour *host_dibbmp = (SDD_HostColour *)dibbmp;
  SDD_HostColour *host_curbmp = (SDD_HostColour *)curbmp;

  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  HorizPos = HorizPos*HD.XScale+HD.XOffset;
  VertPos = VertPos*HD.YScale+HD.YOffset;

  if (Height < 0) Height = 0;
  if (VertPos < 0) VertPos = 0;

  rMouseX = HorizPos;
  rMouseY = VertPos;
  rMouseWidth = Width;
  rMouseHeight = Height;

  /* Cursor palette */
  cursorPal[0] = SDD_Name(Host_GetColour)(state,0);
  for(x=0; x<3; x++) {
    cursorPal[x+1] = SDD_Name(Host_GetColour)(state,VIDC.CursorPalette[x]);
  }

  offset=0;
  memptr=MEMC.Cinit*16;
  repeat=0;
  host_dibbmp = (SDD_HostColour *)(void *) ((uint8_t *)host_dibbmp + (rMouseY*dibstride));
  for(y=0;y<Height;y++) {
    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++) {
        pix = ((tmp[x/16]>>((x & 15)*2)) & 3);

        for(xs=0;xs<HD.XScale;xs++) {
          diboffs = rMouseX + (x*HD.XScale) + xs;

          host_curbmp[(x*HD.XScale) + xs] =
                  (pix || diboffs < 0 || diboffs >= HD.Width) ?
                  cursorPal[pix] : host_dibbmp[diboffs];
        } /* xs */
      } /* x */
    } else return;
    if(++repeat == HD.YScale) {
      memptr+=8;
      offset+=8;
      repeat = 0;
    }
    host_dibbmp = (SDD_HostColour *)(void *) ((uint8_t *)host_dibbmp + dibstride);
    host_curbmp = (SDD_HostColour *)(void *) ((uint8_t *)host_curbmp + curstride);
  }; /* y */
} /* RefreshMouse */

void
SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  SDD_Name(RefreshMouse)(state);
  updateDisplay();
}


/* ------------------------------------------------------------------ */

/* Palettised display device */

#define PDD_Name(x) pdd_##x

typedef struct {
  ARMword *data;
  int offset;
} PDD_Row;

static void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz);

static void PDD_Name(Host_SetPaletteEntry)(ARMul_State *state,int i,uint_fast16_t phys)
{
  uint8_t r, g, b;

  /* Convert to 8-bit component values */
  r = (phys & 0x00f);
  g = (phys & 0x0f0);
  b = (phys & 0xf00) >> 8;
  /* May want to tweak this a bit at some point? */
  r |= r<<4;
  g |= g>>4;
  b |= b<<4;

  setPaletteColour(i, r, g, b);
}

static void PDD_Name(Host_SetCursorPaletteEntry)(ARMul_State *state,int i,uint_fast16_t phys)
{
  uint8_t r, g, b;

  /* Convert to 8-bit component values */
  r = (phys & 0x00f);
  g = (phys & 0x0f0);
  b = (phys & 0xf00) >> 8;
  /* May want to tweak this a bit at some point? */
  r |= r<<4;
  g |= g>>4;
  b |= b<<4;

  setCursorPaletteColour(i + 1, r, g, b);
}

static void PDD_Name(Host_SetBorderColour)(ARMul_State *state,uint_fast16_t phys)
{
  /* TODO */
}

static inline PDD_Row PDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset,int *alignment)
{
  PDD_Row drow;
  uintptr_t base = ((uintptr_t)dibbmp + dibstride*row) + offset;
  drow.offset = ((base<<3) & 0x18); /* Just in case bytes per line isn't aligned */
  drow.data = (ARMword *) (base & ~0x3);
  *alignment = drow.offset;
  return drow;
}

static inline void PDD_Name(Host_EndRow)(ARMul_State *state,PDD_Row *row) { /* nothing */ }

static inline ARMword *PDD_Name(Host_BeginUpdate)(ARMul_State *state,PDD_Row *row,unsigned int count,int *outoffset)
{
  *outoffset = row->offset;
  return row->data;
}

static inline void PDD_Name(Host_EndUpdate)(ARMul_State *state,PDD_Row *row) { /* nothing */ }

static inline void PDD_Name(Host_AdvanceRow)(ARMul_State *state,PDD_Row *row,unsigned int count)
{
  row->offset += count;
  row->data += count>>5;
  row->offset &= 0x1f;
}

static void
PDD_Name(Host_PollDisplay)(ARMul_State *state);

static void PDD_Name(Host_DrawBorderRect)(ARMul_State *state,int x,int y,int width,int height)
{
  /* TODO */
}

#include "../arch/paldisplaydev.c"

void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz)
{
  if((width > MonitorWidth) || (height > MonitorHeight))
  {
    ControlPane_Error(true,"Mode %dx%d too big",width,height);
  }
  HD.Width = width;
  HD.Height = height;
  HD.XScale = 1;
  HD.YScale = 1;
  /* Try and detect rectangular pixel modes */
  if(CONFIG.bAspectRatioCorrection && (width >= height*2) && (height*2 <= MonitorHeight))
  {
    HD.YScale = 2;
    HD.Height *= 2;
  }
  else if(CONFIG.bAspectRatioCorrection && (height >= width) && (width*2 <= MonitorWidth))
  {
    HD.XScale = 2;
    HD.Width *= 2;
  }
  /* Try and detect small screen resolutions */
  else if (CONFIG.bUpscale && (width < MinimumWidth) && (width * 2 <= MonitorWidth) && (height * 2 <= MonitorHeight))
  {
    HD.XScale  = 2;
    HD.YScale  = 2;
    HD.Width  *= 2;
    HD.Height *= 2;
  }
  createBitmaps(HD.Width,HD.Height,8,HD.XScale);
  resizeWindow(HD.Width,HD.Height);
  /* Screen is expected to be cleared */
  memset(dibbmp,0,dibstride*HD.Height);

  /* Calculate expansion params */
  if((depth == 3) && (HD.XScale == 1))
  {
    /* No expansion */
    HD.ExpandTable = NULL;
  }
  else
  {
    /* Expansion! */
    static ARMword expandtable[256];
    unsigned int mul = 1;
    int i;
    HD.ExpandFactor = 0;
    while((1<<HD.ExpandFactor) < HD.XScale)
      HD.ExpandFactor++;
    HD.ExpandFactor += (3-depth);
    HD.ExpandTable = expandtable;
    for(i=0;i<HD.XScale;i++)
    {
      mul |= 1<<(i*8);
    }
    GenExpandTable(HD.ExpandTable,1<<depth,HD.ExpandFactor,mul);
  }
}

/* Refresh the mouse's image */
static void PDD_Name(RefreshMouse)(ARMul_State *state) {
  int x,y,xs,offset, pix, repeat;
  int memptr;
  int HorizPos;
  int Width = 32*HD.XScale;
  int Height = ((int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart)*HD.YScale;
  int VertPos;
  uint8_t *host_curbmp = curbmp;
  uint8_t *host_mskbmp = mskbmp;

  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  HorizPos = HorizPos*HD.XScale+HD.XOffset;
  VertPos = VertPos*HD.YScale+HD.YOffset;

  if (Height < 0) Height = 0;
  if (VertPos < 0) VertPos = 0;

  rMouseX = HorizPos;
  rMouseY = VertPos;
  rMouseWidth = Width;
  rMouseHeight = Height;

  offset=0;
  memptr=MEMC.Cinit*16;
  repeat=0;
  for(y=0;y<Height;y++) {
    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(xs=0;xs<HD.XScale*4;xs++) {
        host_mskbmp[xs] = 0;
      }

      for(x=0;x<32;x++) {
        pix = ((tmp[x/16]>>((x & 15)*2)) & 3);

        for(xs=0;xs<HD.XScale;xs++) {
		  int idx = (x*HD.XScale) + xs;
          host_curbmp[idx] = pix;
          if (pix == 0) {
            host_mskbmp[idx/8] |= 0x80 >> (idx%8);
          }
        } /* xs */
      } /* x */
    } else return;
    if(++repeat == HD.YScale) {
      memptr+=8;
      offset+=8;
      repeat = 0;
    }
    host_curbmp += curstride;
    host_mskbmp += mskstride;
  }; /* y */
} /* RefreshMouse */

static void
PDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  PDD_Name(RefreshMouse)(state);
  updateDisplay();
}

#undef DISPLAYINFO
#undef HOSTDISPLAY
#undef DC
#undef HD


/*-----------------------------------------------------------------------------*/
void MouseMoved(ARMul_State *state, int nMouseX, int nMouseY) {
  int xdiff, ydiff;

  xdiff = -(oldMouseX - nMouseX);
  ydiff = oldMouseY - nMouseY;

#ifdef DEBUG_MOUSEMOVEMENT
  dbug_kbd("MouseMoved: xdiff = %d  ydiff = %d\n",
           xdiff, ydiff);
#endif

  if (xdiff > 63)
    xdiff=63;
  if (xdiff < -63)
    xdiff=-63;

  if (ydiff>63)
    ydiff=63;
  if (ydiff<-63)
    ydiff=-63;

  oldMouseX = nMouseX;
  oldMouseY = nMouseY;

  KBD.MouseXCount = xdiff & 127;
  KBD.MouseYCount = ydiff & 127;

#ifdef DEBUG_MOUSEMOVEMENT
  dbug_kbd("MouseMoved: generated counts %d,%d xdiff=%d ydifff=%d\n",KBD.MouseXCount,KBD.MouseYCount,xdiff,ydiff);
#endif
} /* MouseMoved */


/*-----------------------------------------------------------------------------*/
void ProcessKey(ARMul_State *state, int nVirtKey, int lKeyData, int nKeyStat) {
  const vk_to_arch_key *ktak;
  for (ktak = vk_to_arch_key_map; ktak->sym; ktak++) {
    if (ktak->sym == nVirtKey) {
      keyboard_key_changed(&KBD, ktak->kid, nKeyStat == 1);
      return;
    }
  }

  warn_kbd("ProcessKey: unknown key: keysym=%u\n", nVirtKey);
} /* ProcessKey */


/*-----------------------------------------------------------------------------*/
static bool
DisplayDev_Select(ARMul_State *state, ArcemConfig_DisplayDriver driver)
{
  if (driver == DisplayDriver_Palettised)
    return DisplayDev_Set(state,&PDD_DisplayDev);
  else
    return DisplayDev_Set(state,&SDD_DisplayDev);
}

/*-----------------------------------------------------------------------------*/
bool
DisplayDev_Init(ARMul_State *state)
{
  /* Setup display and cursor bitmaps */
  createWindow(state, MonitorWidth, MonitorHeight);
  return DisplayDev_Select(state,CONFIG.eDisplayDriver);
}

/*-----------------------------------------------------------------------------*/
int
Kbd_PollHostKbd(ARMul_State *state)
{
  /* Keyboard and mouse input is handled in WndProc */
  if (CONFIG.eDisplayDriver != requestedDriver ||
      CONFIG.bAspectRatioCorrection != requestedAspect ||
      CONFIG.bUpscale != requestedUpscale) {
    /* TODO: Avoid needing to recreate the driver for aspect/scale changes */
    DisplayDev_Select(state,requestedDriver);
    CONFIG.eDisplayDriver = requestedDriver;
    CONFIG.bAspectRatioCorrection = requestedAspect;
    CONFIG.bUpscale = requestedUpscale;
  }
  return 0;
}

