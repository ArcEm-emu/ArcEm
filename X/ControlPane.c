/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */

#include "X11/X.h"
#include "X11/Xlib.h"
#include "X11/Xutil.h"
#include "X11/keysym.h"
#include "X11/extensions/shape.h"

#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "DispKbd.h"
#include "ControlPane.h"

#include <string.h>

#define CTRLPANEWIDTH 640
#define CTRLPANEHEIGHT 100

#define LEDHEIGHT 15
#define LEDWIDTH 15
#define LEDTOPS 90

/* HOSTDISPLAY is too verbose here - but HD could be hard disc somewhere else! */
#define HD HOSTDISPLAY
#define DC DISPLAYCONTROL

static void draw_keyboard_leds(int leds);
static void draw_floppy_leds(int leds);

static void insert_or_eject_floppy(int drive);

/*-----------------------------------------------------------------------------*/
static void TextAt(ARMul_State *state, const char *Text, int x, int y) {
  XDrawImageString(HD.disp,HD.ControlPane,HD.ControlPaneGC,x,y,
                   Text,strlen(Text));
}; /* TextAt */

/*-----------------------------------------------------------------------------*/
/* Centeres the text in the 'lx rx' box placing the text so that its top edge
   is at 'ty'; it returns the position of the bottom of the text               */
static int TextCenteredH(ARMul_State *state, const char *Text, int ty,int lx,int rx) {
  int dirreturn,ascentret,descentret;
  XCharStruct overall;
  int x;

  XTextExtents(HD.ButtonFont,Text,strlen(Text),
     &dirreturn,&ascentret,&descentret,&overall);

  /* Move the text down so that its top is where the caller asked */
  ty+=ascentret;

  x=(rx-lx)/2;

  x-=overall.width/2;

  TextAt(state,Text,x,ty);

  return(ty+descentret);
}; /* TextCenteredH */

/*----------------------------------------------------------------------------*/
static void DoLED(const char *Text, int On, int ty, int lx)
{
  XSetBackground(HD.disp,HD.ControlPaneGC,HD.OffWhite.pixel);
  XSetForeground(HD.disp,HD.ControlPaneGC,HD.Black.pixel);

    TextAt(NULL, Text, lx + LEDWIDTH + 2, ty);

  XSetForeground(HD.disp,HD.ControlPaneGC,HD.Black.pixel);
  XSetFillStyle(HD.disp,HD.ControlPaneGC,FillSolid);

  XDrawArc(HD.disp,HD.ControlPane,HD.ControlPaneGC,lx,ty-LEDHEIGHT,LEDWIDTH,LEDHEIGHT,
           0,360*64);

  XSetForeground(HD.disp,HD.ControlPaneGC,(On?HD.Green:HD.GreyDark).pixel);
  XFillArc(HD.disp,HD.ControlPane,HD.ControlPaneGC,lx+2,ty+6-(LEDHEIGHT+4),LEDWIDTH-4,(LEDHEIGHT-4),
           0,360*64);
}; /* DoLED */

/*-----------------------------------------------------------------------------*/
static void ControlPane_Redraw(ARMul_State *state,XExposeEvent *e) {
  int y;
  XSetBackground(HD.disp,HD.ControlPaneGC,HD.OffWhite.pixel);
  XSetForeground(HD.disp,HD.ControlPaneGC,HD.Black.pixel);
  y=TextCenteredH(state,"Archimedes Emulation (c) 1995-1999 David Alan Gilbert",0,0,
                CTRLPANEWIDTH);

    y += 2;
    y = TextCenteredH(state, "http://arcem.sf.net/", y, 0, CTRLPANEWIDTH);

  y+=2;
  XDrawLine(HD.disp,HD.ControlPane,HD.ControlPaneGC,
            0,y,CTRLPANEWIDTH-1,y);

    y += 2;
    y = TextCenteredH(state, "Type `q' to quit.", y, 0, CTRLPANEWIDTH);

    y += 2;
    y = TextCenteredH(state, "Type `0', `1', `2', or `3' to "
        "insert/eject floppy image.", y, 0, CTRLPANEWIDTH);

  y+=2;
    draw_keyboard_leds(KBD.Leds);
    draw_floppy_leds(~ioc.LatchA & 0xf);
}; /* ControlPane_Redraw */

