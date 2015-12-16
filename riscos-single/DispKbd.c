/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info
   Hacked about with for new display driver interface by Jeffrey Lee, 2011 */
/* Display and keyboard interface for the Arc emulator */

#include <stdio.h>
#include <limits.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

#include "kernel.h"
#include "swis.h"

#define  ArcEmKey_GetKey 0x53340

#include "../armdefs.h"
#include "armarc.h"
#include "arch/keyboard.h"
#include "archio.h"
#include "hdc63463.h"
#include "../armemu.h"
#include "arch/displaydev.h"
#include "arch/ArcemConfig.h"
#include "../prof.h"

#include "ControlPane.h"

#define ENABLE_MENU


#ifdef ENABLE_MENU
static int enable_screenshots = 0;
static int enable_stats = 0;
static int do_screenshot = 0;
#ifdef PROFILE_ENABLED
static int enable_profile = 0;
#endif

static void GoMenu(void);
#endif

static void InitModeTable(void);

typedef struct {
  int Width;
  int Height;
  int XOffset;
  int YOffset;
  int XScale;
  int YScale;
} DisplayParams;

static void set_cursor_palette(uint16_t *pal);
static void UpdateCursorPos(ARMul_State *state,const DisplayParams *params);
static void Host_PollDisplay_Common(ARMul_State *state,const DisplayParams *params);

typedef struct {
  int w;
  int h;
  int aspect; /* Aspect ratio: 1 = wide pixels, 2 = square pixels, 4 = tall pixels */
  uint32_t depths; /* Bitmask of supported display depths */
  uint32_t modeflags_16bpp; /* Preferred modeflags & ncolour values for 16 & 32bpp */
  uint32_t ncolour_16bpp;
  uint32_t modeflags_32bpp;
  uint32_t ncolour_32bpp;
} HostMode;
static HostMode *ModeList;
static int NumModes;
static int colourdepths_available = 0;

static int current_hz;
static const HostMode *current_mode=NULL;
static int current_depth=-1;

static HostMode *SelectROScreenMode(int x, int y, int aspect, int depths, int *outxscale, int *outyscale);

#ifndef PROFILE_ENABLED /* Profiling code uses a nasty hack to estimate program size, which will only work if we're using the wimpslot for our heap */
const char * const __dynamic_da_name = "ArcEm Heap";
#endif

#define MODE_VAR_WIDTH 0
#define MODE_VAR_HEIGHT 1
#define MODE_VAR_BPL 2
#define MODE_VAR_ADDR 3
#define MODE_VAR_XEIG 4
#define MODE_VAR_YEIG 5
#define MODE_VAR_LOG2BPP 6

static const ARMword ModeVarsIn[] = {
 11, /* Width-1 */
 12, /* Height-1 */
 6, /* Bytes per line */
 148, /* Address */
 4, /* XEig */
 5, /* YEig */
 9, /* log2 BPP */
 -1,
};

static ARMword ModeVarsOut[7];

static int CursorXOffset=0; /* How many columns were skipped from the left edge of the cursor image */
static int CursorYOffset=0; /* How many rows were skipped from the top of the cursor image */

/* Lookup table for 16/32bpp display drivers - populated on mode change to avoid any SWI overheads in Host_GetColour */
static uint32_t sdd_palette[4096];

static int ChangeMode(const HostMode *mode,int depth)
{
  while(!(mode->depths & (1<<depth)) && ((1<<depth) < mode->depths))
    depth++;
  if((mode != current_mode) || (depth != current_depth))
  {
    /* Change mode */
    int block[10];
    block[0] = 1;
    block[1] = mode->w;
    block[2] = mode->h;
    block[3] = depth;
    block[4] = -1;
    switch(depth)
    {
      case 3:
        /* Request full 256 entry palette */
        block[5] = 0;
        block[6] = 128;
        block[7] = 3;
        block[8] = 255;
        block[9] = -1;
        break;
      case 4:
        /* Request correct pixel format */
        block[5] = 0;
        block[6] = mode->modeflags_16bpp;
        block[7] = 3;
        block[8] = mode->ncolour_16bpp;
        block[9] = -1;
        break;
      case 5:
        /* Request correct pixel format */
        block[5] = 0;
        block[6] = mode->modeflags_32bpp;
        block[7] = 3;
        block[8] = mode->ncolour_32bpp;
        block[9] = -1;
        break;
      default:
        block[5] = -1;
        break;
    }
    
    _kernel_oserror *err = _swix(OS_ScreenMode, _INR(0,1), 0, &block);
    if(err)
    {
      ControlPane_Error(EXIT_FAILURE,"Failed to change screen mode: Error %d %s\n",err->errnum,err->errmess);
    }
  
    /* Remove text cursor from real RO */
    _swi(OS_RemoveCursors,0);

    current_mode = mode;
    current_depth = depth;

    if(depth > 3)
    {
      /* Populate palette */
      int i;
      for(i=0;i<4096;i++)
      {
        int r = (i & 0xf);
        int g = (i & 0xf0) >> 4;
        int b = (i & 0xf00) >> 4;
        uint32_t col;
        if(hArcemConfig.bRedBlueSwap && (depth == 4))
        {
          col = (r<<24) | (g<<16) | (b<<8);
        }
        else
        {
          col = (r<<8) | (g<<16) | (b<<24);
        }
        col |= col<<4;
        _swix(ColourTrans_ReturnColourNumber,_IN(0)|_OUT(0),col,&sdd_palette[i]);
      }
    }
  }
  
  _swi(OS_ReadVduVariables,_INR(0,1),ModeVarsIn,ModeVarsOut);

  return depth;
}

