/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */
/* Display and keyboard interface for the Arc emulator */

/* Now does TrueColor and PseudoColor X modes; but doesnt handle palette
   changes properly in TrueColor - in particular it doesn't force a redraw if
   the palette is changed which it really needs to */

#define KEYREENABLEDELAY 1000
/*#define DEBUG_VIDCREGS */
/*#define DEBUG_KBD */
/*#define DEBUG_MOUSEMOVEMENT */

/* NOTE: Can't use ARMul's refresh function because it has a small limit on the
   time delay from posting the event to it executing */
/* It's actually decremented once every POLLGAP - that is called with the ARMul
   scheduler */
#define AUTOREFRESHPOLL 2500

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/shape.h>

#if defined(sun) && defined(__SVR4)
# include <X11/Sunkeysym.h>
#endif

#include "armdefs.h"
#include "arch/armarc.h"
#include "arch/keyboard.h"
#include "arch/DispKbd.h"
#include "arch/archio.h"
#include "arch/hdc63463.h"
#ifdef SOUND_SUPPORT
#include "arch/sound.h"
#endif

#include "ControlPane.h"
#include "platform.h"

/* A sensible set of defaults for the start window, the OS
   will call the VIDC and push this smaller or bigger later. */
#define InitialVideoWidth 640
#define InitialVideoHeight 480

/* An upper limit on how big to support monitor size, used for
   allocating a scanline buffer and bounds checking. It's much
   more than a VIDC1 can handle, and should be pushing the RPC/A7000
   VIDC too, if we ever get around to supporting that. */
#define MaxVideoWidth 2048
#define MaxVideoHeight 1536

/* The size of the border surrounding the video data. */
#define VIDC_BORDER 10

#define ControlHeight 30

/* Three colourmap entries are nicked out of the 256 needed by the video
 * data for the cursor's colours, and one more for the border.  This may
 * muck up a nice image utilising all 256 colours but what else can one
 * do apart from pick the closest matching colours from the video's
 * palette. */
#define BORDER_COLOURMAP_ENTRY 249
#define CURSORCOLBASE 250

#define DC DISPLAYCONTROL

/* 0 <= x < 0x10. */
#define MULT_BY_0x1111(x) \
    x |= x << 4; \
    x |= x << 8

#define IF_DIFF_THEN_ASSIGN_AND_SET_FLAG(a, b, f) \
    if ((a) != (b)) { \
        (a) = (b); \
        (f) = TRUE; \
    }

static void set_video_4bpp_colourmap(void);
static void set_video_8bpp_colourmap(void);
static void set_border_colourmap(void);
static void set_cursor_colourmap(void);

static void set_video_4bpp_pixelmap(void);
static void set_video_8bpp_pixelmap(void);
static void set_border_pixelmap(void);
static void set_cursor_pixelmap(void);

static void Resize_Window(void);

static int (*prev_x_error_handler)(Display *, XErrorEvent *);

static struct {
    const char *name;
    KeySym keysym;
} mouse_key = {
    "KP_Add",
    XK_KP_Add
};

/* Structure to hold most of the X windows values, shared with ControlPane.c */
struct host_display HD;

/* One temp scanline buffer of MaxVideoWidth wide */
static unsigned char ScanLineBuffer[MaxVideoWidth];



/* ------------------------------------------------------------------ */

typedef struct {
    KeySym sym;
    arch_key_id kid;
} keysym_to_arch_key;

#define X(sym, kid) { XK_ ## sym, ARCH_KEY_ ## kid },
#if defined(sun) && defined(__SVR4)
# define SUNX(sym, kid) { SunXK_ ## sym, ARCH_KEY_ ## kid },
#endif
static const keysym_to_arch_key keysym_to_arch_key_map[] = {
    X(0, 0)
    X(1, 1)
    X(2, 2)
    X(3, 3)
    X(4, 4)
    X(5, 5)
    X(6, 6)
    X(7, 7)
    X(8, 8)
    X(9, 9)
    X(Alt_L, alt_l)
    X(Alt_R, alt_r)
    X(BackSpace, backspace)
    X(Break, break)
    X(Caps_Lock, caps_lock)
    X(Control_L, control_l)
    X(Control_R, control_r)
    X(Delete, delete)
    X(Down, down)
    X(End, copy)
    X(Escape, escape)
    X(F1, f1)
    X(F10, f10)
    X(F11, f11)
    X(F12, f12)
    X(F2, f2)
    X(F3, f3)
    X(F4, f4)
    X(F5, f5)
    X(F6, f6)
    X(F7, f7)
    X(F8, f8)
    X(F9, f9)
    X(Home, home)
    X(Insert, insert)
    X(KP_0, kp_0)
    X(KP_1, kp_1)
    X(KP_2, kp_2)
    X(KP_3, kp_3)
    X(KP_4, kp_4)
    X(KP_5, kp_5)
    X(KP_6, kp_6)
    X(KP_7, kp_7)
    X(KP_8, kp_8)
    X(KP_9, kp_9)
    X(KP_Add, kp_plus)
    X(KP_Decimal, kp_decimal)
    X(KP_Divide, kp_slash)
    X(KP_Enter, kp_enter)
    /* X doesn't define a # on the keypad - so we use KP_F1 - but most
     * keypads don't have that either. */
    X(KP_F1, kp_hash)
    X(KP_Multiply, kp_star)
    X(KP_Subtract, kp_minus)
    X(Left, left)
    X(Num_Lock, num_lock)
    /* For some screwy reason these seem to be missing in X11R5. */
#ifdef XK_Page_Up
    X(Page_Up, page_up)
#endif
#ifdef XK_Page_Down
    X(Page_Down, page_down)
#endif
    X(Pause, break)
    X(Print, print)
    X(Return, return)
    X(Right, right)
    X(Scroll_Lock, scroll_lock)
    X(Shift_L, shift_l)
    X(Shift_R, shift_r)
    X(Tab, tab)
    X(Up, up)
    X(a, a)
    X(apostrophe, apostrophe)
    X(asciitilde, grave)
    X(b, b)
    X(backslash, backslash)
    X(bar, backslash)
    X(braceleft, bracket_l)
    X(braceleft, bracket_r)
    X(bracketleft, bracket_l)
    X(bracketright, bracket_r)
    X(c, c)
    X(colon, semicolon)
    X(comma, comma)
    X(currency, sterling)
    X(d, d)
    X(e, e)
    X(equal, equal)
    X(f, f)
    X(g, g)
    X(grave, grave)
    X(greater, period)
    X(h, h)
    X(i, i)
    X(j, j)
    X(k, k)
    X(l, l)
    X(less, comma)
    X(m, m)
    X(minus, minus)
    X(n, n)
    X(o, o)
    X(p, p)
    X(period, period)
    X(plus, equal)
    X(q, q)
    X(question, slash)
    X(quotedbl, apostrophe)
    X(r, r)
    X(s, s)
    X(semicolon, semicolon)
    X(slash, slash)
    X(space, space)
    X(sterling, sterling)
    X(t, t)
    X(u, u)
    X(underscore, minus)
    X(v, v)
    X(w, w)
    X(x, x)
    X(y, y)
    X(z, z)

#if defined(sun) && defined(__SVR4)
    /* Extra keycodes found on Sun hardware */
    X(KP_Insert, kp_0)  /* Labelled "0 (Insert)" */
    X(F33, kp_1)    /* Labelled "1 (End)" */
    X(F21, break)   /* Labelled "Pause (Break)" */
    X(F22, print)   /* Labelled "Print Screen (SysRq)" */
    X(F23, scroll_lock) /* Labelled "Scroll Lock" */
    X(F24, kp_minus)    /* Labelled "-" */
    X(F25, kp_slash)    /* Labelled "/" */
    X(F26, kp_star) /* Labelled "*" */
    SUNX(F36, f11)  /* Labelled "F11" */
    SUNX(F37, f12)  /* Labelled "F12" */
#endif

    { NoSymbol },
};
#undef X
#undef SUNX

