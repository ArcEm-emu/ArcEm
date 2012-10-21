/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info
   Hacked about with for new display driver interface by Jeffrey Lee, 2011 */
/* Display and keyboard interface for the Arc emulator */

/*#define DEBUG_VIDCREGS */
/*#define DEBUG_KBD */
/*#define DEBUG_MOUSEMOVEMENT */

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
#include "arch/archio.h"
#include "arch/hdc63463.h"
#ifdef SOUND_SUPPORT
#include "arch/sound.h"
#endif
#include "arch/displaydev.h"

#include "ControlPane.h"
#include "platform.h"

/* ------------------------------------------------------------------ */

/* General macros to help handle arrays.  Number of elements, i.e. what
 * its dimension is, and a pointer to the element past the end. */
#define DIM(a) ((sizeof (a)) / sizeof (a)[0])
#define END(a) ((a) + DIM(a))

/* A sensible set of defaults for the start window, the OS
   will call the VIDC and push this smaller or bigger later. */
#define InitialVideoWidth 640
#define InitialVideoHeight 480

#define ControlHeight 30

#define IF_DIFF_THEN_ASSIGN_AND_SET_FLAG(a, b, f) \
    if ((a) != (b)) { \
        (a) = (b); \
        (f) = TRUE; \
    }

/* ------------------------------------------------------------------ */
static int lastmousemode=0, lastmousex=0, lastmousey=0;

/* ------------------------------------------------------------------ */

static void store_colour(Colormap map, unsigned long pixel,
    unsigned short r, unsigned short g, unsigned short b);

static void *emalloc(size_t n, const char *use);
static void *ecalloc(size_t n, const char *use);

static void insist(int expr, const char *diag);

/* ------------------------------------------------------------------ */

static int (*prev_x_error_handler)(Display *, XErrorEvent *);

static struct {
    const char *name;
    KeySym keysym;
} mouse_key = {
    "KP_Add",
    XK_KP_Add
};

/* Structure to hold most of the X windows values, shared with ControlPane.c */
struct plat_display PD;


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
    return (((red   >> (16 - PD.red_prec))   << PD.red_shift)   +
            ((green >> (16 - PD.green_prec)) << PD.green_shift) +
            ((blue  >> (16 - PD.blue_prec))  << PD.blue_shift));

} /* get_pixval */

/* ------------------------------------------------------------------ */

/* X uses 16-bit RGB amounts.  If we've an Arcem one that's 4-bit, we
 * need to scale it up so 0x0 remains 0x0000 and 0xf becomes 0xffff,
 * with ther other fourteen values being evenly spread inbetween. */

unsigned int vidc_col_to_x_col(unsigned int col)
{
  unsigned int r = (col & 0xf)*0x1111;
  unsigned int g = ((col & 0xf0)>>4)*0x1111;
  unsigned int b = ((col & 0xf00)>>8)*0x1111;
  return get_pixelval(r,g,b);
}

/* ------------------------------------------------------------------ */

static void store_colour(Colormap map, unsigned long pixel,
    unsigned short r, unsigned short g, unsigned short b)
{
    XColor col;

    col.pixel = pixel;
    col.red = r;
    col.green = g;
    col.blue = b;
    col.flags = DoRed | DoGreen | DoBlue;
    XStoreColor(PD.disp, map, &col);

    return;
}

/*----------------------------------------------------------------------------*/

static int DisplayKbd_XError(Display* disp, XErrorEvent *err)
{
  char s[1024];

    XGetErrorText(disp, err->error_code, s, sizeof s - 1);

  fprintf(stderr,
"arcem X error detected: '%s'\n"
"If you didn't close arcem's windows to cause it please report it\n"
"along with this text to arcem-devel@lists.sourceforge.net.\n"
"Original error message follows.\n", s);

  (*prev_x_error_handler)(disp, err);

  exit(EXIT_FAILURE);
}