/* ------------------------------------------------------------------ */

/* Standard display device, 16bpp */

#define SDD_HostColour uint16_t
#define SDD_Name(x) sdd16_##x
static const int SDD_RowsAtOnce = 1;
#define SDD_Row SDD_HostColour *
#define SDD_DisplayDev SDD16_DisplayDev


static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col)
{
  return sdd_palette[col & ~0x1000];
}  

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  return ((SDD_Row) (ModeVarsOut[MODE_VAR_ADDR] + ModeVarsOut[MODE_VAR_BPL]*row))+offset;
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count) { /* nothing */ };

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { (*row) += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix) { *(*row)++ = pix; }

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count) { while(count--) *(*row)++ = pix; }

static void
SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  current_hz = hz;

  /* Search the mode list for the best match */
  int aspect;
  if(width <= height)
    aspect = 1;
  else if(width >= height*2)
    aspect = 4;
  else
    aspect = 2;

  HostMode *mode = SelectROScreenMode(width,height,aspect,1<<4,&HD.XScale,&HD.YScale);
  ChangeMode(mode,4);
  HD.Width = ModeVarsOut[MODE_VAR_WIDTH]+1; /* Should match mode->w, mode->h, but use these just to make sure */
  HD.Height = ModeVarsOut[MODE_VAR_HEIGHT]+1;
  
  fprintf(stderr,"Emu mode %dx%d aspect %.1f mapped to real mode %dx%d aspect %.1f, with scale factors %dx%d\n",width,height,((float)aspect)/2.0f,mode->w,mode->h,((float)mode->aspect)/2.0f,HD.XScale,HD.YScale);

  /* Screen is expected to be cleared */
  _swi(OS_WriteC,_IN(0),12);
}

static void
SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  DisplayParams params;
  params.Width = HD.Width;
  params.Height = HD.Height;
  params.XOffset = HD.XOffset;
  params.YOffset = HD.YOffset;
  params.XScale = HD.XScale;
  params.YScale = HD.YScale;
  Host_PollDisplay_Common(state,&params);
}

#undef SDD_HostColour
#undef SDD_Name
#undef SDD_Row
#undef SDD_DisplayDev

/* ------------------------------------------------------------------ */

/* Standard display device, 32bpp */

#define SDD_HostColour uint32_t
#define SDD_Name(x) sdd32_##x
#define SDD_Row SDD_HostColour *
#define SDD_DisplayDev SDD32_DisplayDev


static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col)
{
  return sdd_palette[col & ~0x1000];
}  

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
  return ((SDD_Row) (ModeVarsOut[MODE_VAR_ADDR] + ModeVarsOut[MODE_VAR_BPL]*row))+offset;
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count) { /* nothing */ };

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row) { /* nothing */ };

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count) { (*row) += count; }

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix) { *(*row)++ = pix; }

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count) { while(count--) *(*row)++ = pix; }

static void
SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
  current_hz = hz;

  /* Search the mode list for the best match */
  int aspect;
  if(width <= height)
    aspect = 1;
  else if(width >= height*2)
    aspect = 4;
  else
    aspect = 2;

  HostMode *mode = SelectROScreenMode(width,height,aspect,1<<5,&HD.XScale,&HD.YScale);
  ChangeMode(mode,5);
  HD.Width = ModeVarsOut[MODE_VAR_WIDTH]+1; /* Should match mode->w, mode->h, but use these just to make sure */
  HD.Height = ModeVarsOut[MODE_VAR_HEIGHT]+1;
  
  fprintf(stderr,"Emu mode %dx%d aspect %.1f mapped to real mode %dx%d aspect %.1f, with scale factors %dx%d\n",width,height,((float)aspect)/2.0f,mode->w,mode->h,((float)mode->aspect)/2.0f,HD.XScale,HD.YScale);

  /* Screen is expected to be cleared */
  _swi(OS_WriteC,_IN(0),12);
}

static void
SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  DisplayParams params;
  params.Width = HD.Width;
  params.Height = HD.Height;
  params.XOffset = HD.XOffset;
  params.YOffset = HD.YOffset;
  params.XScale = HD.XScale;
  params.YScale = HD.YScale;
  Host_PollDisplay_Common(state,&params);
}

#undef SDD_HostColour
#undef SDD_Name
#undef SDD_Row
#undef SDD_DisplayDev

/* ------------------------------------------------------------------ */

/* Palettised display code */
#define PDD_Name(x) pdd_##x

static int BorderPalEntry; /* Border palette entry, or 256 if no spare entries */
static int FakeBorderPalEntry=-1; /* Palette entry currently in use for the fake border (i.e. closest colour to real border colour; for when BorderPalEntry == 256). -1 if dirty and needs redraw. */

typedef struct {
  ARMword *data;
  int offset;
} PDD_Row;

static void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz);

static void PDD_Name(Host_SetPaletteEntry)(ARMul_State *state,int i,unsigned int phys)
{
  char buf[5];
  buf[0] = i;
  buf[1] = 16;
  buf[2] = (phys & 0xf)*0x11;
  buf[3] = ((phys>>4) & 0xf)*0x11;
  buf[4] = ((phys>>8) & 0xf)*0x11;
  _swix(OS_Word,_INR(0,1),12,buf);

  if(i == FakeBorderPalEntry)
    FakeBorderPalEntry = -1;
}

