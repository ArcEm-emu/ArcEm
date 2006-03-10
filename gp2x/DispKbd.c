/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */
/* Display and keyboard interface for the Arc emulator */

// #define MOUSEKEY XK_KP_Add

/*#define DEBUG_VIDCREGS*/
/* NOTE: Can't use ARMul's refresh function because it has a small
   limit on the time delay from posting the event to it executing   */
/* It's actually decremented once every POLLGAP - that is called
   with the ARMul scheduler */

#define AUTOREFRESHPOLL 2500

#include <stdio.h>
#include <limits.h>

#include "../armdefs.h"
#include "armarc.h"
#include "arch/keyboard.h"
#include "DispKbd.h"
#include "archio.h"
#include "hdc63463.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>

#define ControlHeight 30
#define CURSORCOLBASE 250

#define KEY_PAD_UP          (1<<0)
#define KEY_PAD_UP_LEFT     (1<<1)
#define KEY_PAD_LEFT        (1<<2)
#define KEY_PAD_DOWN_LEFT   (1<<3)
#define KEY_PAD_DOWN        (1<<4)
#define KEY_PAD_DOWN_RIGHT  (1<<5)
#define KEY_PAD_RIGHT       (1<<6)
#define KEY_PAD_UP_RIGHT    (1<<7)
#define KEY_START           (1<<8)
#define KEY_SELECT          (1<<9)
#define KEY_LEFT_SHOULDER   (1<<10)
#define KEY_RIGHT_SHOULDER  (1<<11)
#define KEY_A               (1<<12)
#define KEY_B               (1<<13)
#define KEY_X               (1<<14)
#define KEY_Y               (1<<15)
#define KEY_VOL_UP          (1<<16)
#define KEY_VOL_DOWN        (1<<17)
#define KEY_PAD_IN          (1<<18)
static struct
{
    unsigned long keys;
    unsigned long changed;
    int           fd;
}
joypad;

#define SK(x) { ARCH_KEY_shift_l, ARCH_KEY_ ## x }
#define NK(x) { -1, ARCH_KEY_ ## x }

int KeyLookup[95][2]={
       NK(space),
       SK(1),
       SK(2),
       SK(3),
       SK(4),
       SK(5),
       SK(7),
       NK(apostrophe),
       SK(9),
       SK(0),
       SK(8),
       SK(equal),
       NK(comma),
       NK(minus),
       NK(period),
       NK(slash),
       NK(0),
       NK(1),
       NK(2),
       NK(3),
       NK(4),
       NK(5),
       NK(6),
       NK(7),
       NK(8),
       NK(9),
       SK(semicolon),
       NK(semicolon),
       SK(comma),
       NK(equal),
       SK(period),
       SK(slash),
       SK(apostrophe),
       SK(a),
       SK(b),
       SK(c),
       SK(d),
       SK(e),
       SK(f),
       SK(g),
       SK(h),
       SK(i),
       SK(j),
       SK(k),
       SK(l),
       SK(m),
       SK(n),
       SK(o),
       SK(p),
       SK(q),
       SK(r),
       SK(s),
       SK(t),
       SK(u),
       SK(v),
       SK(w),
       SK(x),
       SK(y),
       SK(z),
       NK(bracket_l),
       NK(backslash),
       NK(bracket_r),
       SK(6),
       SK(minus),
       NK(grave),
       NK(a),
       NK(b),
       NK(c),
       NK(d),
       NK(e),
       NK(f),
       NK(g),
       NK(h),
       NK(i),
       NK(j),
       NK(k),
       NK(l),
       NK(m),
       NK(n),
       NK(o),
       NK(p),
       NK(q),
       NK(r),
       NK(s),
       NK(t),
       NK(u),
       NK(v),
       NK(w),
       NK(x),
       NK(y),
       NK(z),
       SK(bracket_l),
       SK(backslash),
       SK(bracket_r),
       SK(grave)
};



