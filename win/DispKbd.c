#include <string.h>



#include "../armdefs.h"
#include "armarc.h"
#include "../arch/dbugsys.h"
#include "../arch/keyboard.h"
#include "displaydev.h"
#include "win.h"
#include "KeyTable.h"
#include "../armemu.h"
#include "ControlPane.h"


#define MinimumWidth 512
#define MonitorWidth 1600
#define MonitorHeight 1200

extern void *dibbmp;
extern void *curbmp;



int rMouseX = 0;
int rMouseY = 0;
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
  return (SDD_Row)dibbmp + ((MonitorHeight-(row+1))*MonitorWidth+offset);
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count) { /* nothing */ };

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { (*row) += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix) { *(*row)++ = pix; }

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count) { while(count--) *(*row)++ = pix; }

void SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  if((width > MonitorWidth) || (height > MonitorHeight))
  {
    ControlPane_Error(EXIT_FAILURE,"Mode %dx%d too big\n",width,height);
  }
  HD.Width = width;
  HD.Height = height;
  HD.XScale = 1;
  HD.YScale = 1;
  /* Try and detect rectangular pixel modes */
  if((width >= height*2) && (height*2 <= MonitorHeight))
  {
    HD.YScale = 2;
    HD.Height *= 2;
  }
  else if((height >= width) && (width*2 <= MonitorWidth))
  {
    HD.XScale = 2;
    HD.Width *= 2;
  }
  /* Try and detect small screen resolutions */
  else if ((width < MinimumWidth) && (width * 2 <= MonitorWidth) && (height * 2 <= MonitorHeight))
  {
    HD.XScale  = 2;
    HD.YScale  = 2;
    HD.Width  *= 2;
    HD.Height *= 2;
  }
  resizeWindow(HD.Width,HD.Height);
  /* Screen is expected to be cleared */
  memset(dibbmp,0,sizeof(SDD_HostColour)*MonitorWidth*MonitorHeight);
}

/*-----------------------------------------------------------------------------*/

void MouseMoved(ARMul_State *state, int nMouseX, int nMouseY) {
  int xdiff, ydiff;

  xdiff = -(oldMouseX - nMouseX);
  ydiff = oldMouseY - nMouseY;

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: xdiff = %d  ydiff = %d\n",
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
  fprintf(stderr,"MouseMoved: generated counts %d,%d xdiff=%d ydifff=%d\n",KBD.MouseXCount,KBD.MouseYCount,xdiff,ydiff);
#endif
}; /* MouseMoved */

/*-----------------------------------------------------------------------------*/
/* Refresh the mouse's image                                                    */
static void RefreshMouse(ARMul_State *state) {
  int x,y,offset, pix, repeat;
  int memptr;
  int HorizPos;
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
  rMouseHeight = Height;

  /* Cursor palette */
  cursorPal[0] = SDD_Name(Host_GetColour)(state,0);
  for(x=0; x<3; x++) {
    cursorPal[x+1] = SDD_Name(Host_GetColour)(state,VIDC.CursorPalette[x]);
  }

  offset=0;
  memptr=MEMC.Cinit*16;
  repeat=0;
  for(y=0;y<Height;y++) {
    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++) {
        pix = ((tmp[x/16]>>((x & 15)*2)) & 3);
        diboffs = rMouseX + x + (MonitorHeight - rMouseY - y - 1) * MonitorWidth;

        host_curbmp[x + (MonitorHeight - y - 1) * 32] =
                (pix || diboffs < 0) ?
                cursorPal[pix] : host_dibbmp[diboffs];
      }; /* x */
    } else return;
    if(++repeat == HD.YScale) {
      memptr+=8;
      offset+=8;
      repeat = 0;
    }
  }; /* y */
}; /* RefreshMouse */


/*-----------------------------------------------------------------------------*/
void ProcessKey(ARMul_State *state, int nVirtKey, int lKeyData, int nKeyStat) {
  const vk_to_arch_key *ktak;
  for (ktak = vk_to_arch_key_map; ktak->sym; ktak++) {
    if (ktak->sym == nVirtKey) {
      keyboard_key_changed(&KBD, ktak->kid, nKeyStat == 1);
      return;
    }
  }

  fprintf(stderr, "ProcessKey: unknown key: keysym=%u\n", nVirtKey);
}; /* ProcessKey */


/*-----------------------------------------------------------------------------*/
int
DisplayDev_Init(ARMul_State *state)
{
  /* Setup display and cursor bitmaps */
  createWindow(MonitorWidth, MonitorHeight, SDD_BitsPerPixel);
  return DisplayDev_Set(state,&SDD_DisplayDev);
}

/*-----------------------------------------------------------------------------*/
void
SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  RefreshMouse(state);
  updateDisplay();
}

/*-----------------------------------------------------------------------------*/
int
Kbd_PollHostKbd(ARMul_State *state)
{
  /* Keyboard and mouse input is handled in WndProc */
  return 0;
}