static void PDD_Name(Host_SetBorderColour)(ARMul_State *state,unsigned int phys)
{
  char buf[5];
  /* Set real border */
  buf[0] = 0;
  buf[1] = 24;
  buf[2] = (phys & 0xf)*0x11;
  buf[3] = ((phys>>4) & 0xf)*0x11;
  buf[4] = ((phys>>8) & 0xf)*0x11;
  _swix(OS_Word,_INR(0,1),12,buf);
  /* Set border palette entry */
  if(BorderPalEntry != 256)
  {
    buf[0] = BorderPalEntry;
    buf[1] = 16;
    _swix(OS_Word,_INR(0,1),12,buf);
  }
  else
  {
    FakeBorderPalEntry = -1;
  }
}

static inline PDD_Row PDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset,int *alignment)
{
  PDD_Row drow;
  ARMword base = ModeVarsOut[MODE_VAR_ADDR] + ModeVarsOut[MODE_VAR_BPL]*row;
  offset = offset<<ModeVarsOut[MODE_VAR_LOG2BPP];
  base += offset>>3;
  drow.offset = (offset & 0x7) | ((base<<3) & 0x18); /* Just in case bytes per line isn't aligned */
  drow.data = (ARMword *) (base & ~0x3);
  *alignment = drow.offset;
  return drow;
}

static inline void PDD_Name(Host_EndRow)(ARMul_State *state,PDD_Row *row) { /* nothing */ };

static inline ARMword *PDD_Name(Host_BeginUpdate)(ARMul_State *state,PDD_Row *row,unsigned int count,int *outoffset)
{
  *outoffset = row->offset;
  return row->data;
}

static inline void PDD_Name(Host_EndUpdate)(ARMul_State *state,PDD_Row *row) { /* nothing */ };

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
  /* Quickest way is likely to be to OS_Plot */
  y = ModeVarsOut[MODE_VAR_HEIGHT]+1-(y+height);
  x = x<<ModeVarsOut[MODE_VAR_XEIG];
  y = y<<ModeVarsOut[MODE_VAR_YEIG];
  width = width<<ModeVarsOut[MODE_VAR_XEIG];
  height = height<<ModeVarsOut[MODE_VAR_YEIG];

  _swi(OS_Plot,_INR(0,2),4,x,y);
  _swi(OS_Plot,_INR(0,2),96+1,width-1,height-1);
}

#include "../arch/paldisplaydev.c"

void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz)
{
  current_hz = hz;

  /* Search the mode list for the best match */
  int aspect;
  if(width <= height)
    aspect = 1;
  else if(width >= height*2)
    aspect = 4;
  else
    aspect = 2;

  HostMode *mode = SelectROScreenMode(width,height,aspect,(0xf<<depth)&0xf,&HD.XScale,&HD.YScale);
  int realdepth = ChangeMode(mode,depth);
  
  HD.Width = ModeVarsOut[MODE_VAR_WIDTH]+1; /* Should match mode->w, mode->h, but use these just to make sure */
  HD.Height = ModeVarsOut[MODE_VAR_HEIGHT]+1;
  if(realdepth > depth)
  {
    /* We have enough palette entries to have a border */
    BorderPalEntry = 1<<(1<<depth);
  }
  else
  {
    /* Disable border entry */
    BorderPalEntry = 256;
    FakeBorderPalEntry = -1;
  }

  /* Calculate expansion params */
  if((realdepth == depth) && (HD.XScale == 1))
  {
    /* No expansion */
    HD.ExpandTable = NULL;
  }
  else
  {
    /* Expansion! */
    static ARMword expandtable[256];
    HD.ExpandFactor = 0;
    while((1<<HD.ExpandFactor) < HD.XScale)
      HD.ExpandFactor++;
    HD.ExpandFactor += (realdepth-depth);
    HD.ExpandTable = expandtable;
    unsigned int mul = 1;
    int i;
    for(i=0;i<HD.XScale;i++)
    {
      mul |= 1<<(i*(1<<realdepth));
    }
    GenExpandTable(HD.ExpandTable,1<<depth,HD.ExpandFactor,mul);
  }
  
  fprintf(stderr,"Emu mode %dx%dx%d aspect %.1f mapped to real mode %dx%dx%d aspect %.1f, with scale factors %dx%d\n",width,height,depth,((float)aspect)/2.0f,mode->w,mode->h,realdepth,((float)mode->aspect)/2.0f,HD.XScale,HD.YScale);

  /* Set correct graphics colour for border */
  _swi(ColourTrans_SetColour,_IN(0)|_INR(3,4),BorderPalEntry&255,0,0);

  /* Screen is expected to be cleared */
  PDD_Name(Host_DrawBorderRect)(state,0,0,HD.Width,HD.Height);
}

static void
PDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  DisplayParams params;
  params.Width = HD.Width;
  params.Height = HD.Height;
  params.XOffset = HD.XOffset;
  params.YOffset = HD.YOffset;
  params.XScale = HD.XScale;
  params.YScale = HD.YScale;
  Host_PollDisplay_Common(state,&params);
  if((BorderPalEntry == 256) && (FakeBorderPalEntry == -1))
  {
    /* Work out what colour to use for the border */
    _swi(ColourTrans_InvalidateCache,0);
    unsigned int pal = ((VIDC.BorderCol & 0xf)*0x1100) | ((VIDC.BorderCol&0xf0)*0x11000) | ((VIDC.BorderCol&0xf00)*0x110000);
    _swi(ColourTrans_ReturnColourNumber,_IN(0)|_OUT(0),pal,&FakeBorderPalEntry);
    _swi(ColourTrans_SetColour,_IN(0)|_INR(3,4),FakeBorderPalEntry,0,0);
    /* Now redraw */
    int Width = (VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
    int Height = (VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart);
    int xend = Width*params.XScale+params.XOffset;
    int yend = Height*params.YScale+params.YOffset;
    PDD_Name(Host_DrawBorderRect)(state,0,0,params.Width,params.YOffset);
    PDD_Name(Host_DrawBorderRect)(state,0,yend,params.Width,params.Height-yend);
    PDD_Name(Host_DrawBorderRect)(state,0,params.YOffset,params.XOffset,params.Height*params.YScale);
    PDD_Name(Host_DrawBorderRect)(state,xend,params.YOffset,params.Width-xend,params.Height*params.YScale);
  }
}