/* HOSTDISPLAY is too verbose here - but HD could be hard disc somewhere else! */
/*#define HD HOSTDISPLAY*/
#define DC DISPLAYCONTROL

static void UpdateCursorPos(ARMul_State *state);
static void SelectScreenMode(int x, int y, int bpp);

static void set_cursor_palette(unsigned int *pal);

static int MonitorWidth;
static int MonitorHeight;
int MonitorBpp;

static struct termios savedTerminalSettings;

int screenfd=0;
int mmsp2fd=0;
int *DirectScreenMemory;
int DirectScreenExtent = 512*1024;
volatile unsigned short *mmsp2;
volatile unsigned short mmsp2pal[512];
unsigned long mmsp2cursorpal[2];

#define MMSP2(reg) mmsp2[(reg)>>1]
#define MMSP2_32(reg,val) mmsp2[(reg)>>1]=(val)&0xffff, mmsp2[((reg)+2)>>1]=(val)>>16
#define MMSP2SETPAL(c,r,g,b) mmsp2pal[(c*2)]=((b)|((g)<<8)), mmsp2pal[(c*2)+1]=(r)
#define MMSP2SETCURSORPAL(c,r,g,b) MMSP2(0x2924+(c*4))=((r)|((g)<<8)), MMSP2(0x2924+(c*4)+2)=(b)

/*-----------------------------------------------------------------------------*/
/* Write colour palette to MMSP2 hardware in one shot */
void mmsp2writepal( void )
{
  int i;
  MMSP2(0x2958)=0;
  for(i=0;i<512;i++)
      MMSP2(0x295A)=mmsp2pal[i];
}

/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the standard 1 bpp modes                        */
static void DoColourMap_2(ARMul_State *state) {
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

/* MMSP2 DOES NOT HAVE 1BPP SUPPORT! */
  for(c=0;c<2;c++)
  {
    MMSP2SETPAL( c,
                 (VIDC.Palette[c] & 15)*17,
                 ((VIDC.Palette[c]>>4) & 15)*17,
                 ((VIDC.Palette[c]>>8) & 15)*17
               );
  };

  mmsp2writepal();

  set_cursor_palette(VIDC.CursorPalette);

  DC.MustResetPalette=0;
}; /* DoColourMap_2 */


/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the standard 2 bpp modes                        */
static void DoColourMap_4(ARMul_State *state) {
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

/* MMSP2 DOES NOT HAVE 2BPP SUPPORT! */
  for(c=0;c<4;c++)
  {
    MMSP2SETPAL( c,
                 (VIDC.Palette[c] & 15)*17,
                 ((VIDC.Palette[c]>>4) & 15)*17,
                 ((VIDC.Palette[c]>>8) & 15)*17
               );
  };

  mmsp2writepal();

  set_cursor_palette(VIDC.CursorPalette);

  DC.MustResetPalette=0;
}; /* DoColourMap_4 */


/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the standard 4 bpp modes                        */
static void DoColourMap_16(ARMul_State *state) {
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<16;c++)
  {
    MMSP2SETPAL( c,
                 (VIDC.Palette[c] & 15)*17,
                 ((VIDC.Palette[c]>>4) & 15)*17,
                 ((VIDC.Palette[c]>>8) & 15)*17
               );
  };

  mmsp2writepal();

  set_cursor_palette(VIDC.CursorPalette);

  DC.MustResetPalette=0;
}; /* DoColourMap_16 */


/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the 8bpp modes                                  */
static void DoColourMap_256(ARMul_State *state) {
    int c;
    int l4;
    int l65;
    int l7;
    unsigned int pal;
    int r, g, b;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;


    for (c = 0; c < 256; c++)
    {
        l4 = c >> 1 & 8;
        l65 = c >> 3 & 0xc;
        l7 = c >> 4 & 8;

        pal = VIDC.Palette[c & 0xf];
        r = l4 | (pal & 7);
        g = l65 | (pal >> 4 & 3);
        b = l7 | (pal >> 8 & 7);

        MMSP2SETPAL( c, r*17, g*17, b*17 );
    }

  mmsp2writepal();

  set_cursor_palette(VIDC.CursorPalette);

  DC.MustResetPalette=0;
}; /* DoColourMap_Standard */

