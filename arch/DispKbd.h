/* Display and keyboard interface for the Arc emulator */
/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */

#ifndef DISPKBD_HEADER
#define DISPKBD_HEADER

#include "../armdefs.h"

#define POLLGAP 125

#define KBDBUFFLEN 128

typedef struct {
  int KeyColToSend, KeyRowToSend, KeyUpNDown;
} KbdEntry;


#if defined(SYSTEM_win) || defined(MACOSX)
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

struct DisplayInfo {
  struct {
    int MustRedraw;       /* Set to 1 by emulator if something major changes */
#if defined(SYSTEM_X)
    /* These three flags indicate VIDC's palette registers have
     * changed without the host's equivalent structure being updated
     * to match. */
    char video_palette_dirty;
    char border_palette_dirty;
    char cursor_palette_dirty;
#else
    int MustResetPalette; /* Set to 1 by emulator if something major changes */
#endif

    /* Matches the ones in the memory model - if they are different redraw */
    unsigned int UpdateFlags[(512*1024)/256];

    /* min and max values which need refereshing on the X display */
    int miny,maxy;

    int DoingMouseFollow; /* If true following the mouse */
  } Control;

  struct {
    unsigned int Palette[16];
    unsigned int BorderCol;
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

#if defined(WIN32) || defined(MACOSX)
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
#endif

  struct arch_keyboard {
    KbdStates KbdState;
    /* A signed, 7-bit value stored in an unsigned char as it gets
     * passed to keyboard transmission functions expecting an
     * unsigned char. */
    unsigned char MouseXCount;
    unsigned char MouseYCount;
    int KeyColToSend,KeyRowToSend,KeyUpNDown;
    int Leds;
    /* The bottom three bits of leds holds their current state.  If
     * the bit is set the LED should be emitting. */
    void (*leds_changed)(unsigned int leds);

    /* Double buffering - update the others while sending this */
    unsigned char MouseXToSend;
    unsigned char MouseYToSend;
    int MouseTransEnable,KeyScanEnable; /* When 1 allowed to transmit */
    int HostCommand;            /* Normally 0 else the command code */
    KbdEntry Buffer[KBDBUFFLEN];
    int BuffOcc;
    int TimerIntHasHappened;
  } Kbd;
};


#define DISPLAYINFO (*(state->Display))
#define VIDC (DISPLAYINFO.Vidc)
/* Use this in gdb: state->Display->HostDisplay */
#define HOSTDISPLAY (DISPLAYINFO.HostDisplay)
#define DISPLAYCONTROL (DISPLAYINFO.Control)
#define KBD (DISPLAYINFO.Kbd)


#define VideoRelUpdateAndForce(flag, writeto, from) \
{\
  if ((writeto) != (from)) { \
    (writeto) = (from);\
    flag = 1;\
  };\
};

/*----------------------------------------------------------------------------*/

/* Functions each Host must provide */
void DisplayKbd_InitHost(ARMul_State *state);
int DisplayKbd_PollHost(ARMul_State *state);
void VIDC_PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw);

#ifdef SYSTEM_X

/* Adjust to gaining or losing the keyboard and pointer focus. */
void hostdisplay_change_focus(int focus);

#endif

/* Functions defined in DispKbdShared.c */
void MarkAsUpdated(ARMul_State *state, int end);
int QueryRamChange(ARMul_State *state, unsigned int offset, int len);
void CopyScreenRAM(ARMul_State *state, unsigned int offset, int len, char *Buffer);
void DisplayKbd_Init(ARMul_State *state);

#endif