/* ------------------------------------------------------------------ */

#ifdef UNUSED__STOP_COMPILER_WARNINGS
static unsigned AutoKey(ARMul_State *state);
#endif

/*----------------------------------------------------------------------------*/
/* From the GIMP Drawing Kit, in the GTK+ toolkit from GNOME                  */
/* Given a colour mask from a visual extract shift and bit precision values   */

static void
gdk_visual_decompose_mask (unsigned long mask, int *shift, int *prec)
{
  *shift = 0;
  *prec = 0;

  while (!(mask & 0x1))
    {
      (*shift)++;
      mask >>= 1;
    }

  while (mask & 0x1)
    {
      (*prec)++;
      mask >>= 1;
    }
}


/*----------------------------------------------------------------------------*/
/* Also borrowed from GDK (with a little rework).  Get the XPixel value (as
   passed to XPutPixel) based on 16 bit colour values                         */
static unsigned long get_pixelval(unsigned int red, unsigned int green, unsigned int blue) {
    return (((red   >> (16 - HD.red_prec))   << HD.red_shift)   +
            ((green >> (16 - HD.green_prec)) << HD.green_shift) +
            ((blue  >> (16 - HD.blue_prec))  << HD.blue_shift));

} /* get_pixval */


/*----------------------------------------------------------------------------*/
#ifdef UNUSED__STOP_COMPILER_WARNINGS
static unsigned AutoKey(ARMul_State *state) {
  /*fprintf(stderr,"AutoKey!\n"); */
  KBD.TimerIntHasHappened+=2;

  return 0;
}
#endif


/* ------------------------------------------------------------------ */

/* Refresh the mouses image                                                   */
static void RefreshMouse(ARMul_State *state) {
  int x, y, height, offset;
  int memptr, TransBit;
  char *ImgPtr, *TransPtr;

  offset   = 0;
  memptr   = MEMC.Cinit * 16;
  height   = (VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart) + 1;
  ImgPtr   = HD.CursorImageData;
  TransPtr = HD.ShapePixmapData;
  TransBit = 0;

  for(y=0; y<height; y++,memptr+=8,offset+=8,TransPtr+=4) {
    if (offset < 512*1024) {
      ARMword tmp[2];

      tmp[0] = MEMC.PhysRam[memptr/4];
      tmp[1] = MEMC.PhysRam[memptr/4+1];

      for(x=0; x<32; x++,ImgPtr++,TransBit<<=1) {
        if (HD.visInfo.class == PseudoColor) {
          *ImgPtr = CURSORCOLBASE + -1 + ((tmp[x / 16] >> ((x & 15) * 2)) & 3);
        } else {
          XPutPixel(HD.CursorImage, x, y, HD.cursorPixelMap[((tmp[x / 16] >> ((x & 15) * 2)) & 3)]);
        } /* True color */
        if ((x & 7) == 0) {
          TransPtr[x / 8] = 0xff;
          TransBit = 1;
        }
        TransPtr[x / 8] &= ((tmp[x / 16]>>((x & 15) * 2)) & 3) ? ~TransBit : 0xff;
      } /* x */
    } else {
      return;
    }
  } /* y */

  if (HD.ShapeEnabled) {
    int DisplayHeight =  VIDC.Vert_DisplayEnd  - VIDC.Vert_DisplayStart;

    if(DisplayHeight > 0)
    {
      HD.shape_mask = XCreatePixmapFromBitmapData(HD.disp, HD.CursorPane,
                                                  HD.ShapePixmapData,
                                                  32, DisplayHeight, 0, 1, 1);
      /* Eek - a lot of this is copied from XEyes - unfortunatly the manual
         page for this call is somewhat lacking.  To quote its bugs entry:
        'This manual page needs a lot more work' */
      XShapeCombineMask(HD.disp,HD.CursorPane,ShapeBounding,0,0,HD.shape_mask,
                        ShapeSet);
      XFreePixmap(HD.disp,HD.shape_mask);
    }

  }; /* Shape enabled */
} /* RefreshMouse */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_1bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int x,y,memoffset;
  char *ImgPtr=HD.ImageData;

  if (DC.video_palette_dirty) {
    set_video_4bpp_colourmap();
  }

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/8,ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state, memoffset, DisplayWidth/8))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state ,memoffset, DisplayWidth/8, ScanLineBuffer);

      for(x=0;x<DisplayWidth;x+=8) {
        int bit;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(bit=0;bit<=8;bit++) {
          ImgPtr[x+bit]=(ScanLineBuffer[x/8]>>bit) &1;
        } /* bit */
      } /* x */
    } /* Refresh test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_PseudoColor_1bpp */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_2bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int x,y,memoffset;
  char *ImgPtr=HD.ImageData;

  if (DC.video_palette_dirty) {
    set_video_4bpp_colourmap();
  }

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/4,ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,DisplayWidth/4))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state, memoffset, DisplayWidth/4, ScanLineBuffer);

      for(x=0; x<DisplayWidth; x+=4) {
        int pixel;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(pixel=0;pixel<4;pixel++) {
          ImgPtr[x+pixel]=(ScanLineBuffer[x/4]>>(pixel*2)) &3;
        } /* pixel */
      } /* x */
    } /* Update test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_PseudoColor_2bpp */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_4bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int x,y,memoffset;
  char *ImgPtr=HD.ImageData;

  if (DC.video_palette_dirty) {
    set_video_4bpp_colourmap();
  }

  for(y=0,memoffset=0; y<DisplayHeight;
                      y++,memoffset+=(DisplayWidth/2),ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state, memoffset, DisplayWidth/2))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state, memoffset, DisplayWidth/2, ScanLineBuffer);

      for(x=0; x<DisplayWidth; x+=2) {
        int pixel;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(pixel=0; pixel<2; pixel++) {
          ImgPtr[x+pixel] = (ScanLineBuffer[x/2]>>(pixel*4)) &15;
        } /* pixel */
      } /* x */
    } /* Refresh test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_PseudoColor_4bpp */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_8bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int y,memoffset;
  char *ImgPtr=HD.ImageData;

  if (DC.video_palette_dirty) {
    set_video_8bpp_colourmap();
  }

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth,ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state, memoffset, DisplayWidth))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state, memoffset, DisplayWidth, ImgPtr);
    } /* Refresh test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_PseudoColor_8bpp */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_1bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int x,y,memoffset;

  if (DC.video_palette_dirty) {
    set_video_4bpp_pixelmap();
  }

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/8) {
    if ((DC.MustRedraw) || (QueryRamChange(state, memoffset, DisplayWidth/8))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state, memoffset, DisplayWidth/8, ScanLineBuffer);

      for(x=0; x<DisplayWidth; x+=8) {
        int bit;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(bit=0;bit<=8;bit++) {
          XPutPixel(HD.DisplayImage,x+bit,y,HD.pixelMap[(ScanLineBuffer[x/8]>>bit) &1]);
        } /* bit */
      } /* x */
    } /* Refresh test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state, memoffset);
} /* RefreshDisplay_TrueColor_1bpp */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_2bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int x,y,memoffset;

  if (DC.video_palette_dirty) {
    set_video_4bpp_pixelmap();
  }

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/4) {
    if ((DC.MustRedraw) || (QueryRamChange(state, memoffset, DisplayWidth/4))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state, memoffset, DisplayWidth/4, ScanLineBuffer);

      for(x=0; x<DisplayWidth; x+=4) {
        int pixel;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(pixel=0;pixel<4;pixel++) {
          XPutPixel(HD.DisplayImage,x+pixel,y,HD.pixelMap[(ScanLineBuffer[x/4]>>(pixel*2)) &3]);
        } /* pixel */
      } /* x */
    } /* Update test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_TrueColor_2bpp */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_4bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int x,y,memoffset;

  if (DC.video_palette_dirty) {
    set_video_4bpp_pixelmap();
  }

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=(DisplayWidth/2)) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset, DisplayWidth/2))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state, memoffset, DisplayWidth/2, ScanLineBuffer);

      for(x=0; x<DisplayWidth; x+=2) {
        int pixel;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(pixel=0; pixel<2; pixel++) {
          XPutPixel(HD.DisplayImage, x+pixel, y, HD.pixelMap[(ScanLineBuffer[x/2]>>(pixel*4)) &15]);
        } /* pixel */
      } /* x */
    } /* Refresh test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_TrueColor_4bpp */