/* ------------------------------------------------------------------ */

static void set_cursor_palette(unsigned int *pal)
{
    int c;
    for(c = 0; c < 2; c++)  /* Should be 0..3 but gp2x h/w cursor is only 2 colour */
    {
        MMSP2SETCURSORPAL( c,
                           (pal[c] & 0xf) * 17,
                           (pal[c] >> 4 & 0xf) * 17,
                           (pal[c] >> 8 & 0xf) * 17
                         );
    }
}

/* ------------------------------------------------------------------ */

/* Refresh the mouse's image                                                    */
static void RefreshMouse(ARMul_State *state) {
/*imj
  int x, y, height, offset;
  int memptr, TransBit;
  char *ImgPtr, *TransPtr;

  offset   = 0;
  memptr   = MEMC.Cinit * 16;
  height   = (VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart) + 1;
  ImgPtr   = HD.CursorImageData;
  TransPtr = HD.ShapePixmapData;
  TransBit = 0;

  for(y=0; y<height; y++,memptr+=8,offset+=8,TransPtr+=4)
  {
    if (offset < 512*1024)
    {
      ARMword tmp[2];

      tmp[0] = MEMC.PhysRam[memptr/4];
      tmp[1] = MEMC.PhysRam[memptr/4+1];

      for(x=0; x<32; x++,ImgPtr++,TransBit<<=1)
      {
        *ImgPtr = CURSORCOLBASE + -1 + ((tmp[x / 16] >> ((x & 15) * 2)) & 3);
        if ((x & 7) == 0)
        {
          TransPtr[x / 8] = 0xff;
          TransBit = 1;
        }
        TransPtr[x / 8] &= ((tmp[x / 16]>>((x & 15) * 2)) & 3) ? ~TransBit : 0xff;
      }
    } else
    {
      return;
    }
  }
*/

  UpdateCursorPos(state);

}; /* RefreshMouse */



void
RefreshDisplay(ARMul_State *state)
{
  DC.AutoRefresh=AUTOREFRESHPOLL;
  ioc.IRQStatus|=8; /* VSync */
  ioc.IRQStatus |= 0x20; /* Sound - just an experiment */
  IO_UpdateNirq();

  DC.miny=MonitorHeight-1;
  DC.maxy=0;

static int hackdelay = 1;

  if ( --hackdelay )
    return;
  hackdelay = 20;

  RefreshMouse(state);

  /* Figure out number of BPP */
  switch ((VIDC.ControlReg & 0xc)>>2)
  {
    case 0:
      DoColourMap_2(state);
      break;
    case 1:
      DoColourMap_4(state);
      break;
    case 2:
      DoColourMap_16(state);
      break;
    case 3:
      DoColourMap_256(state);
      break;
  }

  /*fprintf(stderr,"RefreshDisplay: Refreshed %d-%d\n",DC.miny,DC.maxy); */

}; /* RefreshDisplay */

void readJoypad(void)
{
    int ret=0;
    unsigned long oldkeys = joypad.keys;

    ret = read( joypad.fd, &joypad.keys, 4);

    joypad.changed = joypad.keys ^ oldkeys;
}

int openJoypad(void)
{
    joypad.fd = open( "/dev/GPIO", O_RDWR | O_NDELAY );
    if( joypad.fd < 0 )
    {
        return 0;
    }

    readJoypad();

    return 1;
}

void cleanupterme(void)
{
  tcsetattr(STDIN_FILENO, TCSANOW, &savedTerminalSettings);
}
void cleanupterm(int s)
{
  cleanupterme();
  exit(0);
}

