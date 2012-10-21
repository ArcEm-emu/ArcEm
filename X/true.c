/*
  X/true.c

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Standard display device implementation for truecolour X11 displays

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

typedef unsigned int SDD_HostColour;
#define SDD_Name(x) true_##x
static const int SDD_RowsAtOnce = 1;
typedef struct {
  int x,y; /* Current image position */
} SDD_Row;
#define SDD_DisplayDev true_DisplayDev

static int UpdateMinX=INT_MAX,UpdateMaxX=-1;
static int UpdateMinY=INT_MAX,UpdateMaxY=-1;

static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col)
{
  return vidc_col_to_x_col(col);
}  

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  SDD_Row dhrow;
  dhrow.x = offset;
  dhrow.y = row;
  return dhrow;
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count)
{
  UpdateMinX = MIN(UpdateMinX,row->x);
  UpdateMaxX = MAX(UpdateMaxX,row->x+count-1);
  UpdateMinY = MIN(UpdateMinY,row->y);
  UpdateMaxY = MAX(UpdateMaxY,row->y);
}

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { row->x += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix)
{
  XPutPixel(PD.DisplayImage, row->x++, row->y, pix);
}

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count)
{
  while(count--)
  {
    XPutPixel(PD.DisplayImage, row->x++, row->y, pix);
  }
}

static void SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

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

  UpdateMinX=UpdateMinY=0;
  UpdateMaxX=HD.Width-1;
  UpdateMaxY=HD.Height-1;
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
        XPutPixel(PD.CursorImage, x, y, cursorPalette[idx-1]);
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
  if(UpdateMaxY >= 0)
  {
    int startx = UpdateMinX;
    int starty = UpdateMinY;
    int endx = UpdateMaxX;
    int endy = UpdateMaxY;
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
    UpdateMinX = UpdateMinY = INT_MAX;
    UpdateMaxX = UpdateMaxY = -1;
  }

  RefreshMouse(state);
  
  UpdateCursorPos(state,HD.XScale,HD.XOffset,HD.YScale,HD.YOffset);

  XPutImage(PD.disp, PD.CursorPane, PD.MainPaneGC, PD.CursorImage,
              0, 0,
              0, 0,
              32, ((VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart) - 1));
}