#undef DISPLAYINFO
#undef HOSTDISPLAY
#undef DC
#undef HD

/* ------------------------------------------------------------------ */

static void set_cursor_palette(uint16_t *pal)
{
  char buf[5];
  int c;

  buf[1] = 25;

  for(c = 0; c < 3; c++)
  {
    buf[0] = c+1;
    buf[2] = (pal[c] & 0xf)*0x11;
    buf[3] = ((pal[c]>>4) & 0xf)*0x11;
    buf[4] = ((pal[c]>>8) & 0xf)*0x11;
    _swix(OS_Word,_INR(0,1),12,buf);
  }
}

/*-----------------------------------------------------------------------------*/
/* Move the cursor image                                                      */
static void UpdateCursorPos(ARMul_State *state,const DisplayParams *params) {
  int internal_x, internal_y;
  char block[5];

  /* Calculate correct cursor position, relative to the display start */
  DisplayDev_GetCursorPos(state,&internal_x,&internal_y);
  /* Convert to our screen space */
  internal_x+=CursorXOffset;
  internal_y+=CursorYOffset;
  internal_x=internal_x*params->XScale+params->XOffset;
  internal_y=internal_y*params->YScale+params->YOffset;

  block[0]=5;
  {
    short x = internal_x << ModeVarsOut[MODE_VAR_XEIG];
    block[1] = x & 255;
    block[2] = x >> 8;
  }
  {
    short y = ((params->Height-1)-internal_y) << ModeVarsOut[MODE_VAR_YEIG];
    block[3] = y & 255;
    block[4] = y >> 8;
  }

  _swi(OS_Word, _INR(0,1), 21, &block);

}; /* UpdateCursorPos */

/* ------------------------------------------------------------------ */

/* Refresh the mouse's image                                                    */
static void RefreshMouse(ARMul_State *state,const DisplayParams *params) {
  int height;
  ARMword *pointer_data = MEMC.PhysRam + ((MEMC.Cinit * 16)/4);

  height = VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart;

  CursorYOffset = 0;
  if(height && (height <= 32) && (params->YScale >= 2))
  {
    /* Skip any blank rows at the start & end to enable better line doubling */
    while(height && !pointer_data[0] && !pointer_data[1])
    {
      height--;
      pointer_data += 2;
      CursorYOffset++;
    }
    while(height && !pointer_data[height*2-1] && !pointer_data[height*2-2])
    {
      height--;
    }
    
    if(height && (height <= 16))
    {
      /* line-double the cursor image */
      static ARMword double_data[2*32];
      int i;
      for(i=0;i<height;i++)
      {
        double_data[i*4+0] = double_data[i*4+2] = pointer_data[i*2];
        double_data[i*4+1] = double_data[i*4+3] = pointer_data[i*2+1];
      }
      height *= 2;
      pointer_data = double_data;
    }
  }
  
  CursorXOffset = 0;
  if(height && (height <= 32) && (params->XScale >= 2))
  {
    /* Double the width of the image; might not work too well */
    static ARMword double_data[2*32];
    int x,y;
    char *src = (char *) pointer_data;
    char *dest = (char *) double_data;
    /* RISC OS tends to store the image in the right half of the buffer, so shift the image left as far as possible to avoid losing any columns
       Note that we're only doing it 4 columns at a time here to keep the code simple */
    for(;CursorXOffset<16;CursorXOffset += 4)
    {
      for(y=0;y<height;y++)
        if(src[y*8])
          break;
      if(y!=height)
        break;
      src++;
    }
    for(y=0;y<height;y++)
    {
      for(x=0;x<4;x++)
      {
        char c = *src;
        *dest++ = ((c&0x3)*0x5) | (((c&0xc)>>2)*0x50);
        *dest++ = (((c&0x30)>>4)*0x5) | (((c&0xc0)>>6)*0x50);
        src++;
      }
      src += 4;
    }
    pointer_data = double_data;
  }

  {
    char block[10];

    block[0] = 0;
    block[1] = 2;
    block[2] = 8;
    block[3] = height;
    block[4] = 0;
    block[5] = 0;
    block[6] = ((int) pointer_data) & 0x000000FF;
    block[7] = (((int) pointer_data) & 0x0000FF00)>>8;
    block[8] = (((int) pointer_data) & 0x00FF0000)>>16;
    block[9] = (((int) pointer_data) & 0xFF000000)>>24;

    _swi(OS_Word,_INR(0,1), 21, &block);
  }

  _swi(OS_Byte, _INR(0,1), 106, 2+(1<<7));

  UpdateCursorPos(state,params);
  set_cursor_palette(VIDC.CursorPalette);
}; /* RefreshMouse */


#ifdef ENABLE_MENU
static void rbswap(void)
{
  ARMword *pix = (ARMword *) ModeVarsOut[MODE_VAR_ADDR];
  ARMword bytes = ModeVarsOut[MODE_VAR_BPL]*(ModeVarsOut[MODE_VAR_HEIGHT]+1);
  const ARMword mask = 0x1f001f;
  while(bytes>=4)
  {
    ARMword temp = *pix;
    ARMword red = temp & mask;
    ARMword green = temp & (mask<<5);
    ARMword blue = temp & (mask<<10);
    *pix++ = (red<<10) | green | (blue>>10);
    bytes -= 4;
  }
}
#endif