/*----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_8bpp(ARMul_State *state, int DisplayHeight, int DisplayWidth) {
  int x,y,memoffset;

  if (DC.video_palette_dirty) {
    set_video_8bpp_pixelmap();
  }

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state, memoffset, DisplayWidth))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state, memoffset, DisplayWidth, ScanLineBuffer);

      for(x=0; x<DisplayWidth; x++) {
        XPutPixel(HD.DisplayImage, x, y, HD.pixelMap[ScanLineBuffer[x]]);
      } /* X loop */
    } /* Refresh test */
  } /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_TrueColor_8bpp */

/*----------------------------------------------------------------------------*/
void
RefreshDisplay(ARMul_State *state)
{
  int DisplayHeight =  VIDC.Vert_DisplayEnd  - VIDC.Vert_DisplayStart;
  int DisplayWidth  = (VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) * 2;

  DC.AutoRefresh  = AUTOREFRESHPOLL;
  ioc.IRQStatus  |= IRQA_VFLYBK; /* VSync */
  ioc.IRQStatus  |= IRQA_TM0; /* Sound - just an experiment */
  IO_UpdateNirq();

  RefreshMouse(state);

  DC.miny = DisplayHeight - 1;
  DC.maxy = 0;

  /* First figure out number of BPP */
  if (HD.visInfo.class == PseudoColor) {
    if (DC.border_palette_dirty) {
      set_border_colourmap();
    }
    if (DC.cursor_palette_dirty) {
      set_cursor_colourmap();
    }

    if (DisplayHeight > 0 && DisplayWidth > 0) {
      switch ((VIDC.ControlReg & 0xC)>>2) {
        case 0: /* 1bpp */
          RefreshDisplay_PseudoColor_1bpp(state, DisplayHeight, DisplayWidth);
          break;
        case 1: /* 2bpp */
          RefreshDisplay_PseudoColor_2bpp(state, DisplayHeight, DisplayWidth);
          break;
        case 2: /* 4bpp */
          RefreshDisplay_PseudoColor_4bpp(state, DisplayHeight, DisplayWidth);
          break;
        case 3: /* 8bpp */
          RefreshDisplay_PseudoColor_8bpp(state, DisplayHeight, DisplayWidth);
          break;
      }
    } else {
      fprintf(stderr,"RefreshDisplay: 0 or -ve display width or height\n");
    }
  } else {
    if (DC.border_palette_dirty) {
      set_border_pixelmap();
    }
    if (DC.cursor_palette_dirty) {
      set_cursor_pixelmap();
    }

    if (DisplayHeight > 0 && DisplayWidth > 0) {
      switch ((VIDC.ControlReg & 0xC)>>2) {
        case 0: /* 1bpp */
          RefreshDisplay_TrueColor_1bpp(state, DisplayHeight, DisplayWidth);
          break;
        case 1: /* 2bpp */
          RefreshDisplay_TrueColor_2bpp(state, DisplayHeight, DisplayWidth);
        break;
        case 2: /* 4bpp */
          RefreshDisplay_TrueColor_4bpp(state, DisplayHeight, DisplayWidth);
          break;
        case 3: /* 8bpp */
          RefreshDisplay_TrueColor_8bpp(state, DisplayHeight, DisplayWidth);
          break;
      }
    } else {
      fprintf(stderr,"RefreshDisplay: 0 or -ve display width or height\n");
    }
  }

  /*fprintf(stderr,"RefreshDisplay: Refreshed %d-%d\n",DC.miny,DC.maxy); */
  /* Only tell X to redisplay those bits which we've replotted into the image */
  if (DC.miny < DC.maxy) {
    XPutImage(HD.disp, HD.MainPane, HD.MainPaneGC, HD.DisplayImage,
              0, DC.miny, /* source pos. in image */
              0, DC.miny, /* Position on window */
              DisplayWidth, // width of current mode, not MonitorWidth
              (DC.maxy - DC.miny) + 1);
  }

  XPutImage(HD.disp, HD.CursorPane, HD.MainPaneGC, HD.CursorImage,
              0, 0,
              0, 0,
              32, ((VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart) - 1));
} /* RefreshDisplay */

/* ------------------------------------------------------------------ */

static void set_video_4bpp_colourmap(void)
{
    int c;
    unsigned int pal;
    XColor col;

    for (c = 0; c < 16; c++) {
        pal = VIDC.Palette[c];

        col.flags = DoRed | DoGreen | DoBlue;
        col.pixel = c;
        col.red = pal & 0xf;
        col.green = pal >> 4 & 0xf;
        col.blue = pal >> 8 & 0xf;

        MULT_BY_0x1111(col.red);
        MULT_BY_0x1111(col.green);
        MULT_BY_0x1111(col.blue);

        XStoreColor(HD.disp, HD.ArcsColormap, &col);
    }

    DC.video_palette_dirty = FALSE;
}

/* ------------------------------------------------------------------ */

static void set_video_8bpp_colourmap(void)
{
    int c;
    int l4;
    int l65;
    int l7;
    unsigned int pal;
    XColor col;

    for (c = 0; c < 256; c++) {
        l4 = c >> 1 & 8;
        l65 = c >> 3 & 0xc;
        l7 = c >> 4 & 8;

        col.flags = DoRed | DoGreen | DoBlue;
        col.pixel = c;
        pal = VIDC.Palette[c & 0xf];
        col.red = l4 | (pal & 7);
        col.green = l65 | (pal >> 4 & 3);
        col.blue = l7 | (pal >> 8 & 7);

        MULT_BY_0x1111(col.red);
        MULT_BY_0x1111(col.green);
        MULT_BY_0x1111(col.blue);

        XStoreColor(HD.disp, HD.ArcsColormap, &col);
    }

    DC.video_palette_dirty = FALSE;
}

