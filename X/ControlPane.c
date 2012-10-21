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
#include "ControlPane.h"
#include "arch/displaydev.h"
#include "platform.h"
#include "arch/keyboard.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define CTRLPANEWIDTH 640
#define CTRLPANEHEIGHT 100

#define LEDHEIGHT 15
#define LEDWIDTH 15
#define LEDTOPS 90


/*-----------------------------------------------------------------------------*/

static void TextAt(ARMul_State *state, const char *Text, int x, int y) {
  XDrawImageString(PD.disp, PD.ControlPane, PD.ControlPaneGC, x, y,
                   Text, strlen(Text));
} /* TextAt */


/*----------------------------------------------------------------------------*/

static void DoLED(const char *Text, int On, int ty, int lx)
{
  XSetBackground(PD.disp, PD.ControlPaneGC, PD.OffWhite.pixel);
  XSetForeground(PD.disp, PD.ControlPaneGC, PD.Black.pixel);

  TextAt(NULL, Text, lx + LEDWIDTH + 2, ty);

  XSetForeground(PD.disp, PD.ControlPaneGC, PD.Black.pixel);
  XSetFillStyle(PD.disp, PD.ControlPaneGC, FillSolid);

  XDrawArc(PD.disp, PD.ControlPane, PD.ControlPaneGC, lx, ty-LEDHEIGHT, LEDWIDTH, LEDHEIGHT,
           0, 360*64);

  XSetForeground(PD.disp, PD.ControlPaneGC, (On?PD.Green:PD.GreyDark).pixel);
  XFillArc(PD.disp, PD.ControlPane, PD.ControlPaneGC, lx+2, ty+6-(LEDHEIGHT+4), LEDWIDTH-4, (LEDHEIGHT-4),
           0, 360*64);
} /* DoLED */


/*----------------------------------------------------------------------------*/

static void draw_keyboard_leds(uint8_t leds)
{
    DoLED("Caps Lock",   leds & KBD_LED_CAPSLOCK, LEDTOPS, 0);
    DoLED("Num Lock",    leds & KBD_LED_NUMLOCK, LEDTOPS, 90);
    DoLED("Scroll Lock", leds & KBD_LED_SCROLLLOCK, LEDTOPS, 180);
}


/*----------------------------------------------------------------------------*/

static void draw_floppy_leds(unsigned int leds)
{
    unsigned int floppy;
    char label[12];

    for (floppy = 0; floppy < 4; floppy++) {
      sprintf(label, "Floppy %d", floppy);
      DoLED(label, leds & (1 << floppy), LEDTOPS, 290 + floppy * 80);
    }
}


/* ------------------------------------------------------------------ */

static void insert_or_eject_floppy(int drive)
{
    static char got_disc[4];
    static char image[] = "FloppyImage#";
    const char *err;

    if (got_disc[drive]) {
        err = FDC_EjectFloppy(drive);
        fprintf(stderr, "ejecting drive %d: %s\n", drive,
            err ? err : "ok");
    } else {
        image[sizeof image - 2] = '0' + drive;
        err = FDC_InsertFloppy(drive, image);
        fprintf(stderr, "inserting floppy image %s into drive %d: %s\n",
            image, drive, err ? err : "ok");
    }

    got_disc[drive] ^= !err;
}




/*-----------------------------------------------------------------------------*/
/* Centres the text in the 'lx rx' box placing the text so that its top edge   */
/* is at 'ty'; it returns the position of the bottom of the text               */

static int TextCenteredH(ARMul_State *state, const char *Text, int ty, int lx, int rx) {
  int dirreturn, ascentret, descentret;
  XCharStruct overall;
  int x;

  XTextExtents(PD.ButtonFont, Text, strlen(Text),
     &dirreturn, &ascentret, &descentret, &overall);

  /* Move the text down so that its top is where the caller asked */
  ty += ascentret;
  x= (rx - lx) / 2;

  TextAt(state, Text, x - overall.width / 2, ty);

  return ty + descentret;
} /* TextCenteredH */


/*-----------------------------------------------------------------------------*/