static void Host_PollDisplay_Common(ARMul_State *state,const DisplayParams *params)
{
  RefreshMouse(state,params);

#ifdef ENABLE_MENU
  if(enable_stats)
  {
    static clock_t oldtime;
    static ARMword oldcycles;
    static int fps;
    clock_t nowtime2 = clock();

    /* Simple game FPS counter - count the number of frames where Vinit has changed */
    static ARMword oldvinit;
    if(MEMC.Vinit != oldvinit)
    {
      fps++;
      oldvinit = MEMC.Vinit;
    }

    if((nowtime2-oldtime) > CLOCKS_PER_SEC)
    {
      if(ModeVarsOut[MODE_VAR_LOG2BPP] < 4)
      {
        /* Try and select sensible text colours
           Unfortunately ColourTrans doesn't always get it right, even though
           we invalidate the cache each time. */
        _swi(ColourTrans_InvalidateCache,0);
        _swi(ColourTrans_SetTextColour,_IN(0)|_IN(3),0xffffff00,0);
        _swi(ColourTrans_SetTextColour,_IN(0)|_IN(3),0,128);
      }
      
      const float scale = ((float)CLOCKS_PER_SEC)/1000000.0f;
      float mhz = scale*((float)(ARMul_Time-oldcycles))/((float)(nowtime2-oldtime));
      printf("\x1e%.2fMHz %dx%d %dHz %dbpp %d:%d %dfps   \n",mhz,(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart,current_hz,1<<((VIDC.ControlReg>>2)&3),params->XScale,params->YScale,fps);
      oldcycles = ARMul_Time;
      oldtime = nowtime2;
      fps = 0;
    }
  }
  if(do_screenshot)
  {
    do_screenshot = 0;
    char name[32];
    static int count = 0;
    sprintf(name,"<ArcEm$Dir>.^.screen%04d",count++);
    if(hArcemConfig.bRedBlueSwap && (ModeVarsOut[MODE_VAR_LOG2BPP] == 4))
    {
      /* Unswap red/blue so the sprite is correct */
      rbswap();
    }
    _swi(OS_SpriteOp,_IN(0)|_INR(2,3),2,name,1);
    if(hArcemConfig.bRedBlueSwap && (ModeVarsOut[MODE_VAR_LOG2BPP] == 4))
    {
      /* Reswap red/blue :( */
      rbswap();
    }
    /* Reset EmuRate since the above may have taken a looong time */
    EmuRate_Reset(&statestr);
  }
#endif 
}

/*-----------------------------------------------------------------------------*/

static void leds_changed(uint8_t leds)
{
  int newstate = 0;
  if(!(leds & KBD_LED_CAPSLOCK))
    newstate |= 1<<4;
  if(!(leds & KBD_LED_NUMLOCK))
    newstate |= 1<<2;
  if(leds & KBD_LED_SCROLLLOCK)
    newstate |= 1<<1;
  _swix(OS_Byte,_INR(0,2),202,newstate,0xe9);
  _swix(OS_Byte,_IN(0),118);
}

/*-----------------------------------------------------------------------------*/

static int old_escape,old_break;

static void restorebreak(void)
{
  /* Restore escape & break actions */
  _swix(OS_Byte,_INR(0,2),200,old_escape,0);
  _swix(OS_Byte,_INR(0,2),247,old_break,0);
}

/*-----------------------------------------------------------------------------*/

static const DisplayDev *displays[2] = {&PDD_DisplayDev,&SDD16_DisplayDev};

int
DisplayDev_Init(ARMul_State *state)
{
  KBD.leds_changed = leds_changed;
  leds_changed(KBD.Leds);

  InitModeTable();
  if(!(colourdepths_available & (1<<4)))
    displays[DisplayDriver_Standard] = &SDD32_DisplayDev;

  /* Disable escape & break. Have to use alt-break, or ArcEm_Shutdown, to quit the emulator. */
  _swi(OS_Byte,_INR(0,2)|_OUT(1),200,1,0xfe,&old_escape);
  _swi(OS_Byte,_INR(0,2)|_OUT(1),247,0xaa,0,&old_break);
  atexit(restorebreak);

  return DisplayDev_Set(state,displays[hArcemConfig.eDisplayDriver]);
} /* DisplayDev_Init */


/*-----------------------------------------------------------------------------*/
static void ProcessKey(ARMul_State *state, int key, int transition) {
      /* Now add it to the buffer */
      KBD.Buffer[KBD.BuffOcc].KeyColToSend=key % 16;
      KBD.Buffer[KBD.BuffOcc].KeyRowToSend=key / 16;
      KBD.Buffer[KBD.BuffOcc].KeyUpNDown=transition ? 0 : 1;
      KBD.BuffOcc++;
#ifdef DEBUG_KBD
      fprintf(stderr,"ProcessKey: Got Col,Row=%d,%d UpNDown=%d BuffOcc=%d\n",
              KBD.Buffer[KBD.BuffOcc].KeyColToSend,
               KBD.Buffer[KBD.BuffOcc].KeyRowToSend,
               KBD.Buffer[KBD.BuffOcc].KeyUpNDown,
              KBD.BuffOcc);
#endif
}; /* ProcessKey */


/*-----------------------------------------------------------------------------*/
/* Called on an X motion event */
static void MouseMoved(ARMul_State *state, int mousex, int mousey/*,XMotionEvent *xmotion*/) {
  int xdiff,ydiff;

  /* We are now only using differences from the reference position */
  int xmid = (ModeVarsOut[MODE_VAR_WIDTH]+1)>>1;
  int ymid = (ModeVarsOut[MODE_VAR_HEIGHT]+1)>>1;
  if ((mousex==xmid) && (mousey==ymid)) return;

  {
    char block[5];
    int x=xmid;
    int y=ymid;

    block[0]=3;
    block[1]=x & 255;
    block[2]=(x>>8) & 255;
    block[3]=y & 255;
    block[4]=(y>>8) & 255;

    _swi(OS_Word, _INR(0,1), 21, &block);
  }

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: CursorStart=%d xmotion->x=%d\n",
          VIDC.Horiz_CursorStart,mousex);
#endif
  xdiff=mousex-xmid;
  if (KBD.MouseXCount!=0) {
    if (KBD.MouseXCount & 64) {
      signed char tmpC;
      int tmpI;
      tmpC=KBD.MouseXCount | 128;
      tmpI=(signed int)tmpC;
      xdiff+=tmpI;
    } else {
      xdiff+=KBD.MouseXCount;
    };
  };

  if (xdiff>63) xdiff=63;
  if (xdiff<-63) xdiff=-63;

  ydiff=mousey-ymid;
  if (KBD.MouseYCount & 64) {
    signed char tmpC;
    tmpC=KBD.MouseYCount | 128; /* Sign extend */
    ydiff+=tmpC;
  } else {
    ydiff+=KBD.MouseYCount;
  };
  if (ydiff>63) ydiff=63;
  if (ydiff<-63) ydiff=-63;

  KBD.MouseXCount=xdiff & 127;
  KBD.MouseYCount=ydiff & 127;

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: generated counts %d,%d xdiff=%d ydifff=%d\n",KBD.MouseXCount,KBD.MouseYCount,xdiff,ydiff);
#endif
}; /* MouseMoved */