/* ------------------------------------------------------------------ */

static void set_border_colourmap(void)
{
    unsigned int pal;
    XColor col;

    pal = VIDC.BorderCol;
    col.flags = DoRed | DoGreen | DoBlue;
    col.pixel = BORDER_COLOURMAP_ENTRY;
    col.red = pal & 0xf;
    col.green = pal >> 4 & 0xf;
    col.blue = pal >> 8 & 0xf;

    MULT_BY_0x1111(col.red);
    MULT_BY_0x1111(col.green);
    MULT_BY_0x1111(col.blue);

    XStoreColor(HD.disp, HD.ArcsColormap, &col);

    DC.border_palette_dirty = FALSE;
}

/* ------------------------------------------------------------------ */

static void set_cursor_colourmap(void)
{
    int c;
    unsigned int pal;
    XColor col;

    for (c = 0; c < 3; c++) {
        pal = VIDC.CursorPalette[c];
        col.flags = DoRed | DoGreen | DoBlue;
        col.pixel = c + CURSORCOLBASE;
        col.red = pal & 0xf;
        col.green = pal >> 4 & 0xf;
        col.blue = pal >> 8 & 0xf;

        MULT_BY_0x1111(col.red);
        MULT_BY_0x1111(col.green);
        MULT_BY_0x1111(col.blue);

        XStoreColor(HD.disp, HD.ArcsColormap, &col);
    }

    DC.cursor_palette_dirty = FALSE;
}

/* ------------------------------------------------------------------ */

static void set_video_4bpp_pixelmap(void)
{
    int c;
    unsigned int pal;
    unsigned short r;
    unsigned short g;
    unsigned short b;

    for (c = 0; c < 16; c++) {
        pal = VIDC.Palette[c];

        r = pal & 0xf;
        g = pal >> 4 & 0xf;
        b = pal >> 8 & 0xf;

        MULT_BY_0x1111(r);
        MULT_BY_0x1111(g);
        MULT_BY_0x1111(b);

        HD.pixelMap[c] = get_pixelval(r, g, b);
    }

    DC.video_palette_dirty = FALSE;
}

/* ------------------------------------------------------------------ */

static void set_video_8bpp_pixelmap(void)
{
    int c;
    int l4;
    int l65;
    int l7;
    unsigned int pal;
    unsigned short r;
    unsigned short g;
    unsigned short b;

    for (c = 0; c < 256; c++) {
        l4 = c >> 1 & 8;
        l65 = c >> 3 & 0xc;
        l7 = c >> 4 & 8;

        pal = VIDC.Palette[c & 0xf];
        r = l4 | (pal & 7);
        g = l65 | (pal >> 4 & 3);
        b = l7 | (pal >> 8 & 7);

        MULT_BY_0x1111(r);
        MULT_BY_0x1111(g);
        MULT_BY_0x1111(b);

        HD.pixelMap[c] = get_pixelval(r, g, b);
    }

    DC.video_palette_dirty = FALSE;
}

/* ------------------------------------------------------------------ */

static void set_border_pixelmap(void)
{
    unsigned int pal;
    unsigned short r;
    unsigned short g;
    unsigned short b;

    pal = VIDC.BorderCol;

    r = pal & 0xf;
    g = pal >> 4 & 0xf;
    b = pal >> 8 & 0xf;

    MULT_BY_0x1111(r);
    MULT_BY_0x1111(g);
    MULT_BY_0x1111(b);

    HD.border_map = get_pixelval(r, g, b);
    XSetWindowBackground(HD.disp, HD.BackingWindow, HD.border_map);
    XClearWindow(HD.disp, HD.BackingWindow);

    DC.border_palette_dirty = FALSE;
}

/* ------------------------------------------------------------------ */

static void set_cursor_pixelmap(void)
{
    int c;
    unsigned int pal;
    unsigned short r;
    unsigned short g;
    unsigned short b;

    for (c = 0; c < 3; c++) {
        pal = VIDC.CursorPalette[c];

        r = pal & 0xf;
        g = pal >> 4 & 0xf;
        b = pal >> 8 & 0xf;

        MULT_BY_0x1111(r);
        MULT_BY_0x1111(g);
        MULT_BY_0x1111(b);

        /* Entry 0 is transparent. */
        HD.cursorPixelMap[c + 1] = get_pixelval(r, g, b);
    }

    DC.cursor_palette_dirty = FALSE;
}

/*----------------------------------------------------------------------------*/

static int DisplayKbd_XError(Display* disp, XErrorEvent *err)
{
  char s[1024];

  XGetErrorText(disp, err->error_code, s, 1023);

  fprintf(stderr,
"arcem X error detected: '%s'\n"
"If you didn't close arcem's windows to cause it please report it\n"
"along with this text to arcem-devel@lists.sourceforge.net.\n"
"Original error message follows.\n", s);

  (*prev_x_error_handler)(disp, err);

  exit(EXIT_FAILURE);
}