/*-----------------------------------------------------------------------------*/
void
DisplayKbd_InitHost(ARMul_State *state)
{
  struct termios term;

  tcgetattr(STDIN_FILENO, &savedTerminalSettings);
  term = savedTerminalSettings;
  term.c_lflag &= ~(ICANON | ECHO | ECHONL);
  tcsetattr(STDIN_FILENO, TCSANOW, &term);
  signal(SIGSEGV,cleanupterm);
  signal(SIGTERM,cleanupterm);
  signal(SIGHUP,cleanupterm);
  signal(SIGINT,cleanupterm);
  atexit(cleanupterme);

  SelectScreenMode(320, 240, 3);

  openJoypad();
} /* DisplayKbd_InitHost */

/*-----------------------------------------------------------------------------*/
static void ProcessKey(ARMul_State *state, int key, int transition) {
    keyboard_key_changed( &KBD, key, transition );
}; /* ProcessKey */

/*-----------------------------------------------------------------------------*/
/* Move the Control pane window                                                */
static void UpdateCursorPos(ARMul_State *state) {
  int x, y;
  x=VIDC.Horiz_CursorStart-(VIDC.Horiz_DisplayStart * 2 );
  y=VIDC.Vert_CursorStart-VIDC.Vert_DisplayStart;

  MMSP2(0x2920)=20+((x*320)/MonitorWidth);
  MMSP2(0x2922)=(y*240)/MonitorHeight;

}; /* UpdateCursorPos */