/*----------------------------------------------------------------------------*/
int
Kbd_PollHostKbd(ARMul_State *state)
{
  /* Keyboard handling */
  {
    int key;
    int transition;

    if (_swi (ArcEmKey_GetKey, _RETURN(0)|_OUTR(1,2), &transition, &key))
    {
      //printf("Processing key %d, transition %d\n",key, transition);
      ProcessKey(state, key, transition);
#ifdef ENABLE_MENU
      static int key1_down = 0;
      static int key2_down = 0;
      static int both_down = 0;
      if(key == hArcemConfig.iTweakMenuKey1)
        key1_down = transition;
      else if(key == hArcemConfig.iTweakMenuKey2)
        key2_down = transition;
      if(key1_down & key2_down)
        both_down = 1;
      if(both_down && !key1_down && !key2_down)
      {
        both_down = 0;
        GoMenu();
      }
      else if(enable_screenshots && (key == 13) && transition)
      {
        do_screenshot = 1;
      }
#endif
    }
  }

  /* Mouse handling */
  {
    int mousex;
    int mousey;
    _swi(OS_Mouse, _OUTR(0,1), &mousex, &mousey);

    MouseMoved(state, mousex, mousey);
  }

  return 0;
} /* Kbd_PollHostKbd */

/*-----------------------------------------------------------------------------*/