/*----------------------------------------------------------------------------*/
void
DisplayKbd_InitHost(ARMul_State *state)
{
  const char *s;
  KeySym ks;
  XSetWindowAttributes attr;
  unsigned long attrmask;
  XColor tmpcol;
  int prescol;
  XTextProperty name;
  int shape_event_base, shape_error_base;

  HD.disp=XOpenDisplay(NULL);
  if (HD.disp==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't open display\n");
    exit(EXIT_FAILURE);
  }

  prev_x_error_handler = XSetErrorHandler(DisplayKbd_XError);

  if (getenv("ARCEMXSYNC")) {
    XSynchronize(HD.disp, 1);
    fputs("arcem: synchronous X protocol selected.\n", stderr);
  }

  if ((s = getenv("ARCEMXMOUSEKEY"))) {
    if ((ks = XStringToKeysym(s))) {
      mouse_key.name = s;
      mouse_key.keysym = ks;
    } else {
      fprintf(stderr, "unknown mouse_key keysym: %s\n", s);
    }
    fprintf(stderr, "mouse_key is %s\n", mouse_key.name);
  }

  HD.xScreen    = XDefaultScreenOfDisplay(HD.disp);
  HD.ScreenNum  = XScreenNumberOfScreen(HD.xScreen);
  HD.RootWindow = DefaultRootWindow(HD.disp);

  /* Try and find a visual we can work with */
  if (XMatchVisualInfo(HD.disp,HD.ScreenNum,8,PseudoColor,&(HD.visInfo))) {
    /* It's 8 bpp, Pseudo colour - the easiest to deal with */
    printf("Ah - a nice easy 8bpp visual....\n");
  } else {
    /* Could probably cope with a wider range of visuals - particularly
    any other true colour probably? */
    /* NOTE: this is using short circuit eval to only evaluate upto the first match */
    if ((XMatchVisualInfo(HD.disp,HD.ScreenNum,24,TrueColor,&(HD.visInfo))) ||
        (XMatchVisualInfo(HD.disp,HD.ScreenNum,32,TrueColor,&(HD.visInfo))) ||
        (XMatchVisualInfo(HD.disp,HD.ScreenNum,16,TrueColor,&(HD.visInfo))) ||
        (XMatchVisualInfo(HD.disp,HD.ScreenNum,15,TrueColor,&(HD.visInfo)))) {
      /* True colour - extract the shift and precision values */
      gdk_visual_decompose_mask(HD.visInfo.visual->red_mask,&(HD.red_shift),&(HD.red_prec));
      gdk_visual_decompose_mask(HD.visInfo.visual->green_mask,&(HD.green_shift),&(HD.green_prec));
      gdk_visual_decompose_mask(HD.visInfo.visual->blue_mask,&(HD.blue_shift),&(HD.blue_prec));
      printf("Shift/masks = r/g/b = %d/%d,%d/%d,%d/%d\n",HD.red_shift,HD.red_prec,
                                                         HD.green_shift,HD.green_prec,
                                                         HD.blue_shift,HD.blue_prec);
    } else {
      fprintf(stderr,"DisplayKbd_Init: Failed to find a matching visual - I'm looking for either 8 bit Pseudo colour, or 32,24,16,  or 15 bit TrueColour - sorry\n");
      exit(EXIT_FAILURE);
    }
  }

#ifdef DEBUG_X_INIT
  {
    XVisualInfo *vi;

    vi = &HD.visInfo;
    fprintf(stderr, "XVisualInfo: %p, %#lx, %d, %d, %d, %#lx, "
            "%#lx, %#lx, %d, %d)\n", vi->visual, vi->visualid,
            vi->screen, vi->depth, vi->class, vi->red_mask,
            vi->green_mask, vi->blue_mask, vi->colormap_size,
            vi->bits_per_rgb);
  }
#endif

  HD.ArcsColormap = XCreateColormap(HD.disp, HD.RootWindow,
        HD.visInfo.visual, HD.visInfo.class == PseudoColor ? AllocAll :
        AllocNone);

  attr.border_pixel = 0;
  attr.colormap = HD.ArcsColormap;
  attrmask = CWBorderPixel | CWColormap;

  if (HD.visInfo.class == PseudoColor) {
    attr.background_pixel = BORDER_COLOURMAP_ENTRY;
    attrmask |= CWBackPixel;
  }

  /* Create the main arcem pane, create it small, it will be
     pushed bigger when the VIDC recieves instructions to change video
     mode */
  HD.BackingWindow = XCreateWindow(HD.disp, HD.RootWindow, 500, 500,
        InitialVideoWidth + VIDC_BORDER * 2, InitialVideoHeight + VIDC_BORDER * 2,
        0, HD.visInfo.depth, InputOutput, HD.visInfo.visual, attrmask,
        &attr);

  {
    char *title = strdup("Arc emulator - Main display");

    if (XStringListToTextProperty(&title, 1, &name) == 0) {
      fprintf(stderr,"Could not allocate window name\n");
      exit(EXIT_FAILURE);
    }

    XSetWMName(HD.disp,HD.BackingWindow,&name);
    XFree(name.value);
  }


  HD.MainPane = XCreateWindow(HD.disp, HD.BackingWindow, VIDC_BORDER,
      VIDC_BORDER, InitialVideoWidth, InitialVideoHeight, 0, CopyFromParent,
      InputOutput, CopyFromParent, 0, NULL);

  HD.CursorPane = XCreateWindow(HD.disp,
                                HD.MainPane,
                                0, 0,
                                32, InitialVideoHeight,
                                0, /* Border width */
                                CopyFromParent, /* depth */
                                InputOutput, /* class */
                                CopyFromParent, /* visual */
                                0, /* valuemask */
                                NULL /* attribs */);



  /* Allocate the memory for the actual display image */
  //TODO!! Need to allocate more for truecolour
  HD.ImageData = malloc(4 * (InitialVideoWidth+ 32) * InitialVideoHeight);

  if (HD.ImageData == NULL) {
    fprintf(stderr, "DisplayKbd_Init: Couldn't allocate image memory\n");
    exit(EXIT_FAILURE);
  }

  HD.DisplayImage = XCreateImage(HD.disp, DefaultVisual(HD.disp, HD.ScreenNum),
                                 HD.visInfo.depth, ZPixmap, 0, HD.ImageData,
                                 InitialVideoWidth, InitialVideoHeight, 32,
                                 0);
  if (HD.DisplayImage == NULL) {
    fprintf(stderr, "DisplayKbd_Init: Couldn't create image\n");
    exit(EXIT_FAILURE);
  }

  /* Now the same for the cursor image */
  HD.CursorImageData = malloc(4 * 64 * InitialVideoHeight);
  if (HD.CursorImageData == NULL) {
    fprintf(stderr, "DisplayKbd_Init: Couldn't allocate cursor image memory\n");
    exit(EXIT_FAILURE);
  }

  HD.CursorImage = XCreateImage(HD.disp, DefaultVisual(HD.disp, HD.ScreenNum),
                                HD.visInfo.depth, ZPixmap, 0, HD.CursorImageData,
                                32, InitialVideoHeight, 32,
                                0);
  if (HD.CursorImage == NULL) {
    fprintf(stderr, "DisplayKbd_Init: Couldn't create cursor image\n");
    exit(EXIT_FAILURE);
  }


  XSelectInput(HD.disp,HD.MainPane,ExposureMask |
                                   PointerMotionMask |
                                   EnterWindowMask | /* For changing colour maps */
                                   LeaveWindowMask | /* For changing colour maps */
                                   KeyPressMask |
                                   KeyReleaseMask |
                                   ButtonPressMask |
                                   ButtonReleaseMask);
  XSelectInput(HD.disp,HD.CursorPane,ExposureMask |
                                   FocusChangeMask |
                                   KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
  HD.DefaultColormap=DefaultColormapOfScreen(HD.xScreen);

  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"white",&(HD.White),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'white'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"black",&(HD.Black),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'black'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"red",&(HD.Red),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'red'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"green",&(HD.Green),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'green'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"gray10",&(HD.GreyDark),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'gray10'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"gray90",&(HD.GreyLight),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'gray90'\n");

  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"PapayaWhip",&(HD.OffWhite),&tmpcol)) {
    /* A great shame - a rather nice colour */
    HD.OffWhite = HD.White;
    fprintf(stderr,"Failed to allocate colour 'PapayaWhip'\n");
  };

  /* I think the main monitor window will need its own colourmap
     since we need at least 256 colours for 256 colour mode */
  if (HD.visInfo.class==PseudoColor) {

    for(prescol=0; prescol<256; prescol++) {
      tmpcol.flags = DoRed | DoGreen | DoBlue;
      tmpcol.pixel = prescol;
      tmpcol.red   = (prescol & 1) ? 65535 : 0;
      tmpcol.green = (prescol & 2) ? 65535 : 0;
      tmpcol.blue  = (prescol & 2) ? 65535 : 0;
      XStoreColor(HD.disp, HD.ArcsColormap, &tmpcol); /* Should use XStoreColors */
    };

    XSetWindowColormap(HD.disp,HD.MainPane,HD.ArcsColormap);
    /* The following seems to be necessary to get the colourmap to switch -
    but I don't understand why! */
    XSetWindowColormap(HD.disp,HD.BackingWindow,HD.ArcsColormap);

    /* I think is to get the window manager to automatically set the colourmap ... */
    XSetWMColormapWindows(HD.disp,HD.BackingWindow,&(HD.MainPane),1);

    /*HD.ArcsColormap=DefaultColormapOfScreen(HD.xScreen); */
  } else {
    /* TrueColor - the colourmap is actually a fixed thing */
    for(prescol=0;prescol<256;prescol++) {
      tmpcol.flags = DoRed | DoGreen | DoBlue;
      tmpcol.red   = (prescol & 1) ? 65535 : 0;
      tmpcol.green = (prescol & 2) ? 65535 : 0;
      tmpcol.blue  = (prescol & 2) ? 65535 : 0;
      tmpcol.pixel = prescol;
      /*XQueryColor(HD.disp,HD.ArcsColormap,&tmpcol);*/
      HD.pixelMap[prescol]=0;
    };
  };

  HD.MainPaneGC = XCreateGC(HD.disp, HD.MainPane, 0, NULL);

  /* Calloc to clear it as well */
  HD.ShapePixmapData = calloc(32 * InitialVideoHeight, 1);
  if (HD.ShapePixmapData == NULL) {
    fprintf(stderr, "Couldn't allocate memory for pixmap data\n");
    exit(EXIT_FAILURE);
  }

  /* Shape stuff for the mouse cursor window */
  if (!XShapeQueryExtension(HD.disp, &shape_event_base, &shape_error_base)) {
    HD.ShapeEnabled = 0;
    HD.shape_mask = 0;
  } else {
    HD.ShapeEnabled = 1;

    HD.shape_mask = XCreatePixmapFromBitmapData(HD.disp, HD.CursorPane, HD.ShapePixmapData,
                                                32, InitialVideoHeight, 0, 1, 1);
    /* Eek - a lot of this is copied from XEyes - unfortunatly the manual
       page for this call is somewhat lacking.  To quote its bugs entry:
      'This manual page needs a lot more work' */
    XShapeCombineMask(HD.disp, HD.CursorPane, ShapeBounding, 0, 0, HD.shape_mask,
                      ShapeSet);
    XFreePixmap(HD.disp, HD.shape_mask);
  };

  XMapWindow(HD.disp,HD.BackingWindow);
  XMapSubwindows(HD.disp,HD.BackingWindow);
  XMapSubwindows(HD.disp,HD.MainPane);

  ControlPane_Init(state);
} /* DisplayKbd_InitHost */


