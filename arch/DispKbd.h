/* Display and keyboard interface for the Arc emulator */
/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */
#ifndef DISPKBD_HEADER
#define DISPKBD_HEADER

#include "../armdefs.h"
#include "../armopts.h"

#if !defined(__riscos__) && !defined(__CYGWIN__)
#include "X11/Xlib.h"
#include "X11/Xutil.h"
#endif

#define KBDBUFFLEN 128

typedef struct {
  int KeyColToSend,KeyRowToSend,KeyUpNDown;
} KbdEntry;


#ifdef SYSTEM_win
typedef struct {
  unsigned int red, green, blue;
} XColor;
#endif

typedef enum {
  KbdState_JustStarted,
  KbdState_SentHardReset,
  KbdState_SentAck1,
  KbdState_SentAck2,
  KbdState_SentKeyByte1, /* and waiting for ack */
  KbdState_SentKeyByte2, /* and waiting for ack */
  KbdState_SentMouseByte1,
  KbdState_SentMouseByte2,
  KbdState_Idle
} KbdStates;

typedef struct {
  struct {
    int MustRedraw;       /* Set to 1 by emulator if something major changes */
    int MustResetPalette; /* Set to 1 by emulator if palette is changed      */
    int PollCount;
    int AutoRefresh;

    /* Matches the ones in the memory model - if they are different redraw */
    unsigned int UpdateFlags[(512*1024)/256];

    /* min and max values which need refereshing on the X display */
    int miny,maxy; 

    int DoingMouseFollow; /* If true following the mouse */
  } Control;

  struct {
    unsigned int Palette[16];
    unsigned int BorderCol:14;
    unsigned int CursorPalette[3];
    unsigned int Horiz_Cycle;
    unsigned int Horiz_SyncWidth;
    unsigned int Horiz_BorderStart;
    unsigned int Horiz_DisplayStart; /* In 2 pixel units */
    unsigned int Horiz_DisplayEnd;   /* In 2 pixel units */
    unsigned int Horiz_BorderEnd;
    unsigned int Horiz_CursorStart;
    unsigned int Horiz_Interlace;
    unsigned int Vert_Cycle;
    unsigned int Vert_SyncWidth;
    unsigned int Vert_BorderStart;
    unsigned int Vert_DisplayStart;
    unsigned int Vert_DisplayEnd;
    unsigned int Vert_BorderEnd;
    unsigned int Vert_CursorStart;
    unsigned int Vert_CursorEnd;
    unsigned int SoundFreq;
    unsigned int ControlReg;
    unsigned int StereoImageReg[8];
  } Vidc;

#ifdef __CYGWIN__
  struct {
    char *ImageData,*CursorImageData;

    /* Map from host memory contents to 'pixel' value for putpixel */
    unsigned long pixelMap[256]; 

    /* Map from host memory contents to 'pixel' value for putpixel in cursor*/
    unsigned long cursorPixelMap[4]; 
    int red_shift,red_prec;
    int green_shift,green_prec;
    int blue_shift,blue_prec;
  } HostDisplay;

#else

#ifdef SYSTEM_X
  struct {
    Window RootWindow,BackingWindow,MainPane,ControlPane,CursorPane;
    Display *disp;
    Screen *xScreen;
    int ScreenNum;
    XVisualInfo visInfo;
    XImage *DisplayImage,*CursorImage;
    char *ImageData,*CursorImageData;
    Colormap DefaultColormap;
    Colormap ArcsColormap;
    GC MainPaneGC;
    GC ControlPaneGC;
    XColor White,Black,Red,Green,GreyDark,GreyLight,OffWhite;
    XFontStruct *ButtonFont;

    /* Stuff for shape masking of the pointer based on XEyes */
    Pixmap shape_mask; /* window shape */
    int ShapeEnabled;      /* Yep - we are using shapes */
    char *ShapePixmapData; /* This is what we use to create the pixmap */
    unsigned long pixelMap[256]; /* Map from host memory contents to 'pixel' value for putpixel */
    unsigned long cursorPixelMap[4]; /* Map from host memory contents to 'pixel' value for putpixel in cursor*/
    int red_shift,red_prec;
    int green_shift,green_prec;
    int blue_shift,blue_prec;
  } HostDisplay;
#endif

#endif

  struct {
    KbdStates KbdState;
    int MouseXCount,MouseYCount;
    int KeyColToSend,KeyRowToSend,KeyUpNDown;
    int Leds;

    /* Double buffering - update the others while sending this */
    int MouseXToSend,MouseYToSend; 
    int MouseTransEnable,KeyScanEnable; /* When 1 allowed to transmit */
    int HostCommand;            /* Normally 0 else the command code */
    KbdEntry Buffer[KBDBUFFLEN];
    int BuffOcc;
    int TimerIntHasHappened;
  } Kbd;
} DisplayInfo;

#define DISPLAYINFO (PRIVD->Display)
#define VIDC (DISPLAYINFO.Vidc)
/* Use this in gdb: ((PrivateDataType *)state->MemDataPtr)->Display->HostDisplay */
#define HOSTDISPLAY (DISPLAYINFO.HostDisplay)
#define DISPLAYCONTROL (DISPLAYINFO.Control)
#define KBD (DISPLAYINFO.Kbd)

#define VideoRelUpdateAndForce(FLAG,WRITETO,FROM) {\
                                               if ((WRITETO)!=(FROM)) { \
                                                 (WRITETO)=(FROM);\
                                                 FLAG=1;\
                                               };\
                                             };

/*----------------------------------------------------------------------------*/

unsigned int DisplayKbd_XPoll(void *data);
void DisplayKbd_Init(ARMul_State *state);
void VIDC_PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw);


#endif
