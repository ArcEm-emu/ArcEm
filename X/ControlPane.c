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
#define LEDTOPS 70

/* HOSTDISPLAY is too verbose here - but HD could be hard disc somewhere else! */
#define HD HOSTDISPLAY
#define DC DISPLAYCONTROL

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
static void DoLED(ARMul_State *state, const char *Text,int On,int ty, int lx) {
  XSetBackground(HD.disp,HD.ControlPaneGC,HD.OffWhite.pixel);
  XSetForeground(HD.disp,HD.ControlPaneGC,HD.Black.pixel);

  TextAt(state,Text,lx+LEDWIDTH+2,ty);

  XSetForeground(HD.disp,HD.ControlPaneGC,HD.Black.pixel);
  XSetFillStyle(HD.disp,HD.ControlPaneGC,FillSolid);

  XDrawArc(HD.disp,HD.ControlPane,HD.ControlPaneGC,lx,ty-LEDHEIGHT,LEDWIDTH,LEDHEIGHT,
           0,360*64);

  XSetForeground(HD.disp,HD.ControlPaneGC,(On?HD.Green:HD.GreyDark).pixel);
  XFillArc(HD.disp,HD.ControlPane,HD.ControlPaneGC,lx+2,ty+6-(LEDHEIGHT+4),LEDWIDTH-4,(LEDHEIGHT-4),
           0,360*64);
}; /* DoLED */

/*----------------------------------------------------------------------------*/
void ControlPane_RedrawLeds(ARMul_State *state) {
  int Drive;
  char Temp[32];

  DoLED(state,"Caps Lock",KBD.Leds & 1,LEDTOPS,0);
  DoLED(state,"Num Lock",KBD.Leds & 2,LEDTOPS,90);
  DoLED(state,"Scroll Lock",KBD.Leds & 4,LEDTOPS,180);

  for(Drive=3;Drive>=0;Drive--) {
    sprintf(Temp,"Floppy %d",Drive);

    DoLED(state,Temp,!(ioc.LatchA & (1<<Drive)),LEDTOPS,290+Drive*80);
  };

}; /* DoAllLEDs */

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

  y+=2;
  ControlPane_RedrawLeds(state);

}; /* ControlPane_Redraw */

/*----------------------------------------------------------------------------*/
void ControlPane_Event(ARMul_State *state,XEvent *e) {
    KeySym sym;

  switch (e->type) {
    case KeyPress:
        XLookupString(&e->xkey, NULL, 0, &sym, NULL);
        if (sym == XK_q) {
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
  XGCValues gctmpval;
  XTextProperty name;
  char *tmpptr;

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

  gctmpval.function=GXcopy;
  HD.ControlPaneGC=XCreateGC(HD.disp,HD.ControlPane,GCFunction,&gctmpval);
  XCopyGC(HD.disp,DefaultGC(HD.disp,HD.ScreenNum),GCPlaneMask|GCSubwindowMode,
          HD.ControlPaneGC);

  HD.ButtonFont=XLoadQueryFont(HD.disp,"-*-lucida-bold-r-*-*-*-120-*-*-*-*-*-*");
  if (HD.ButtonFont==NULL) {
    fprintf(stderr,"Couldn't get font for buttons\n");
    exit(1);
  };
  XSetFont(HD.disp,HD.ControlPaneGC,HD.ButtonFont->fid);

  XSetWindowBackground(HD.disp,HD.ControlPane,HD.OffWhite.pixel);

  XSelectInput(HD.disp,HD.ControlPane,ExposureMask |
                                   KeyPressMask | KeyReleaseMask);

  XMapWindow(HD.disp,HD.ControlPane);
}; /* ControlPane_Init */