/*----------------------------------------------------------------------------*/

static void BackingWindow_Event(ARMul_State *state,XEvent *e) {
    fprintf(stderr, "unwanted BackingWindow_Event type=%d\n", e->type);
} /* BackingWindow_Event */



/*----------------------------------------------------------------------------*/

static void ProcessKey(ARMul_State *state, XKeyEvent *key)
{
  KeySym sym;
  const keysym_to_arch_key *ktak;

  XLookupString(key, NULL, 0, &sym, NULL);

  /* Trap the special key for mouse following */
  if (sym == mouse_key.keysym) {
    /* And when it is pressed toggle the mouse follow mode */
    if (key->type == KeyPress) {
      DC.DoingMouseFollow^=1;
      fprintf(stderr, "%s pressed, turning mouse tracking %s.\n",
              mouse_key.name, DC.DoingMouseFollow ? "on" : "off");

      if (DC.DoingMouseFollow)
      {
        /* Create a blank cursor and use that */
        XColor black, dummy;
        Pixmap bm_no;
        static unsigned char bm_no_data[] = { 0,0,0,0, 0,0,0,0 };
        XAllocNamedColor(HD.disp, HD.ArcsColormap, "black", &black, &dummy);
        bm_no = XCreateBitmapFromData(HD.disp, HD.MainPane, bm_no_data, 8, 8);
        XDefineCursor(HD.disp, HD.MainPane,
                      XCreatePixmapCursor(HD.disp, bm_no, bm_no, &black, &black, 0, 0));
      }
      else
      {
        /* Restore the original cursor */
        XUndefineCursor(HD.disp, HD.MainPane);
      }
    }
    return;
  }

  /* Just take the unshifted version of the key */
  sym = XLookupKeysym(key,0);

  for (ktak = keysym_to_arch_key_map; ktak->sym; ktak++) {
    if (ktak->sym == sym) {
      keyboard_key_changed(&KBD, ktak->kid, key->type == KeyRelease);
      return;
    }
  }

  fprintf(stderr,"ProcessKey: Unknown key sym=%ld!\n",sym);
} /* ProcessKey */


/*----------------------------------------------------------------------------*/

static void ProcessButton(ARMul_State *state, XButtonEvent *button)
{
  arch_key_id kid;

  switch (button->button) {
    case Button1:
      kid = ARCH_KEY_button_1;
      break;
    case Button2:
      kid = ARCH_KEY_button_2;
      break;
    case Button3:
      kid = ARCH_KEY_button_3;
      break;
    case Button4: // Mousewheel up
      kid = ARCH_KEY_button_4;
      break;
    case Button5: // Mousewheel down
      kid = ARCH_KEY_button_5;
      break;
    default:
      return;
  }

  keyboard_key_changed(&KBD, kid, button->type == ButtonRelease);
}


/*----------------------------------------------------------------------------*/
/* Move the Control pane window                                                */

static void UpdateCursorPos(ARMul_State *state) {
  int HorizPos = (int)VIDC.Horiz_CursorStart - (int)VIDC.Horiz_DisplayStart * 2;
  int VertPos, tmp;
  int Height = (int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart + 1;
#ifdef DEBUG_CURSOR
  fprintf(stderr, "UpdateCursorPos: Horiz_CursorStart=0x%x\n",  VIDC.Horiz_CursorStart);
  fprintf(stderr, "UpdateCursorPos: Vert_CursorStart=0x%x\n",   VIDC.Vert_CursorStart);
  fprintf(stderr, "UpdateCursorPos: Vert_CursorEnd=0x%x\n",     VIDC.Vert_CursorEnd);
  fprintf(stderr, "UpdateCursorPos: Horiz_DisplayStart=0x%x\n", VIDC.Horiz_DisplayStart);
  fprintf(stderr, "UpdateCursorPos: Vert_DisplayStart=0x%x\n",  VIDC.Vert_DisplayStart);
#endif
  VertPos = (int)VIDC.Vert_CursorStart;
  tmp = (signed int)VIDC.Vert_DisplayStart;
  VertPos -= tmp;
  if (Height < 1)
    Height = 1;

  if (VertPos < 0)
    VertPos = 0;

#ifdef DEBUG_CURSOR
  fprintf(stderr,"UpdateCursorPos: Height=%d VertPos=%d HorizPos=%d\n",Height,VertPos,HorizPos);
#endif
  XMoveResizeWindow(HD.disp, HD.CursorPane, HorizPos, VertPos, 32, Height);
} /* UpdateCursorPos */


/*----------------------------------------------------------------------------*/
/* Called on an X motion event when we are in mouse tracking mode */

static void MouseMoved(ARMul_State *state,XMotionEvent *xmotion)
{
  int xdiff, ydiff;
  unsigned ScreenWidth  = (VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) * 2;
  unsigned ScreenHeight = VIDC.Vert_DisplayEnd - VIDC.Vert_DisplayStart;

  /* Well the coordinates of the mouse cursor are now in xmotion->x and
     xmotion->y, I'm going to compare those against the cursor position
     and transmit the difference.  This can't possibly take care of the OS's
     hotspot offsets */

  /* We are now only using differences from the reference position */
  if ((xmotion->x == ScreenWidth / 2) && (xmotion->y == ScreenHeight / 2))
    return;

  /* Keep the mouse pointer under our window */
  XWarpPointer(HD.disp,
               None,                               /* src window for relative coords (NOT USED) */
               HD.MainPane,                        /* Destination window to move cursor in */
               0, 0,                               /* relative coords (NOT USED) */
               9999, 9999,                         /* src width and height (NOT USED) */
               ScreenWidth / 2, ScreenHeight / 2); /* Coordinates in the destination window */

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: CursorStart=%d xmotion->x=%d\n",
          VIDC.Horiz_CursorStart,xmotion->x);
#endif
  xdiff = xmotion->x - ScreenWidth / 2;
  if (KBD.MouseXCount != 0) {
    if (KBD.MouseXCount & 64) {
      signed char tmpC;
      int tmpI;
      tmpC = KBD.MouseXCount | 128;
      tmpI = (signed int)tmpC;
      xdiff += tmpI;
    } else {
      xdiff += KBD.MouseXCount;
    }
  }

  if (xdiff > 63)
    xdiff = 63;
  if (xdiff < -63)
    xdiff = -63;

  ydiff = ScreenHeight / 2 - xmotion->y;
  if (KBD.MouseYCount & 64) {
    signed char tmpC;
    tmpC = KBD.MouseYCount | 128; /* Sign extend */
    ydiff += tmpC;
  } else {
    ydiff += KBD.MouseYCount;
  }
  if (ydiff > 63)
    ydiff = 63;
  if (ydiff < -63)
    ydiff = -63;

  KBD.MouseXCount = xdiff & 127;
  KBD.MouseYCount = ydiff & 127;

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr, "MouseMoved: generated counts %d,%d xdiff=%d ydifff=%d\n", KBD.MouseXCount, KBD.MouseYCount, xdiff, ydiff);
#endif
} /* MouseMoved */