static void ControlPane_Redraw(ARMul_State *state, XExposeEvent *e) {
  int y;

  XSetBackground(PD.disp, PD.ControlPaneGC, PD.OffWhite.pixel);
  XSetForeground(PD.disp, PD.ControlPaneGC, PD.Black.pixel);
  y = TextCenteredH(state, "Archimedes Emulation (c) 1995-1999 David Alan Gilbert", 0, 0,
                    CTRLPANEWIDTH);

  y += 2;
  y = TextCenteredH(state, "http://arcem.sf.net/", y, 0, CTRLPANEWIDTH);

  y+=2;
  XDrawLine(PD.disp, PD.ControlPane, PD.ControlPaneGC,
            0, y, CTRLPANEWIDTH-1, y);

  y += 2;
  y = TextCenteredH(state, "Type `q' to quit.", y, 0, CTRLPANEWIDTH);

  y += 2;
  y = TextCenteredH(state, "Type `0', `1', `2', or `3' to "
      "insert/eject floppy image.", y, 0, CTRLPANEWIDTH);

  y+=2;
  draw_keyboard_leds(KBD.Leds);
  draw_floppy_leds(~ioc.LatchA & 0xf);
} /* ControlPane_Redraw */


/*----------------------------------------------------------------------------*/

void ControlPane_Event(ARMul_State *state, XEvent *event) {
    KeySym sym;

  switch (event->type) {
    case KeyPress:
      XLookupString(&event->xkey, NULL, 0, &sym, NULL);

      if (sym >= XK_0 && sym <= XK_3) {
        insert_or_eject_floppy(sym - XK_0);

      } else if (sym == XK_q) {
        fputs("arcem: user requested exit\n", stderr);
        hostdisplay_change_focus(FALSE);
        exit(0);
      }
      break;

    case Expose:
      ControlPane_Redraw(state, &event->xexpose);
      break;

    default:
        fprintf(stderr, "unwanted ControlPane_Event type = %d\n", event->type);
      break;
  }
} /* ControlPane_Event */


/*----------------------------------------------------------------------------*/

void ControlPane_Init(ARMul_State *state) {
  XTextProperty name;
  char *tmpptr;
    int drive;

  PD.ControlPane = XCreateWindow(PD.disp,
                                 PD.RootWindow,  /* PD.BackingWindow, */
                                 0, 0,           /* above main pane */
                                 CTRLPANEWIDTH, CTRLPANEHEIGHT,
                                 0,              /* Border width */
                                 CopyFromParent,  /* depth */
                                 InputOutput,    /* class */
                                 CopyFromParent, /* visual */
                                 0,              /* valuemask */
                                 NULL            /* attribs */);
  tmpptr = strdup("Arc emulator - Control panel");

  if (XStringListToTextProperty(&tmpptr, 1, &name) == 0) {
    ControlPane_Error(1,"Could not allocate window name\n");
  }

  XSetWMName(PD.disp, PD.ControlPane, &name);
  XFree(name.value);

  PD.ControlPaneGC = XCreateGC(PD.disp, PD.ControlPane, 0, NULL);
  XCopyGC(PD.disp, DefaultGC(PD.disp, PD.ScreenNum), GCPlaneMask|GCSubwindowMode,
          PD.ControlPaneGC);

  PD.ButtonFont = XLoadQueryFont(PD.disp, "fixed");

  if (PD.ButtonFont == NULL) {
    ControlPane_Error(1,"Couldn't get font for buttons\n");
  }

  XSetFont(PD.disp, PD.ControlPaneGC, PD.ButtonFont->fid);

  XSetWindowBackground(PD.disp, PD.ControlPane, PD.OffWhite.pixel);

  XSelectInput(PD.disp, PD.ControlPane, KeyPressMask | ExposureMask);

  XMapWindow(PD.disp, PD.ControlPane);

  /* setup callbacks for each time various LEDs change */
  KBD.leds_changed = draw_keyboard_leds;
  FDC_SetLEDsChangeFunc(draw_floppy_leds);

  for (drive = 0; drive < 4; drive++) {
    insert_or_eject_floppy(drive);
  }
} /* ControlPane_Init */


/*----------------------------------------------------------------------------*/

void ControlPane_Error(int code,const char *fmt,...)
{
  va_list args;
  va_start(args,fmt);
  /* Log it */
  vfprintf(stderr,fmt,args);
  /* Quit */
  exit(code);
}