/*----------------------------------------------------------------------------*/
void ControlPane_Event(ARMul_State *state,XEvent *e) {
    KeySym sym;

  switch (e->type) {
    case KeyPress:
        XLookupString(&e->xkey, NULL, 0, &sym, NULL);
        switch (sym) {
        case XK_0:
            insert_or_eject_floppy(0);
            break;
        case XK_1:
            insert_or_eject_floppy(1);
            break;
        case XK_2:
            insert_or_eject_floppy(2);
            break;
        case XK_3:
            insert_or_eject_floppy(3);
            break;
        case XK_q:
            fputs("arcem: user requested exit\n", stderr);
            hostdisplay_change_focus(&HD, FALSE);
            exit(0);
        }
        break;
    case Expose:
      ControlPane_Redraw(state,&(e->xexpose));
      break;

    default:
        fprintf(stderr, "unwanted ControlPane_Event type=%d\n", e->type);
      break;
  };
}; /* ControlPane_Event */

/*----------------------------------------------------------------------------*/
void ControlPane_Init(ARMul_State *state) {
  XTextProperty name;
  char *tmpptr;
    int drive;

  HD.ControlPane=XCreateWindow(HD.disp,
                               HD.RootWindow, /* HD.BackingWindow, */
                                     0,0, /* above main pane */
                                     CTRLPANEWIDTH,CTRLPANEHEIGHT,
                                     0, /* Border width */
                                     CopyFromParent, /* depth */
                                     InputOutput, /* class */
                                     CopyFromParent, /* visual */
                                     0, /* valuemask */
                                     NULL /* attribs */);
  tmpptr = strdup("Arc emulator - Control panel");
  if (XStringListToTextProperty(&tmpptr,1,&name)==0) {
    fprintf(stderr,"Could not allocate window name\n");
    exit(1);
  };
  XSetWMName(HD.disp,HD.ControlPane,&name);
  XFree(name.value);

    HD.ControlPaneGC = XCreateGC(HD.disp, HD.ControlPane, 0, NULL);
  XCopyGC(HD.disp,DefaultGC(HD.disp,HD.ScreenNum),GCPlaneMask|GCSubwindowMode,
          HD.ControlPaneGC);

    HD.ButtonFont = XLoadQueryFont(HD.disp, "fixed");
  if (HD.ButtonFont==NULL) {
    fprintf(stderr,"Couldn't get font for buttons\n");
    exit(1);
  };
  XSetFont(HD.disp,HD.ControlPaneGC,HD.ButtonFont->fid);

  XSetWindowBackground(HD.disp,HD.ControlPane,HD.OffWhite.pixel);

    XSelectInput(HD.disp, HD.ControlPane, KeyPressMask | ExposureMask);

  XMapWindow(HD.disp,HD.ControlPane);

    KBD.leds_changed = draw_keyboard_leds;
    FDC.leds_changed = draw_floppy_leds;

    for (drive = 0; drive < 4; drive++) {
        insert_or_eject_floppy(drive);
    }

    return;
}; /* ControlPane_Init */

/*----------------------------------------------------------------------------*/

static void draw_keyboard_leds(int leds)
{
    DoLED("Caps Lock", leds & 1, LEDTOPS, 0);
    DoLED("Num Lock", leds & 2, LEDTOPS, 90);
    DoLED("Scroll Lock", leds & 4, LEDTOPS, 180);

    return;
}

/*----------------------------------------------------------------------------*/

static void draw_floppy_leds(int leds)
{
    int i;
    char s[32];

    for (i = 0; i < 4; i++) {
        sprintf(s, "Floppy %d", i);
        DoLED(s, leds & 1 << i, LEDTOPS, 290 + i * 80);
    }

    return;
}

/* ------------------------------------------------------------------ */

static void insert_or_eject_floppy(int drive)
{
    static char got_disc[4];
    static char image[] = "FloppyImage#";
    char *err;

    if (got_disc[drive]) {
        err = fdc_eject_floppy(drive);
        fprintf(stderr, "ejecting drive %d: %s\n", drive,
            err ? err : "ok");
    } else {
        image[sizeof image - 2] = '0' + drive;
        err = fdc_insert_floppy(drive, image);
        fprintf(stderr, "inserting floppy image %s into drive %d: %s\n",
            image, drive, err ? err : "ok");
    }

    got_disc[drive] ^= !err;

    return;
}
