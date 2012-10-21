/*
  X/pseudo.c

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Standard display device implementation for pseudocolour X11 displays

  TODO - This should probably be converted to use the palettised display device

*/

#include <limits.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>

#if defined(sun) && defined(__SVR4)
# include <X11/Sunkeysym.h>
#endif

#include "../armdefs.h"
#include "armarc.h"
#include "arch/keyboard.h"
#include "archio.h"
#include "../armemu.h"
#include "arch/displaydev.h"
#include "platform.h"
#include "ControlPane.h"

typedef char SDD_HostColour;
#define SDD_Name(x) pseudo_##x
static const int SDD_RowsAtOnce = 1;
typedef SDD_HostColour *SDD_Row;
#define SDD_DisplayDev pseudo_DisplayDev

static int UpdateStart=INT_MAX,UpdateEnd=-1;

static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col)
{
  /* We use a fixed palette which matches the standard 256 colour RISC OS one */
  int tint = (col & 0x3) + ((col & 0x30)>>4) + ((col & 0x300)>>8) + 1;
  return (tint/3)+(col&0xc)+((col&0xc0)>>2)+((col&0xc00)>>4);
}  

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset);

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count)
{
  UpdateStart = MIN(UpdateStart,*row-PD.ImageData);
  UpdateEnd = MAX(UpdateEnd,*row-PD.ImageData+count-1);
}

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { (*row) += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix) { *(*row)++ = pix; }

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count) { while(count--) *(*row)++ = pix; }

static void SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  return PD.ImageData+row*HD.Width+offset;
}

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  if (width > MaxVideoWidth || height > MaxVideoHeight) {
      ControlPane_Error(EXIT_FAILURE,"Resize_Window: new size (%d, %d) exceeds maximum (%d, %d)\n",
          width, height, MaxVideoWidth, MaxVideoHeight);
  }

  HD.XScale = 1;
  HD.YScale = 1;
  /* Try and detect rectangular pixel modes */
  if((width >= height*2) && (height*2 <= MaxVideoHeight))
  {
    HD.YScale = 2;
    height *= 2;
  }
  else if((height >= width) && (width*2 <= MaxVideoWidth))
  {
    HD.XScale = 2;
    width *= 2;
  }
  HD.Width = MIN(MaxVideoWidth,width + (VIDC_BORDER * 2));
  HD.Height = MIN(MaxVideoHeight,height + (VIDC_BORDER * 2));

  Resize_Window(state,HD.Width,HD.Height);

  UpdateStart = 0;
  UpdateEnd = HD.Width*HD.Height-1;
}

/* Refresh the mouses image                                                   */
static void RefreshMouse(ARMul_State *state) {
  int x, y, height, offset;
  int memptr, TransBit;
  char *ImgPtr, *TransPtr;

  offset   = 0;
  memptr   = MEMC.Cinit * 16;
  height   = (VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart) + 1;
  ImgPtr   = PD.CursorImageData;
  TransPtr = PD.ShapePixmapData;
  TransBit = 0;

  SDD_HostColour cursorPalette[3];
  for(x=0;x<3;x++)
  {
    cursorPalette[x] = SDD_Name(Host_GetColour)(state,VIDC.CursorPalette[x]);
  }

  for(y=0; y<height; y++,memptr+=8,offset+=8,TransPtr+=4) {
    if (offset < 512*1024) {
      ARMword tmp[2];

      tmp[0] = MEMC.PhysRam[memptr/4];
      tmp[1] = MEMC.PhysRam[memptr/4+1];

      for(x=0; x<32; x++,ImgPtr++,TransBit<<=1) {
        int idx = ((tmp[x / 16] >> ((x & 15) * 2)) & 3);
        *ImgPtr = cursorPalette[idx-1];
        if ((x & 7) == 0) {
          TransPtr[x / 8] = 0xff;
          TransBit = 1;
        }
        TransPtr[x / 8] &= idx ? ~TransBit : 0xff;
      } /* x */
    } else {
      return;
    }
  } /* y */

  if (PD.ShapeEnabled) {
    int DisplayHeight =  VIDC.Vert_DisplayEnd  - VIDC.Vert_DisplayStart;

    if(DisplayHeight > 0)
    {
      PD.shape_mask = XCreatePixmapFromBitmapData(PD.disp, PD.CursorPane,
                                                  PD.ShapePixmapData,
                                                  32, DisplayHeight, 0, 1, 1);
      /* Eek - a lot of this is copied from XEyes - unfortunatly the manual
         page for this call is somewhat lacking.  To quote its bugs entry:
        'This manual page needs a lot more work' */
      XShapeCombineMask(PD.disp,PD.CursorPane,ShapeBounding,0,0,PD.shape_mask,
                        ShapeSet);
      XFreePixmap(PD.disp,PD.shape_mask);
    }

  }; /* Shape enabled */
} /* RefreshMouse */

static void SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  if(UpdateEnd >= 0)
  {
    /* Work out the rectangle that covers this region */
    int startx = UpdateStart % HD.Width;
    int starty = UpdateStart / HD.Width;
    int endx = UpdateEnd % HD.Width;
    int endy = UpdateEnd / HD.Width;
    if(starty != endy)
    {
      startx = 0;
      endx = HD.Width-1;
    }
    XPutImage(PD.disp, PD.MainPane, PD.MainPaneGC, PD.DisplayImage,
              startx, starty, /* source pos. in image */
              startx, starty, /* Position on window */
              endx-startx+1,
              endy-starty+1);
    UpdateStart = INT_MAX;
    UpdateEnd = -1;
  }

  RefreshMouse(state);

  UpdateCursorPos(state,HD.XScale,HD.XOffset,HD.YScale,HD.YOffset);

  XPutImage(PD.disp, PD.CursorPane, PD.MainPaneGC, PD.CursorImage,
              0, 0,
              0, 0,
              32, ((VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart) - 1));
}