#define DEBUG_MOUSEMOVEMENT
/*-----------------------------------------------------------------------------*/
/* Called on an X motion event */
static void MouseMoved(ARMul_State *state, int xdiff, int ydiff ) {

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
DisplayKbd_PollHost(ARMul_State *state)
{
    int run=0;
    int MouseX=0, MouseY=0;
    unsigned long keys;
    readJoypad();
    keys = joypad.keys;
    if ( keys & KEY_PAD_UP )
        MouseY++;
    if ( keys & KEY_PAD_DOWN )
        MouseY--;
    if ( keys & KEY_PAD_LEFT )
        MouseX--;
    if ( keys & KEY_PAD_RIGHT )
        MouseX++;
    if ( keys & KEY_PAD_UP_LEFT )
        MouseY++, MouseX--;
    if ( keys & KEY_PAD_UP_RIGHT )
        MouseY++, MouseX++;
    if ( keys & KEY_PAD_DOWN_LEFT )
        MouseY--, MouseX--;
    if ( keys & KEY_PAD_DOWN_RIGHT )
        MouseY--, MouseX++;

    if ( MouseX || MouseY )
    {
      MouseMoved(state, MouseX, MouseY);
      run=1;
    }

    if ( joypad.changed & KEY_PAD_IN )
        ProcessKey(state, ARCH_KEY_button_1, !(keys & KEY_PAD_IN) );

    if ( joypad.changed & KEY_SELECT )
        ProcessKey(state, ARCH_KEY_f12, !(keys & KEY_SELECT) );

    if ( joypad.changed & KEY_START )
        ProcessKey(state, ARCH_KEY_return, !(keys & KEY_START) );

    if ( joypad.changed & KEY_A )
        ProcessKey(state, ARCH_KEY_h, !(keys & KEY_A) );

    if ( joypad.changed & KEY_Y )
        ProcessKey(state, ARCH_KEY_period, !(keys & KEY_Y) );

    if ( joypad.changed & KEY_LEFT_SHOULDER )
        ProcessKey(state, ARCH_KEY_shift_l, !(keys & KEY_LEFT_SHOULDER) );

    if ( joypad.changed )
      run=1;

  {
      fd_set s;
      struct timeval tv;
      tv.tv_sec=0;
      tv.tv_usec=0;
      FD_ZERO(&s);
      FD_SET(0,&s);
      if (select(1,&s,NULL,NULL,&tv) )
      if ( FD_ISSET(0,&s) )
      {
        char c;
        if ( read(0,&c,1) )
        {
            if ( c==8 || c==127 )
            {
                ProcessKey(state, ARCH_KEY_delete, 0 );
                ProcessKey(state, ARCH_KEY_delete, 1 );
            }
            else if ( c>31 && c<127 )
            {
                int k=KeyLookup[c-32][1];
                int m=KeyLookup[c-32][0];
                if ( m>=0 )
                    ProcessKey(state, m, 0 );
                ProcessKey(state, k, 0 );
                ProcessKey(state, k, 1 );
                if ( m>=0 )
                    ProcessKey(state, m, 1 );
            }
            run=1;
        }
      }
  }
  return run;
} /* DisplayKbd_PollHost */


static void UpdateGp2xScreenFromVIDC(void)
{
    SelectScreenMode((VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) * 2,
                      VIDC.Vert_DisplayEnd -  VIDC.Vert_DisplayStart,
                     (VIDC.ControlReg & 0xc) >> 2);
}


/*-----------------------------------------------------------------------------*/
void VIDC_PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw) {
  unsigned int addr, val;

  addr=(data>>24) & 255;
  val=data & 0xffffff;

  if (!(addr & 0xc0)) {
    int Log, Sup,Red,Green,Blue;

    /* This lot presumes it's not 8bpp mode! */
    Log=(addr>>2) & 15;
    Sup=(val >> 12) & 1;
    Blue=(val >> 8) & 15;
    Green=(val >> 4) & 15;
    Red=val & 15;
#ifdef DEBUG_VIDCREGS
    fprintf(stderr,"VIDC Palette write: Logical=%d Physical=(%d,%d,%d,%d)\n",
      Log,Sup,Red,Green,Blue);
#endif
    VideoRelUpdateAndForce(DC.MustResetPalette,VIDC.Palette[Log],(val & 0x1fff));
    return;
  };

  addr&=~3;
  switch (addr) {
    case 0x40: /* Border col */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC border colour write val=0x%x\n",val);
#endif
      VideoRelUpdateAndForce(DC.MustResetPalette,VIDC.BorderCol,(val & 0x1fff));
      break;

    case 0x44: /* Cursor palette log col 1 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC cursor log col 1 write val=0x%x\n",val);
#endif
      VideoRelUpdateAndForce(DC.MustResetPalette,VIDC.CursorPalette[0],(val & 0x1fff));
      break;

    case 0x48: /* Cursor palette log col 2 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC cursor log col 2 write val=0x%x\n",val);
#endif
      VideoRelUpdateAndForce(DC.MustResetPalette,VIDC.CursorPalette[1],(val & 0x1fff));
      break;

    case 0x4c: /* Cursor palette log col 3 */
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC cursor log col 3 write val=0x%x\n",val);
#endif
      VideoRelUpdateAndForce(DC.MustResetPalette,VIDC.CursorPalette[2],(val & 0x1fff));
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
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayStart,(val>>14) & 0x3ff);
#ifdef DIRECT_DISPLAY
      //if (DC.MustRedraw)
        UpdateGp2xScreenFromVIDC();
#endif
      break;

    case 0x90:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz display end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayEnd,(val>>14) & 0x3ff);
#ifdef DIRECT_DISPLAY
      //if (DC.MustRedraw)
        UpdateGp2xScreenFromVIDC();
#endif
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
      UpdateCursorPos(state);
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
#ifdef DIRECT_DISPLAY
      //if (DC.MustRedraw)
        UpdateGp2xScreenFromVIDC();
#endif
      break;

    case 0xb0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert disp end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_DisplayEnd,(val>>14) & 0x3ff);
#ifdef DIRECT_DISPLAY
      //if (DC.MustRedraw)
        UpdateGp2xScreenFromVIDC();
#endif
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
      break;

    case 0xe0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Control register val=0x%x\n",val);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.ControlReg,val & 0xffff);