/*----------------------------------------------------------------------------*/

static void MainPane_Event(ARMul_State *state,XEvent *e) {
  switch (e->type) {
    case EnterNotify:
      /*fprintf(stderr,"MainPane: Enter notify!\n"); */
        hostdisplay_change_focus(TRUE);
      DC.PollCount=0;
      break;

    case LeaveNotify:
      /*fprintf(stderr,"MainPane: Leave notify!\n"); */
        hostdisplay_change_focus(FALSE);
      DC.PollCount=0;
      break;

    case Expose:
      XPutImage(HD.disp,HD.MainPane,HD.MainPaneGC,HD.DisplayImage,
                e->xexpose.x,e->xexpose.y, /* source pos. in image */
                e->xexpose.x,e->xexpose.y, /* Position on window */
                e->xexpose.width,e->xexpose.height);
      break;

    case ButtonPress:
    case ButtonRelease:
      ProcessButton(state,&(e->xbutton));
      break;

    case KeyPress:
    case KeyRelease:
      ProcessKey(state,&(e->xkey));
      break;

    case MotionNotify:
#ifdef DEBUG_MOUSEMOVEMENT
      fprintf(stderr,"Motion Notify in mainpane\n");
#endif
      if (DC.DoingMouseFollow) {
        MouseMoved(state, &(e->xmotion));
      }
      break;

    default:
      fprintf(stderr, "unwanted MainPane_Event type=%d\n", e->type);
      break;
  };
} /* MainPane_Event */


/*----------------------------------------------------------------------------*/

static void CursorPane_Event(ARMul_State *state,XEvent *e) {
  /*fprintf(stderr,"CursorPane_Event type=%d\n",e->type); */
  switch (e->type) {
    case EnterNotify:
      fprintf(stderr,"CursorPane: Enter notify!\n");
        hostdisplay_change_focus(TRUE);
      break;

    case LeaveNotify:
      fprintf(stderr,"CursorPane: Leave notify!\n");
        hostdisplay_change_focus(FALSE);
      DC.PollCount=0;
      break;

    case Expose:
      XPutImage(HD.disp,HD.CursorPane,HD.MainPaneGC,HD.CursorImage,
                e->xexpose.x,e->xexpose.y, /* source pos. in image */
                e->xexpose.x,e->xexpose.y, /* Position on window */
                e->xexpose.width,e->xexpose.height);
      break;

    case ButtonPress:
    case ButtonRelease:
      ProcessButton(state,&(e->xbutton));
      break;

    case KeyPress:
    case KeyRelease:
      ProcessKey(state,&(e->xkey));
      break;

    default:
        fprintf(stderr, "unwanted CursorPane_Event type=%d\n", e->type);
      break;
  };
} /* CursorPane_Event */


/*----------------------------------------------------------------------------*/

int
DisplayKbd_PollHost(ARMul_State *state)
{
  XEvent e;

  if (XCheckMaskEvent(HD.disp, ULONG_MAX, &e)) {
#ifdef DEBUG_X_PROTOCOL
    if (e.xany.window == HD.BackingWindow) {
      printf("backingwindow ");
    } else if (e.xany.window == HD.MainPane) {
      printf("mainpane ");
    } else if (e.xany.window == HD.ControlPane) {
      printf("controlpane ");
    } else if (e.xany.window == HD.CursorPane) {
      printf("cursorpane ");
    } else {
      printf("unknown window ");
    }
    printf("= %d\n", e.type);
#endif

    if (e.xany.window == HD.BackingWindow) {
      BackingWindow_Event(state, &e);
    } else if (e.xany.window == HD.MainPane) {
      MainPane_Event(state, &e);
    } else if (e.xany.window == HD.ControlPane) {
      ControlPane_Event(state, &e);
    } else if (e.xany.window == HD.CursorPane) {
      CursorPane_Event(state, &e);
    } else {
      fprintf(stderr, "event on unknown window: %#lx %d\n",
              e.xany.window, e.type);
      exit(EXIT_FAILURE);
    }
  }
  return 0;
} /* DisplayKbd_PollHost */


/*----------------------------------------------------------------------------*/