static void InitModeTable(void)
{
  int *mode_list;
  int count;
  int size;
  _swi(OS_ScreenMode,_IN(0)|_IN(2)|_INR(6,7)|_OUT(7),2,0,0,0,&size);
  size = -size;
  mode_list = (int *) malloc(size);
  if(!mode_list)
  {
    ControlPane_Error(EXIT_FAILURE,"Failed to get memory for mode list\n");
  }
  _swi(OS_ScreenMode,_IN(0)|_IN(2)|_INR(6,7)|_OUT(2),2,0,mode_list,size,&count);
  count = -count;
  ModeList = (HostMode *) malloc(sizeof(HostMode)*count);
  if(!ModeList)
  {
    ControlPane_Error(EXIT_FAILURE,"Failed to get memory for mode list\n");
  }
  memset(ModeList,0,sizeof(HostMode)*count);
  NumModes = 0;
  int *mode = mode_list;
  /* Convert the OS mode list into one in our own format */
  while(count--)
  {
    /* Check block format */
    if (((mode[1] & 0xff) != 1) && ((mode[1] & 0xff) != 3))
      goto next;
    /* Too small? */
    if((mode[2] < hArcemConfig.iMinResX) || (mode[3] < hArcemConfig.iMinResY))
      goto next;
    /* Determine pixel format */
    uint32_t ncolour,modeflags,log2bpp;
    if ((mode[1] & 0xff) == 1)
    {
      log2bpp = mode[4];
      ncolour = (1<<(1<<log2bpp))-1;
      modeflags = (log2bpp==3?128:0);
    }
    else
    {
      ncolour = mode[4];
      modeflags = mode[5];
      log2bpp = mode[6];
    }
    /* Discard non-RGB or other tricky formats */
    if ((ncolour == 63) || (log2bpp > 5) || (modeflags & (3<<12)))
      goto next;
    /* Low colour not allowed? */
    if(hArcemConfig.bNoLowColour && (log2bpp < 3))
      goto next;
    /* Not exact scale for an LCD? */
    if(hArcemConfig.iLCDResX)
    {
      /* Simply too big? */
      if((hArcemConfig.iLCDResX < mode[2]) || (hArcemConfig.iLCDResY < mode[3]))
        goto next;
      /* Assume the monitor will scale it up while maintaining the aspect ratio
         Therefore, work out how much it can scale it before it reaches an edge, and check that value */
      float xscale = ((float)hArcemConfig.iLCDResX)/mode[2];
      float yscale = ((float)hArcemConfig.iLCDResY)/mode[3];
      xscale = MIN(xscale,yscale);
      if(floor(xscale) != xscale)
        goto next;
    }

    colourdepths_available |= 1<<log2bpp;

    /* Already got this entry? (i.e. due to multiple framerates) */
    int i;
    for(i=NumModes-1;i>=0;i--)
      if((ModeList[i].w == mode[2]) && (ModeList[i].h == mode[3]))
      {
        /* If we get multiple modes for the same BPP, try and pick the best pixel format */
        if (ModeList[i].depths & (1<<log2bpp))
        {
          if (log2bpp == 4)
          {
            /* Prefer non-alpha modes if possible - might lighten the load on the hardware */
            if ((ModeList[i].modeflags_16bpp & (8<<12)) && !(modeflags & (8<<12)))
            {
              ModeList[i].modeflags_16bpp = modeflags;
              ModeList[i].ncolour_16bpp = ncolour;
            }
            /* Prefer 4K colour mode if possible, for best colour reproduction */
            else if (((ModeList[i].modeflags_16bpp & (8<<12)) == (modeflags & (8<<12))) && (ncolour == 4095))
            {
              ModeList[i].modeflags_16bpp = modeflags;
              ModeList[i].ncolour_16bpp = ncolour;
            }
          }
          else if (log2bpp == 5)
          {
            /* Prefer non-alpha modes if possible - might lighten the load on the hardware */
            if ((ModeList[i].modeflags_32bpp & (8<<12)) && !(modeflags & (8<<12)))
            {
              ModeList[i].modeflags_32bpp = modeflags;
              ModeList[i].ncolour_32bpp = ncolour;
            }
          }
        }
        else
        {
          ModeList[i].depths |= 1<<log2bpp;
          if(log2bpp == 4)
          {
            ModeList[i].modeflags_16bpp = modeflags;
            ModeList[i].ncolour_16bpp = ncolour;
          }
          else if(log2bpp == 5)
          {
            ModeList[i].modeflags_32bpp = modeflags;
            ModeList[i].ncolour_32bpp = ncolour;
          }
        }
        goto next;
      }
    /* Add it to our list */
    ModeList[NumModes].w = mode[2];
    ModeList[NumModes].h = mode[3];
    ModeList[NumModes].depths = 1<<log2bpp;
    if(mode[2] <= mode[3])
      ModeList[NumModes].aspect = 1;
    else if(mode[2] >= mode[3]*2)
      ModeList[NumModes].aspect = 4;
    else
      ModeList[NumModes].aspect = 2;
    if(log2bpp == 4)
    {
      ModeList[NumModes].modeflags_16bpp = modeflags;
      ModeList[NumModes].ncolour_16bpp = ncolour;
    }
    else if(log2bpp == 5)
    {
      ModeList[NumModes].modeflags_32bpp = modeflags;
      ModeList[NumModes].ncolour_32bpp = ncolour;
    }
    fprintf(stderr,"Added mode %dx%d aspect %.1f\n",ModeList[NumModes].w,ModeList[NumModes].h,((float)ModeList[NumModes].aspect)/2.0f);
    NumModes++;

  next:
    mode += mode[0]/4;
  }
  free(mode_list);
}

static float ComputeFit(HostMode *mode,int x,int y,int aspect,int *outxscale,int *outyscale)
{
  /* Work out how best to fit the given screen into the given mode */
  int xscale=1;
  int yscale=1;

  if(hArcemConfig.bAspectRatioCorrection)
  {
    /* Use aspect ratios to work out right scale factors */
    if(aspect > mode->aspect)
    {
      /* Emulator pixels are taller, apply Y scaling */
      yscale = aspect/mode->aspect;
    }
    else if(aspect < mode->aspect)
    {
      /* Emulator pixels are wider, apply X scaling */
      xscale = mode->aspect/aspect;
      if(xscale > 2)
      {
        return -1.0f; /* Too much X scaling */
      }
    }
  }

  if((x*xscale > mode->w) || (y*yscale > mode->h))
    return -1.0f; /* Mode not big enough */

  /* Apply global 2* scaling if possible */
  if((x*xscale*2 <= mode->w) && (y*yscale*2 <= mode->h) && (xscale < 2) && (hArcemConfig.bUpscale))
  {
    xscale*=2;
    yscale*=2;
  }

  *outxscale = xscale;
  *outyscale = yscale;

  /* Work out the proportion of the screen we'll fill */
  return ((float)(x*xscale*y*yscale))/(mode->w*mode->h);
}

static float ScaleCost(int xscale,int yscale)
{
  /* Estimate how much extra processing this scale factor causes */
  return (((((float)yscale)-1.0f)*1.5f)+1.0f)*xscale; /* Y scaling (probably) has a higher cost than X scaling */
}

static HostMode *SelectROScreenMode(int x, int y, int aspect, int depths, int *outxscale,int *outyscale)
{
  HostMode *bestmode=NULL;
  int bestxscale=1,bestyscale=1;
  float bestscore=0.0f;
  int i;
  for(i=0;i<NumModes;i++)
  {
    if(!(ModeList[i].depths & depths))
      continue;
    int xscale=1,yscale=1;
    float score = ComputeFit(&ModeList[i],x,y,aspect,&xscale,&yscale);
    if((score > bestscore) || ((score == bestscore) && (ScaleCost(xscale,yscale) < ScaleCost(bestxscale,bestyscale))))
    {
      bestmode = &ModeList[i];
      bestxscale = xscale;
      bestyscale = yscale;
      bestscore = score;
    }
  }
  if(!bestmode)
  {
    ControlPane_Error(EXIT_FAILURE,"Failed to find suitable screen mode for %dx%d, aspect %.1f, depths %x\n",x,y,((float)aspect)/2.0f,depths);
  }
  *outxscale = bestxscale;
  *outyscale = bestyscale;
  return bestmode;
}