#ifdef DIRECT_DISPLAY
      //if (DC.MustRedraw)
        UpdateGp2xScreenFromVIDC();
#endif
      break;

    default:
      fprintf(stderr,"Write to unknown VIDC register reg=0x%x val=0x%x\n",addr,val);
      break;

  }; /* Register switch */
}; /* PutValVIDC */

void gp2xScreenOffset(int offset)
{
    // printf("scroll screen to %d\n", offset );
    unsigned long addr = 0x3101000 + offset;
    MMSP2_32(0x290e,addr);
    MMSP2_32(0x2912,addr);
}

static void SelectScreenMode(int x, int y, int bpp)
{
  // printf("request set screen mode to %dx%d at %d bpp\n",x, y, 1<<bpp);

  if (x<=0 || x>1024 || y<=0 || y>768 || bpp<=0 || bpp>3)
    return;

  if (x==MonitorWidth && y==MonitorHeight && bpp==MonitorBpp)
    return;

  printf("setting screen mode to %dx%d at %d bpp\n",x, y, 1<<bpp);

  MonitorWidth  = x;
  MonitorHeight = y;
  MonitorBpp    = bpp;

  if (!mmsp2fd)
  {
     mmsp2fd = open( "/dev/mem", O_RDWR );
     if ( screenfd < 0 )
     {
        fprintf(stderr,"Unable to open mmsp2 device\n");
        return;
     }

     mmsp2 = (unsigned short *) mmap( NULL,
                                      0x10000,
                                      PROT_READ|PROT_WRITE,
                                      MAP_SHARED,
                                      mmsp2fd,
                                      0xc0000000
                                    );

    if ( mmsp2 == (unsigned short *)-1 )
    {
        fprintf(stderr,"Unable to memory map the mmsp2 hardware\n");
        close( screenfd );
        return;
    }

     DirectScreenExtent = 512*1024;

     DirectScreenMemory = (int*) mmap( NULL,
                                       DirectScreenExtent+4096,
                                       PROT_READ|PROT_WRITE,
                                       MAP_SHARED,
                                       mmsp2fd,
                                       0x3101000
                                     );

    if ( DirectScreenMemory == (int *)-1 )
    {
        fprintf(stderr,"Unable to memory map the screen memory\n");
        close( screenfd );
        return;
    }

    memset( DirectScreenMemory, 0, DirectScreenExtent );
  }

  bpp=1<<bpp;
  MMSP2(0x28da) = (((bpp+1)/8)<<9) | 0xab; /* bpp, enables */
  MMSP2(0x290c) = x*((bpp+1)/8); /* line length, bytes */

  gp2xScreenOffset(0);

  y = (y*(x*((bpp+1)/8)))/240;
  x = (x*1024)/320;

  /* This is a hack (copied from SDL h/w code) */
  /* Seems like you can't V scale if you're not H scaling */
  if ( (MonitorWidth==320) && (MonitorHeight!=240) )
    x--;

  MMSP2_32(0x2908,y);
  MMSP2(0x2906) = x;

  {
      short *cbase;
      cbase = (unsigned short *) DirectScreenMemory;
      cbase += DirectScreenExtent/2;
      cbase[0]=0xa555;
      cbase[1]=0xa901;
      cbase[2]=0xaa41;
      cbase[3]=0xa911;
      cbase[4]=0xa465;
      cbase[5]=0x91a9;
      cbase[6]=0x46aa;
      cbase[7]=0x9aaa;

      MMSP2(0x291e)=8|(255<<8);
      MMSP2(0x2920)=MonitorWidth/2;
      MMSP2(0x2922)=MonitorHeight/2;
      MMSP2(0x2880)=(1<<2) | (1<<9) | (1<<12) | (3<<13);

      MMSP2_32(0x292c,0x3101000+DirectScreenExtent);
      MMSP2_32(0x2930,0x3101000+DirectScreenExtent);
  }
}