void VIDC_PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw) {
  unsigned int addr, val;

  addr=(data>>24) & 255;
  val=data & 0xffffff;

  int must_resize;

  if (!(addr & 0xc0)) {
    int Log;

    /* This lot presumes it's not 8bpp mode! */
    Log=(addr>>2) & 15;
#ifdef DEBUG_VIDCREGS
    {
      int Sup,Red,Green,Blue;
      Sup   = (val >> 12) & 1;
      Blue  = (val >> 8) & 15;
      Green = (val >> 4) & 15;
      Red   =  val & 15;
      fprintf(stderr, "VIDC Palette write: Logical=%d Physical=(%d,%d,%d,%d)\n",
                      Log,Sup,Red,Green,Blue);
    }
#endif
    IF_DIFF_THEN_ASSIGN_AND_SET_FLAG(VIDC.Palette[Log],
            val & 0x1fff, DC.video_palette_dirty);
    DC.MustRedraw = 1;
    return;
  };

  addr&=~3;
  switch (addr) {
    case 0x40: /* Border col */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC border colour write val=0x%x\n",val);
#endif
        IF_DIFF_THEN_ASSIGN_AND_SET_FLAG(VIDC.BorderCol, val & 0x1fff,
            DC.border_palette_dirty);
      break;

    case 0x44: /* Cursor palette log col 1 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC cursor log col 1 write val=0x%x\n",val);
#endif
        IF_DIFF_THEN_ASSIGN_AND_SET_FLAG(VIDC.CursorPalette[0],
            val & 0x1fff, DC.cursor_palette_dirty);
      break;

    case 0x48: /* Cursor palette log col 2 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC cursor log col 2 write val=0x%x\n",val);
#endif
        IF_DIFF_THEN_ASSIGN_AND_SET_FLAG(VIDC.CursorPalette[1],
            val & 0x1fff, DC.cursor_palette_dirty);
      break;

    case 0x4c: /* Cursor palette log col 3 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC cursor log col 3 write val=0x%x\n",val);
#endif
        IF_DIFF_THEN_ASSIGN_AND_SET_FLAG(VIDC.CursorPalette[2],
            val & 0x1fff, DC.cursor_palette_dirty);
      break;

    case 0x60: /* Stereo image reg 7 */
    case 0x64: /* Stereo image reg 0 */
    case 0x68: /* Stereo image reg 1 */
    case 0x6c: /* Stereo image reg 2 */
    case 0x70: /* Stereo image reg 3 */
    case 0x74: /* Stereo image reg 4 */
    case 0x78: /* Stereo image reg 5 */
    case 0x7c: /* Stereo image reg 6 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC stereo image reg write val=0x%x\n",val);
#endif
      VIDC.StereoImageReg[(addr==0x60)?7:((addr-0x64)/4)]=val & 7;
#ifdef SOUND_SUPPORT
      SoundUpdateStereoImage();
#endif
      break;

    case 0x80:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz cycle register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_Cycle,(val>>14) & 0x3ff);
      break;

    case 0x84:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz sync width register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_SyncWidth,(val>>14) & 0x3ff);
      break;

    case 0x88:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz border start register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_BorderStart,(val>>14) & 0x3ff);
      break;

    case 0x8c:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz display start register val=%d\n",val>>14);
#endif
      must_resize = ((VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) == ((val>>14) & 0x3ff));
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayStart,((val>>14) & 0x3ff));
      if (must_resize) {
        /* Only resize the window if the screen size has actually changed */
        Resize_Window();
      }
      break;

    case 0x90:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz display end register val=%d\n",val>>14);
#endif
      must_resize = ((VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) == ((val>>14) & 0x3ff));
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayEnd,(val>>14) & 0x3ff);
      if (must_resize) {
        /* Only resize the window if the screen size has actually changed */
        Resize_Window();
      }
      break;

    case 0x94:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC horizontal border end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_BorderEnd,(val>>14) & 0x3ff);
      break;

    case 0x98:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC horiz cursor start register val=%d\n",val>>13);
#endif
      VIDC.Horiz_CursorStart=(val>>13) & 0x7ff;
      break;

    case 0x9c:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC horiz interlace register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_Interlace,(val>>14) & 0x3ff);
      break;

    case 0xa0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert cycle register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_Cycle,(val>>14) & 0x3ff);
      break;

    case 0xa4:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert sync width register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_SyncWidth,(val>>14) & 0x3ff);
      break;

    case 0xa8:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert border start register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_BorderStart,(val>>14) & 0x3ff);
      break;

    case 0xac:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert disp start register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_DisplayStart,((val>>14) & 0x3ff));
      Resize_Window();
      break;

    case 0xb0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert disp end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_DisplayEnd,(val>>14) & 0x3ff);
      Resize_Window();
      break;

    case 0xb4:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert Border end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_BorderEnd,(val>>14) & 0x3ff);
      break;

    case 0xb8:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert cursor start register val=%d\n",val>>14);
#endif
      VIDC.Vert_CursorStart=(val>>14) & 0x3ff;
      UpdateCursorPos(state);
      break;

    case 0xbc:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert cursor end register val=%d\n",val>>14);
#endif
      VIDC.Vert_CursorEnd=(val>>14) & 0x3ff;
      UpdateCursorPos(state);
      break;

    case 0xc0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Sound freq register val=%d\n",val);
#endif
      VIDC.SoundFreq=val & 0xff;
#ifdef SOUND_SUPPORT
      SoundUpdateSampleRate();
#endif
      break;

    case 0xe0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Control register val=0x%x\n",val);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.ControlReg,val & 0xffff);
      break;

    default:
      fprintf(stderr,"Write to unknown VIDC register reg=0x%x val=0x%x\n",addr,val);
      break;

  } /* Register switch */
} /* PutValVIDC */

/* ------------------------------------------------------------------ */


void hostdisplay_change_focus(int focus)
{
  if (HD.visInfo.class == PseudoColor) {
    (*(focus ? XInstallColormap : XUninstallColormap))(HD.disp, HD.ArcsColormap);
  }
  (*(focus ? XAutoRepeatOff : XAutoRepeatOn))(HD.disp);
}

/**
 * Resize_Window
 *
 * Resize the window to fit the new video mode.
 */
static void Resize_Window(void)
{
  int x = (VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart)*2;
  int y = VIDC.Vert_DisplayEnd - VIDC.Vert_DisplayStart;

  if(x > 0 && y > 0) {
    /* Upper bounds checking */
    if(x > MaxVideoWidth || y > MaxVideoHeight) {
      fprintf(stderr, "Resize_Window: Request to display a mode larger than we can handle (%dx%d)\n", x, y);
      exit(EXIT_FAILURE);
    }

    /* resize outer window including border */
    XResizeWindow(HD.disp, HD.BackingWindow,
                  x + (VIDC_BORDER * 2),
                  y + (VIDC_BORDER * 2) );

    /* resize inner window excluding border */
    XResizeWindow(HD.disp, HD.MainPane, x, y);

    /* clean up previous images used as display and cursor */
    XDestroyImage(HD.DisplayImage);
    XDestroyImage(HD.CursorImage);
    free(HD.ShapePixmapData);

    /* realocate space for new screen image */
    HD.ImageData = malloc(4 * (x + 32) * y);
    if (HD.ImageData == NULL) {
      fprintf(stderr, "Resize_Window: Couldn't allocate image memory\n");
      exit(EXIT_FAILURE);
    }

    HD.DisplayImage = XCreateImage(HD.disp,
                                   DefaultVisual(HD.disp, HD.ScreenNum),
                                   HD.visInfo.depth, ZPixmap, 0,
                                   HD.ImageData,
                                   x, y, 32,
                                   0);
    if (HD.DisplayImage == NULL) {
      fprintf(stderr, "Resize_Window: Couldn't create image\n");
      exit(EXIT_FAILURE);
    }

    /* realocate space for new cursor image */
    HD.CursorImageData = malloc(4 * 64 * y);
    if (HD.CursorImageData == NULL) {
      fprintf(stderr, "Resize_Window: Couldn't allocate cursor image memory\n");
      exit(EXIT_FAILURE);
    }

    HD.CursorImage = XCreateImage(HD.disp,
                                  DefaultVisual(HD.disp, HD.ScreenNum),
                                  HD.visInfo.depth, ZPixmap, 0,
                                  HD.CursorImageData,
                                  32, y, 32,
                                  0);
    if (HD.CursorImage == NULL) {
      fprintf(stderr, "Resize_Window: Couldn't create cursor image\n");
      exit(EXIT_FAILURE);
    }

    /* Calloc to clear it as well */
    HD.ShapePixmapData = calloc(32 * y, 1);
    if (HD.ShapePixmapData == NULL) {
      fprintf(stderr, "Resize_Window: Couldn't allocate memory for pixmap data\n");
      exit(EXIT_FAILURE);
    }
  }
}