/*----------------------------------------------------------------------------*/
int
DisplayDev_Init(ARMul_State *state)
{
  const char *s;
  KeySym ks;
  XSetWindowAttributes attr;
  unsigned long attrmask;
  int prescol;
  XTextProperty name;
  int shape_event_base, shape_error_base;

  PD.disp=XOpenDisplay(NULL);
    insist(!!PD.disp, "opening X display in DisplayKbd_InitHost()");

  prev_x_error_handler = XSetErrorHandler(DisplayKbd_XError);

  if (getenv("ARCEMXSYNC")) {
    XSynchronize(PD.disp, 1);
    fputs("arcem: synchronous X protocol selected.\n", stderr);
  }

  if (getenv("ARCEMNOWARP")) {
    lastmousemode=1;
    fputs("arcem: no-XWarpPointer mode selected.\n", stderr);
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

  PD.xScreen    = XDefaultScreenOfDisplay(PD.disp);
  PD.ScreenNum  = XScreenNumberOfScreen(PD.xScreen);
  PD.RootWindow = DefaultRootWindow(PD.disp);

  /* Try and find a visual we can work with */
    fputs("X visual: ", stdout);
  if (XMatchVisualInfo(PD.disp,PD.ScreenNum,8,PseudoColor,&(PD.visInfo))) {
    /* It's 8 bpp, Pseudo colour - the easiest to deal with */
        printf("PseudoColor 8bpp found\n");
  } else {
    /* Could probably cope with a wider range of visuals - particularly
    any other true colour probably? */
    /* NOTE: this is using short circuit eval to only evaluate upto the first match */
    if ((XMatchVisualInfo(PD.disp,PD.ScreenNum,24,TrueColor,&(PD.visInfo))) ||
        (XMatchVisualInfo(PD.disp,PD.ScreenNum,32,TrueColor,&(PD.visInfo))) ||
        (XMatchVisualInfo(PD.disp,PD.ScreenNum,16,TrueColor,&(PD.visInfo))) ||
        (XMatchVisualInfo(PD.disp,PD.ScreenNum,15,TrueColor,&(PD.visInfo)))) {
      /* True colour - extract the shift and precision values */
      gdk_visual_decompose_mask(PD.visInfo.visual->red_mask,&(PD.red_shift),&(PD.red_prec));
      gdk_visual_decompose_mask(PD.visInfo.visual->green_mask,&(PD.green_shift),&(PD.green_prec));
      gdk_visual_decompose_mask(PD.visInfo.visual->blue_mask,&(PD.blue_shift),&(PD.blue_prec));
        printf("TrueColor %dbpp found, RGB shift/masks = "
            "%d/%d, %d/%d, %d/%d\n", PD.visInfo.depth, PD.red_shift,
            PD.red_prec, PD.green_shift, PD.green_prec, PD.blue_shift,
            PD.blue_prec);
    } else {
        puts("nothing suitable");
      ControlPane_Error(EXIT_FAILURE,"DisplayKbd_Init: Failed to find a matching visual - I'm looking for either 8 bit Pseudo colour, or 32,24,16,  or 15 bit TrueColour - sorry\n");
    }
  }

#ifdef DEBUG_X_INIT
  {
    XVisualInfo *vi;

    vi = &PD.visInfo;
    fprintf(stderr, "XVisualInfo: %p, %#lx, %d, %d, %d, %#lx, "
            "%#lx, %#lx, %d, %d)\n", vi->visual, vi->visualid,
            vi->screen, vi->depth, vi->class, vi->red_mask,
            vi->green_mask, vi->blue_mask, vi->colormap_size,
            vi->bits_per_rgb);
  }
#endif

  PD.ArcsColormap = XCreateColormap(PD.disp, PD.RootWindow,
        PD.visInfo.visual, PD.visInfo.class == PseudoColor ? AllocAll :
        AllocNone);

  attr.border_pixel = 0;
  attr.colormap = PD.ArcsColormap;
  attrmask = CWBorderPixel | CWColormap;

  if (PD.visInfo.class == PseudoColor) {
    attr.background_pixel = 0;
    attrmask |= CWBackPixel;
  }

  /* Create the main arcem pane, create it small, it will be
     pushed bigger when the VIDC recieves instructions to change video
     mode */
  PD.BackingWindow = XCreateWindow(PD.disp, PD.RootWindow, 500, 500,
        InitialVideoWidth + VIDC_BORDER * 2, InitialVideoHeight + VIDC_BORDER * 2,
        0, PD.visInfo.depth, InputOutput, PD.visInfo.visual, attrmask,
        &attr);

  {
    char *title = strdup("Arc emulator - Main display");

        insist(XStringListToTextProperty(&title, 1, &name),
            "allocating window name in DisplayKbd_InitHost()");
    XSetWMName(PD.disp,PD.BackingWindow,&name);
    XFree(name.value);
  }


  PD.MainPane = XCreateWindow(PD.disp, PD.BackingWindow, 0,
      0, InitialVideoWidth, InitialVideoHeight, 0, CopyFromParent,
      InputOutput, CopyFromParent, 0, NULL);

  PD.CursorPane = XCreateWindow(PD.disp,
                                PD.MainPane,
                                0, 0,
                                32, InitialVideoHeight,
                                0, /* Border width */
                                CopyFromParent, /* depth */
                                InputOutput, /* class */
                                CopyFromParent, /* visual */
                                0, /* valuemask */
                                NULL /* attribs */);



    /* Allocate the memory for the actual display image */
    /* TODO!! Need to allocate more for truecolour */
    PD.ImageData = emalloc((InitialVideoWidth + 32) *
        InitialVideoHeight * 4, "host screen image memory");
  PD.DisplayImage = XCreateImage(PD.disp, DefaultVisual(PD.disp, PD.ScreenNum),
                                 PD.visInfo.depth, ZPixmap, 0, PD.ImageData,
                                 InitialVideoWidth, InitialVideoHeight, 32,
                                 0);
    insist(!!PD.DisplayImage, "creating host screen image");

    /* Now the same for the cursor image */
    PD.CursorImageData = emalloc(64 * InitialVideoHeight * 4,
        "host cursor image memory");
  PD.CursorImage = XCreateImage(PD.disp, DefaultVisual(PD.disp, PD.ScreenNum),
                                PD.visInfo.depth, ZPixmap, 0, PD.CursorImageData,
                                32, InitialVideoHeight, 32,
                                0);
    insist(!!PD.CursorImage, "creating host cursor image");

  XSelectInput(PD.disp,PD.MainPane,ExposureMask |
                                   PointerMotionMask |
                                   EnterWindowMask | /* For changing colour maps */
                                   LeaveWindowMask | /* For changing colour maps */
                                   KeyPressMask |
                                   KeyReleaseMask |
                                   ButtonPressMask |
                                   ButtonReleaseMask);
  XSelectInput(PD.disp,PD.CursorPane,ExposureMask |
                                   FocusChangeMask |
                                   KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
  PD.DefaultColormap=DefaultColormapOfScreen(PD.xScreen);

    {
        typedef struct {
            const char *name;
            XColor *dest;
            XColor *reserve;
        } desired_colour;
        static desired_colour desired[] = {
            { "white", &PD.White, NULL },
            { "black", &PD.Black, NULL },
            { "red", &PD.Red, NULL },
            { "green", &PD.Green, NULL },
            { "gray10", &PD.GreyDark, NULL },
            { "gray90", &PD.GreyLight, NULL },
            { "PapayaWhip", &PD.OffWhite, &PD.White },
        };
        desired_colour *d;
        XColor discard;

        for (d = desired; d < END(desired); d++) {
            if (!XAllocNamedColor(PD.disp, PD.DefaultColormap, d->name,
                d->dest, &discard)) {
                fprintf(stderr, "arcem: failed to allocate colour %s\n",
                    d->name);
                if (d->reserve) {
                    *d->dest = *d->reserve;
                }
            }
        }
    }

  /* I think the main monitor window will need its own colourmap
     since we need at least 256 colours for 256 colour mode */
  if (PD.visInfo.class==PseudoColor) {

    for(prescol=0; prescol<256; prescol++) {
      /* For pseudocolour, we use a fixed palette that matches the default
         RISC OS 256 colour one. This isn't ideal but it should work OK with
         most programs, even stuff that relies on mid-frame palette swaps. */
      int tint = prescol & 0x3;
      int r = prescol & 0xc;
      int g = (prescol & 0x30)>>2;
      int b = (prescol & 0xc0)>>4;
      store_colour(PD.ArcsColormap,prescol,(r|tint)*0x1111,(g|tint)*0x1111,(b|tint)*0x1111);
    };

    XSetWindowColormap(PD.disp,PD.MainPane,PD.ArcsColormap);
    /* The following seems to be necessary to get the colourmap to switch -
    but I don't understand why! */
    XSetWindowColormap(PD.disp,PD.BackingWindow,PD.ArcsColormap);

    /* I think is to get the window manager to automatically set the colourmap ... */
    XSetWMColormapWindows(PD.disp,PD.BackingWindow,&(PD.MainPane),1);
  } else {
    /* TrueColor - the colourmap is actually a fixed thing */
  };

  PD.MainPaneGC = XCreateGC(PD.disp, PD.MainPane, 0, NULL);

    PD.ShapePixmapData = ecalloc(32 * InitialVideoHeight,
        "host cursor shape mask");
  /* Shape stuff for the mouse cursor window */
  if (!XShapeQueryExtension(PD.disp, &shape_event_base, &shape_error_base)) {
    PD.ShapeEnabled = 0;
    PD.shape_mask = 0;
  } else {
    PD.ShapeEnabled = 1;

    PD.shape_mask = XCreatePixmapFromBitmapData(PD.disp, PD.CursorPane, PD.ShapePixmapData,
                                                32, InitialVideoHeight, 0, 1, 1);
    /* Eek - a lot of this is copied from XEyes - unfortunatly the manual
       page for this call is somewhat lacking.  To quote its bugs entry:
      'This manual page needs a lot more work' */
    XShapeCombineMask(PD.disp, PD.CursorPane, ShapeBounding, 0, 0, PD.shape_mask,
                      ShapeSet);
    XFreePixmap(PD.disp, PD.shape_mask);
  };

  XMapWindow(PD.disp,PD.BackingWindow);
  XMapSubwindows(PD.disp,PD.BackingWindow);
  XMapSubwindows(PD.disp,PD.MainPane);

  ControlPane_Init(state);

  if(PD.visInfo.class==PseudoColor)
    return DisplayDev_Set(state,&pseudo_DisplayDev);
  else
    return DisplayDev_Set(state,&true_DisplayDev);
} /* DisplayKbd_InitHost */


/*----------------------------------------------------------------------------*/

static void BackingWindow_Event(ARMul_State *state,XEvent *e) {
    fprintf(stderr, "unwanted BackingWindow_Event type=%d\n", e->type);
} /* BackingWindow_Event */

/* ------------------------------------------------------------------ */

/* All X servers will support a cursor of 16 x 16 bits. */
#define CURSOR_SIZE 16

static void ProcessKey(ARMul_State *state, XKeyEvent *key)
{
    KeySym sym;
    static Cursor blank_cursor;
    const keysym_to_arch_key *ktak;
    char *name;

    XLookupString(key, NULL, 0, &sym, NULL);

    if (sym == mouse_key.keysym) {
        if (key->type != KeyPress) {
            return;
        }

        PD.DoingMouseFollow = !PD.DoingMouseFollow;
        fprintf(stderr, "%s pressed, turning mouse tracking %s.\n",
            mouse_key.name, PD.DoingMouseFollow ? "on" : "off");

        if (!blank_cursor) {
            Status s;
            XColor black, exact;
            char *bits;
            Pixmap bm;

            s = XAllocNamedColor(PD.disp, PD.ArcsColormap, "black",
                &black, &exact);
            insist(s, "couldn't find black for cursor");

            bits = ecalloc(CURSOR_SIZE * CURSOR_SIZE / 8, "blank cursor");
            bm = XCreateBitmapFromData(PD.disp, PD.MainPane, bits,
                CURSOR_SIZE, CURSOR_SIZE);
            insist(bm, "couldn't create bitmap for cursor");
            free(bits);

            blank_cursor = XCreatePixmapCursor(PD.disp, bm, bm, &black,
                &black, 0, 0);
            insist(blank_cursor, "couldn't create blank cursor");
        }

        XDefineCursor(PD.disp, PD.MainPane,
            PD.DoingMouseFollow ? blank_cursor : None);
        return;
    }

    /* Just take the unshifted version of the key */
    sym = XLookupKeysym(key, 0);

    for (ktak = keysym_to_arch_key_map; ktak->sym; ktak++) {
        if (ktak->sym == sym) {
            keyboard_key_changed(&KBD, ktak->kid, key->type == KeyRelease);
            return;
        }
    }

    name = XKeysymToString(sym);
    fprintf(stderr, "ProcessKey: unknown key: keycode=%u "
        "keysym=%lu=\"%s\"\n", key->keycode, sym, name ? name : "unknown");
}

/* ------------------------------------------------------------------ */

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
/* Move the cursor window                                                     */

void UpdateCursorPos(ARMul_State *state,int xscale,int xoffset,int yscale,int yoffset) {
  int HorizPos;
  int VertPos;
  int Height = (int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart;

  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
  HorizPos = HorizPos*xscale+xoffset;
  VertPos = VertPos*yscale+yoffset;

  if (Height < 1)
    Height = 1;

  if (VertPos < 0)
    VertPos = 0;

#ifdef DEBUG_CURSOR
  fprintf(stderr,"UpdateCursorPos: Height=%d VertPos=%d HorizPos=%d\n",Height,VertPos,HorizPos);
#endif
  XMoveResizeWindow(PD.disp, PD.CursorPane, HorizPos, VertPos, 32, Height);
} /* UpdateCursorPos */


/*----------------------------------------------------------------------------*/
/* Called on an X motion event when we are in mouse tracking mode */

static void MouseMoved(ARMul_State *state,XMotionEvent *xmotion)
{
  int xdiff, ydiff;

  if ( lastmousemode )
  {
    xdiff=(xmotion->x-lastmousex)*4;
    ydiff=(lastmousey-xmotion->y)*4;
    lastmousex=xmotion->x;
    lastmousey=xmotion->y;
  }
  else
  {
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
    XWarpPointer(PD.disp,
                 None,                               /* src window for relative coords (NOT USED) */
                 PD.MainPane,                        /* Destination window to move cursor in */
                 0, 0,                               /* relative coords (NOT USED) */
                 9999, 9999,                         /* src width and height (NOT USED) */
                 ScreenWidth / 2, ScreenHeight / 2); /* Coordinates in the destination window */

#ifdef DEBUG_MOUSEMOVEMENT
    fprintf(stderr,"MouseMoved: CursorStart=%d xmotion->x=%d\n",
            VIDC.Horiz_CursorStart,xmotion->x);
#endif
    xdiff = xmotion->x - ScreenWidth / 2;
    ydiff = ScreenHeight / 2 - xmotion->y;
  }

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
        lastmousex=e->xcrossing.x;
        lastmousey=e->xcrossing.y;
      break;

    case LeaveNotify:
      /*fprintf(stderr,"MainPane: Leave notify!\n"); */
        hostdisplay_change_focus(FALSE);
      break;

    case Expose:
      XPutImage(PD.disp,PD.MainPane,PD.MainPaneGC,PD.DisplayImage,
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
      if (PD.DoingMouseFollow) {
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
      break;

    case Expose:
      XPutImage(PD.disp,PD.CursorPane,PD.MainPaneGC,PD.CursorImage,
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


/* ------------------------------------------------------------------ */


void hostdisplay_change_focus(int focus)
{
  if (PD.visInfo.class == PseudoColor) {
    (*(focus ? XInstallColormap : XUninstallColormap))(PD.disp, PD.ArcsColormap);
  }
  (*(focus ? XAutoRepeatOff : XAutoRepeatOn))(PD.disp);
}

/*----------------------------------------------------------------------------*/

int
Kbd_PollHostKbd(ARMul_State *state)
{
  XEvent e;

  if (XCheckMaskEvent(PD.disp, ULONG_MAX, &e)) {
#ifdef DEBUG_X_PROTOCOL
    if (e.xany.window == PD.BackingWindow) {
      printf("backingwindow ");
    } else if (e.xany.window == PD.MainPane) {
      printf("mainpane ");
    } else if (e.xany.window == PD.ControlPane) {
      printf("controlpane ");
    } else if (e.xany.window == PD.CursorPane) {
      printf("cursorpane ");
    } else {
      printf("unknown window ");
    }
    printf("= %d\n", e.type);
#endif

    if (e.xany.window == PD.BackingWindow) {
      BackingWindow_Event(state, &e);
    } else if (e.xany.window == PD.MainPane) {
      MainPane_Event(state, &e);
    } else if (e.xany.window == PD.ControlPane) {
      ControlPane_Event(state, &e);
    } else if (e.xany.window == PD.CursorPane) {
      CursorPane_Event(state, &e);
    } else {
      ControlPane_Error(EXIT_FAILURE,"event on unknown window: %#lx %d\n",
              e.xany.window, e.type);
    }
  }
  return 0;
} /* DisplayKbd_PollHost */


/**
 * Resize_Window
 *
 * Resize the window to fit the new video mode.
 */
void Resize_Window(ARMul_State *state,int x,int y)
{
    /* resize outer window */
    XResizeWindow(PD.disp, PD.BackingWindow,
                  x,
                  y );

    /* resize inner window (which is probably redundant now it's the same size as the outer) */
    XResizeWindow(PD.disp, PD.MainPane, x, y);

    /* clean up previous images used as display and cursor */
    XDestroyImage(PD.DisplayImage);
    XDestroyImage(PD.CursorImage);
    free(PD.ShapePixmapData);

    /* realocate space for new screen image */
    PD.ImageData = emalloc(x * 4 * y, "host screen image memory");
    PD.DisplayImage = XCreateImage(PD.disp,
                                   DefaultVisual(PD.disp, PD.ScreenNum),
                                   PD.visInfo.depth, ZPixmap, 0,
                                   PD.ImageData,
                                   x, y, 32,
                                   0);
    insist(!!PD.DisplayImage, "creating host screen image");

    /* realocate space for new cursor image */
    PD.CursorImageData = emalloc(32 * 4 * y, "host cursor image memory");
    PD.CursorImage = XCreateImage(PD.disp,
                                  DefaultVisual(PD.disp, PD.ScreenNum),
                                  PD.visInfo.depth, ZPixmap, 0,
                                  PD.CursorImageData,
                                  32, y, 32,
                                  0);
    insist(!!PD.CursorImage, "creating host cursor image");

    PD.ShapePixmapData = ecalloc(32 * y, "host cursor shape mask");
}

/* ------------------------------------------------------------------ */

static void *emalloc(size_t n, const char *use)
{
    void *p;

    if ((p = malloc(n)) == NULL) {
        ControlPane_Error(1,"arcem: malloc of %zu bytes for %s failed.\n",
            n, use);
    }

    return p;
}

static void *ecalloc(size_t n, const char *use)
{
    void *p;

    p = emalloc(n, use);
    memset(p, 0, n);

    return p;
}

static void insist(int expr, const char *diag)
{
    if (expr) {
        return;
    }

    ControlPane_Error(1,"arcem: insisting on %s.\n", diag);
}
