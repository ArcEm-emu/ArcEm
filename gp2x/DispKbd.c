/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */
/* Display and keyboard interface for the Arc emulator */

#include <stdio.h>
#include <limits.h>

#include "../armdefs.h"
#include "armarc.h"
#include "arch/keyboard.h"
#include "DispKbd.h"
#include "archio.h"
#include "hdc63463.h"
#include "../armemu.h"
#include "arch/displaydev.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>

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



static void UpdateCursorPos(ARMul_State *state);
static void SelectScreenMode(int x, int y, int bpp);
static void RefreshMouse(ARMul_State *state);

static void set_cursor_palette(uint16_t *pal);

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

/* ------------------------------------------------------------------ */

static void set_cursor_palette(uint16_t *pal)
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

/* Palettised display code */
#define PDD_Name(x) pdd_##x

static int BorderPalEntry;

static int palette_updated = 0;

typedef struct {
  ARMword *data;
  int offset;
} PDD_Row;

static void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz);

static void PDD_Name(Host_SetPaletteEntry)(ARMul_State *state,int i,unsigned int phys)
{
  int r = (phys & 0xf)*0x11;
  int g = ((phys>>4) & 0xf)*0x11;
  int b = ((phys>>8) & 0xf)*0x11;
  MMSP2SETPAL(i,r,g,b);
  palette_updated = 1;
}

static void PDD_Name(Host_SetBorderColour)(ARMul_State *state,unsigned int phys)
{
  /* Set border palette entry */
  if(BorderPalEntry != 256)
  {
    int r = (phys & 0xf)*0x11;
    int g = ((phys>>4) & 0xf)*0x11;
    int b = ((phys>>8) & 0xf)*0x11;
    MMSP2SETPAL(BorderPalEntry,r,g,b);
    palette_updated = 1;
  }
}

static inline PDD_Row PDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset,int *alignment)
{
  PDD_Row drow;
  offset = (MonitorWidth+offset)<<MonitorBpp;
  drow.offset = offset & 0x1f;
  drow.data = DirectScreenMemory+(offset>>5);
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
  if(BorderPalEntry != 256)
  {
    /* TODO - Fill rect with border colour */
  }
}

#include "../arch/paldisplaydev.c"

void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz)
{
  SelectScreenMode(width,height,depth);
  HD.XScale = 1;
  HD.YScale = 1;
  HD.Width = MonitorWidth;
  HD.Height = MonitorHeight;
  
  if(MonitorBpp > depth)
  {
    /* We have enough palette entries to have a border */
    BorderPalEntry = 1<<(1<<depth);
  }
  else
  {
    /* Disable border entry */
    BorderPalEntry = 256;
  }

  /* Calculate expansion params */
  if((MonitorBpp == depth) && (HD.XScale == 1))
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
    HD.ExpandFactor += (MonitorBpp-depth);
    HD.ExpandTable = expandtable;
    unsigned int mul = 1;
    int i;
    for(i=0;i<HD.XScale;i++)
    {
      mul |= 1<<(i*(1<<MonitorBpp));
    }
    GenExpandTable(HD.ExpandTable,1<<depth,HD.ExpandFactor,mul);
  }

  /* Screen is expected to be cleared */
  PDD_Name(Host_DrawBorderRect)(state,0,0,HD.Width,HD.Height);
}

static void
PDD_Name(Host_PollDisplay)(ARMul_State *state)
{
  /* TODO - Surely there's a way of updating one palette entry at a time? */
  if(palette_updated)
  {
    palette_updated = 0;
    mmsp2writepal();
  }

  set_cursor_palette(VIDC.CursorPalette);

  RefreshMouse(state);
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
int
DisplayDev_Init(ARMul_State *state)
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

  DisplayDev_FrameSkip = 4; /* TODO - Tweak */

  return DisplayDev_Set(state,&PDD_DisplayDev);
} /* DisplayDev_Init */

/*-----------------------------------------------------------------------------*/
static void ProcessKey(ARMul_State *state, int key, int transition) {
    keyboard_key_changed( &KBD, key, transition );
}; /* ProcessKey */

/*-----------------------------------------------------------------------------*/
/* Move the Control pane window                                                */
static void UpdateCursorPos(ARMul_State *state) {
  int x, y;
  DisplayDev_GetCursorPos(state,&x,&y);

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
Kbd_PollHostKbd(ARMul_State *state)
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
} /* Kbd_PollHostKbd */


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

  if(bpp<2)
    bpp = 2; /* MMSP2 doesn't support 1bpp or 2bpp */

  if (x<=0 || x>1024 || y<=0 || y>768 || bpp<=0 || bpp>3)
  {
    fprintf(stderr,"Bad mode\n");
    exit(EXIT_FAILURE);
  }

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
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }

    memset( DirectScreenMemory, 0, DirectScreenExtent );
  }

  if(x*y<<bpp > DirectScreenExtent<<3)
  {
    fprintf(stderr,"Mode too big\n");
    exit(EXIT_FAILURE);
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