#ifdef ENABLE_MENU
typedef struct {
  const char label;
  const char *name;
  const char **values;
  void *val;
  size_t valsz;
} menu_item;

static const char *values_bool[] = {"Off","On",NULL};
static const char *values_display[] = {"Palettised","Standard",NULL};
static const char *values_skip[] = {"0","1","2","3","4","5","6","7","8","9","10",NULL};

#define XX(X) &X,sizeof(X)

static const menu_item items[] =
{
  {'1',"Display driver",values_display,XX(hArcemConfig.eDisplayDriver)},
  {'2',"Red/blue swap 16bpp output",values_bool,XX(hArcemConfig.bRedBlueSwap)},
  {'3',"Display auto UpdateFlags",values_bool,XX(DisplayDev_AutoUpdateFlags)},
  {'4',"Display uses UpdateFlags",values_bool,XX(DisplayDev_UseUpdateFlags)},
  {'5',"Display frameskip",values_skip,XX(DisplayDev_FrameSkip)},
  {'6',"Aspect ratio correction",values_bool,XX(hArcemConfig.bAspectRatioCorrection)},
  {'7',"2X upscaling",values_bool,XX(hArcemConfig.bUpscale)},
  {'8',"Take screenshots on Print Screen",values_bool,XX(enable_screenshots)},
  {'9',"Show stats",values_bool,XX(enable_stats)},
#ifdef PROFILE_ENABLED
  {'P',"Profiling",values_bool,XX(enable_profile)},
#endif
  {'R',"Resume",NULL,NULL},
  {'Q',"Quit",NULL,NULL},
};

#define ITEM_MAX (sizeof(items)/sizeof(items[0]))
#define ITEM_QUIT (ITEM_MAX-1)
#define ITEM_RESUME (ITEM_MAX-2)

static uint32_t readval(int i)
{
  uint8_t *val = (uint8_t *) items[i].val;
  size_t j;
  uint32_t temp=0;
  for(j=0;j<items[i].valsz;j++)
  {
    temp |= (val[j])<<(j<<3);
  }
  return temp;
}

static void writeval(int i,uint32_t temp)
{
  uint8_t *val = (uint8_t *) items[i].val;
  size_t j;
  for(j=0;j<items[i].valsz;j++)
  {
    val[j] = temp>>(j<<3);
  }
}

static void DrawMenu(void)
{
  _swi(OS_WriteC,_IN(0),12);
  printf("ArcEm tweak menu\n\n");
  int i;
  for(i=0;i<ITEM_MAX;i++)
  {
    if(items[i].values)
      printf("%c. [%s] %s\n",items[i].label,items[i].values[readval(i)],items[i].name);
    else
      printf("%c. %s\n",items[i].label,items[i].name);
  }
}

static void GoMenu(void)
{
#ifdef PROFILE_ENABLED
  /* Stop here */
  Prof_EndFunc(Keyboard_Poll);
  if(enable_profile)
  {
    /* And dump */
    Prof_Dump(stderr);
  }
#endif
  /* Switch to a known-good screen mode. Assume mode 28 available. */
  _swix(OS_ScreenMode,_INR(0,1),0,28);
  current_mode = NULL;
  _swi(OS_RemoveCursors,0);
  /* Flush input buffer */
  _swi(OS_Byte,_INR(0,1),15,1);
  do {
    DrawMenu();
    unsigned int c = toupper(_swi(OS_ReadC,_RETURN(0)));
    unsigned int i;
    for(i=0;i<ITEM_MAX;i++)
      if(c == items[i].label)
        break;
    if(i==ITEM_QUIT)
    {
      exit(0);
    }
    else if(i==ITEM_RESUME)
    {
      break;
    }
    else if(i<ITEM_MAX)
    {
      uint32_t val = readval(i);
      val++;
      if(!items[i].values[val])
        val = 0;
      writeval(i,val);
      if(items[i].val == &DisplayDev_AutoUpdateFlags)
      {
        /* Make sure these are sane */
        DisplayDev_UseUpdateFlags = 1;
        DisplayDev_FrameSkip = 0;
      }
    }
  } while(1);
  /* (re)start display device. Even if we haven't changed anything, this is needed to force the screen to be redrawn (and the mode to be reset) */
  if(DisplayDev_Set(&statestr,displays[hArcemConfig.eDisplayDriver]))
  {
    ControlPane_Error(EXIT_FAILURE,"Failed to reinitialise display\n");
  }
  /* Ensure DisplayDev_UseUpdateFlags is on for SDD */
  if(hArcemConfig.eDisplayDriver == DisplayDriver_Standard)
    DisplayDev_UseUpdateFlags = 1;
  /* Rebuild fastmap for DisplayDev_UseUpdateFlags changes */
  ARMul_RebuildFastMap(&statestr);
  /* Gobble any keyboard input */
  while(_swi (ArcEmKey_GetKey, _RETURN(0))) {};
  /* Reset EmuRate */
  EmuRate_Reset(&statestr);
#ifdef PROFILE_ENABLED
  if(enable_profile)
  {
    /* Start profiling */
    Prof_Reset();
  }
  Prof_BeginFunc(Keyboard_Poll);
#endif
}
#endif
