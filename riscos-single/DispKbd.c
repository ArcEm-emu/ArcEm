/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */
/* Display and keyboard interface for the Arc emulator */

// #define MOUSEKEY XK_KP_Add

#define KEYREENABLEDELAY 1000
#define POLLGAP 125

/*#define DEBUG_VIDCREGS*/
/* NOTE: Can't use ARMul's refresh function because it has a small
   limit on the time delay from posting the event to it executing   */
/* It's actually decremented once every POLLGAP - that is called
   with the ARMul scheduler */

#define AUTOREFRESHPOLL 2500

#include <stdio.h>
#include <limits.h>

#include "kernel.h"
#include "swis.h"

#define  ArcEmKey_GetKey 0x53340

#include "../armdefs.h"
#include "armarc.h"
#include "DispKbd.h"
#include "archio.h"
#include "hdc63463.h"


#include "ControlPane.h"

#define ControlHeight 30
#define CURSORCOLBASE 250

/* HOSTDISPLAY is too verbose here - but HD could be hard disc somewhere else! */
/*#define HD HOSTDISPLAY*/
#define DC DISPLAYCONTROL

static unsigned AutoKey(ARMul_State *state);
static void UpdateCursorPos(ARMul_State *state);
static void SelectROScreenMode(int x, int y, int bpp);


static struct EventNode enodes[4];
static int xpollenode = 2; /* Flips between 2 and 3 */

static int MonitorWidth;
static int MonitorHeight;
int MonitorBpp;

#ifdef DIRECT_DISPLAY
int *ROScreenMemory;
int ROScreenExtent;
#endif


const char *__dynamic_da_name = "ArcEm Heap";


/*-----------------------------------------------------------------------------*/
static unsigned AutoKey(ARMul_State *state) {
  fprintf(stderr,"AutoKey!\n");
  KBD.TimerIntHasHappened+=2;

  return 0;
};
/*----------------------------------------------------------------------------*/
/* I'm not confident that this is completely correct - if it's wrong all hell
   is bound to break loose! If it works however it should speed things up nicely!
*/
static void MarkAsUpdated(ARMul_State *state, int end) {
  unsigned int Addr=MEMC.Vinit*16;
  unsigned int Vend=MEMC.Vend*16;

  /* Loop from Vinit until we have done the whole image */
  for(;end>0;Addr+=UPDATEBLOCKSIZE,end-=UPDATEBLOCKSIZE) {
    if (Addr>Vend) Addr=Addr-Vend+MEMC.Vstart*16;
    DC.UpdateFlags[Addr/UPDATEBLOCKSIZE]=MEMC.UpdateFlags[Addr/UPDATEBLOCKSIZE];
  };

}; /* MarkAsUpdated */

/*----------------------------------------------------------------------------*/
/* Check to see if the area of memory has changed.                            */
/* Returns true if there is any chance that the given area has changed        */
static int QueryRamChange(ARMul_State *state,int offset, int len) {
  unsigned int Vinit=MEMC.Vinit;
  unsigned int Vstart=MEMC.Vstart;
  unsigned int Vend=MEMC.Vend;
  unsigned int startblock,endblock,currentblock;

  /* Figure out if 'offset' starts between Vinit-Vend or Vstart-Vinit */
  if ((offset)<(((Vend-Vinit)+1)*16)) {
    /* Vinit-Vend */
    /* Now check to see if the whole buffer is in that area */
    if ((offset+len)>=(((Vend-Vinit)+1)*16)) {
      /* It's split - so copy the bit upto Vend and then the rest */

      /* Don't bother - this isn't going to happen much - let's say it changed */
      return(1);
    } else {
      offset+=Vinit*16;
    };
  } else {
    /* Vstart-Vinit */
    /* it's all in one place */
    offset-=((Vend-Vinit)+1)*16; /* That's the bit after Vinit */
    offset+=Vstart*16; /* so the bit we copy is somewhere after Vstart */
  };

  /* So now we have an area running from 'offset' to 'offset+len' */
  startblock=offset/UPDATEBLOCKSIZE;
  endblock=(offset+len)/UPDATEBLOCKSIZE;

  /* Now just loop through from startblock to endblock */
  for(currentblock=startblock;currentblock<=endblock;currentblock++)
    if (MEMC.UpdateFlags[currentblock]!=DC.UpdateFlags[currentblock]) return(1);

  /* We've checked them all and their are no changes */
  return(0);
}; /* QueryRamChange */
/*-----------------------------------------------------------------------------*/
/* Copy a lump of screen RAM into the buffer.  The section of screen ram is    */
/* len bytes from the top left of the screen.  The routine takes into account
   all scrolling etc.                                                          */
/* This routine may be burdened with undoing endianness                        */
static void CopyScreenRAM(ARMul_State *state,int offset, int len, char *Buffer) {
  unsigned int Vinit=MEMC.Vinit;
  unsigned int Vstart=MEMC.Vstart;
  unsigned int Vend=MEMC.Vend;

  /*fprintf(stderr,"CopyScreenRAM: offset=%d len=%d Vinit=0x%x VStart=0x%x Vend=0x%x\n",
         offset,len,Vinit,Vstart,Vend); */

  /* Figure out if 'offset' starts between Vinit-Vend or Vstart-Vinit */
  if ((offset)<(((Vend-Vinit)+1)*16)) {
    /* Vinit-Vend */
    /* Now check to see if the whole buffer is in that area */
    if ((offset+len)>=(((Vend-Vinit)+1)*16)) {
      /* Its split - so copy the bit upto Vend and then the rest */
      int tmplen;

      offset+=Vinit*16;
      tmplen=(Vend+1)*16-offset;
      /*fprintf(stderr,"CopyScreenRAM: Split over Vend offset now=0x%x tmplen=%d\n",offset,tmplen); */
      memcpy(Buffer,MEMC.PhysRam+(offset/sizeof(ARMword)),tmplen);
      memcpy(Buffer+tmplen,MEMC.PhysRam+((Vstart*16)/sizeof(ARMword)),len-tmplen);
    } else {
      /* Its all their */
      /*fprintf(stderr,"CopyScreenRAM: All in one piece between Vinit..Vend offset=0x%x\n",offset); */
      offset+=Vinit*16;
      memcpy(Buffer,MEMC.PhysRam+(offset/sizeof(ARMword)),len);
    };
  } else {
    /* Vstart-Vinit */
    /* it's all in one place */
    offset-=((Vend-Vinit)+1)*16; /* That's the bit after Vinit */
    offset+=Vstart*16; /* so the bit we copy is somewhere after Vstart */
    /*fprintf(stderr,"CopyScreenRAM: All in one piece between Vstart..Vinit offset=0x%x\n",offset); */
    memcpy(Buffer,MEMC.PhysRam+(offset/sizeof(ARMword)),len);
  };

#ifdef HOST_BIGENDIAN
/* Hacking of the buffer now - OK - I know that I should do this neater */
  for(offset=0;offset<len;offset+=4) {
    unsigned char tmp;

    tmp=Buffer[offset];
    Buffer[offset]=Buffer[offset+3];
    Buffer[offset+3]=tmp;
    tmp=Buffer[offset+2];
    Buffer[offset+2]=Buffer[offset+1];
    Buffer[offset+1]=tmp;
  };
#endif
}; /* CopyScreenRAM */

/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the standard 1 bpp modes                        */
static void DoColourMap_2(ARMul_State *state) {
  //XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<2;c++) {
    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c, 0);
    _swi(OS_WriteI+16, 0);
    _swi(OS_WriteC, _IN(0), (VIDC.Palette[c] & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.Palette[c] >> 4) & 15) * 17);
    _swi(OS_WriteC, _IN(0), ((VIDC.Palette[c] >> 8) & 15) * 17);
  };

  /* Now do the ones for the cursor */
  for(c=0;c<3;c++) {
    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c+1, 0); // For setting 'real' pointer colours
    _swi(OS_WriteI+25, 0);
    _swi(OS_WriteC, _IN(0), (VIDC.CursorPalette[c] & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>4) & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>8) & 15)*17);
  };

  DC.MustResetPalette=0;
}; /* DoColourMap_2 */


/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the standard 2 bpp modes                        */
static void DoColourMap_4(ARMul_State *state) {
  //XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<4;c++) {
    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c, 0);
    _swi(OS_WriteI+16, 0);
    _swi(OS_WriteC, _IN(0), (VIDC.Palette[c] & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.Palette[c]>>4) & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.Palette[c]>>8) & 15)*17);
  };

  /* Now do the ones for the cursor */
  for(c=0;c<3;c++) {
    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c+1, 0); // For setting 'real' pointer colours
    _swi(OS_WriteI+25, 0);
    _swi(OS_WriteC, _IN(0), (VIDC.CursorPalette[c] & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>4) & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>8) & 15)*17);
  };

  DC.MustResetPalette=0;
}; /* DoColourMap_4 */


/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the standard 4 bpp modes                        */
static void DoColourMap_16(ARMul_State *state) {
  //XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<16;c++) {
    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c, 0);
    _swi(OS_WriteI+16, 0);
    _swi(OS_WriteC, _IN(0), (VIDC.Palette[c] & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.Palette[c]>>4) & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.Palette[c]>>8) & 15)*17);
  };

  /* Now do the ones for the cursor */
  for(c=0;c<3;c++) {
    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c+1, 0); // For setting 'real' pointer colours
    _swi(OS_WriteI+25, 0);
    _swi(OS_WriteC, _IN(0), (VIDC.CursorPalette[c] & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>4) & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>8) & 15)*17);
  };

  DC.MustResetPalette=0;
}; /* DoColourMap_16 */


/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the 8bpp modes                                  */
static void DoColourMap_256(ARMul_State *state) {
//  XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<256;c++) {
    int palentry=c &15;
    int L4=(c>>4) &1;
    int L65=(c>>5) &3;
    int L7=(c>>7) &1;

    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c, 0);
    _swi(OS_WriteI+16, 0);
    _swi(OS_WriteC, _IN(0), ((VIDC.Palette[palentry] & 7) | (L4<<3))*17);
    _swi(OS_WriteC, _IN(0), (((VIDC.Palette[palentry] >> 4) & 3) | (L65<<2))*17);
    _swi(OS_WriteC, _IN(0), (((VIDC.Palette[palentry] >> 8) & 7) | (L7<<3))*17);
  }

  /* Now do the ones for the cursor */
  for(c=0;c<3;c++) {
    _swi(OS_WriteI+19, 0);
    _swi(OS_WriteI+c+1, 0); // For setting 'real' pointer colours
    _swi(OS_WriteI+25, 0);
    _swi(OS_WriteC, _IN(0), (VIDC.CursorPalette[c] & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>4) & 15)*17);
    _swi(OS_WriteC, _IN(0), ((VIDC.CursorPalette[c]>>8) & 15)*17);
  };
  DC.MustResetPalette=0;
}; /* DoColourMap_Standard */


/*-----------------------------------------------------------------------------*/
/* Refresh the mouse's image                                                    */
static void RefreshMouse(ARMul_State *state) {
  int height;
  int *pointer_data = MEMC.PhysRam + ((MEMC.Cinit * 16)/4);

  height = (VIDC.Vert_CursorEnd - VIDC.Vert_CursorStart) + 1;

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

  UpdateCursorPos(state);
}; /* RefreshMouse */


#ifndef DIRECT_DISPLAY
/*----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_1bpp(ARMul_State *state) {
  int DisplayHeight = VIDC.Vert_DisplayEnd - VIDC.Vert_DisplayStart;
  int DisplayWidth  = (VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) * 2;
  int x, y, memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth / 8];
  char *ImgPtr;
  int block[2];

  block[0] = 148;
  block[1] = -1;

  _swi(OS_ReadVduVariables, _INR(0,1), &block, &block);

  ImgPtr = (char *)block[0];

  /* First configure the colourmap */
   DoColourMap_2(state);

  if (DisplayHeight <= 0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_1bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth <= 0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_1bpp: 0 or -ve display width\n");
    return;
  };

  /* Cope with screwy display widths/height */
  if (DisplayHeight>MonitorHeight) DisplayHeight=MonitorHeight;
  if (DisplayWidth>MonitorWidth)
    VisibleDisplayWidth=MonitorWidth;
  else
    VisibleDisplayWidth=DisplayWidth;

/* Redraw of 1bpp modes go here */

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/8,ImgPtr+=MonitorWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,VisibleDisplayWidth/8))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/8, Buffer);

      for(x=0;x<VisibleDisplayWidth;x+=8) {
        int bit;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(bit=0;bit<=8;bit++) {
          ImgPtr[x+bit]=(Buffer[x/8]>>bit) &1;
        }; /* bit */
      }; /* x */
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_PseudoColor_1bpp */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_2bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/4];
  char *ImgPtr;
  int block[2];

  block[0]=148;
  block[1]=-1;

  _swi(OS_ReadVduVariables, _INR(0,1), &block, &block);

  ImgPtr=(char *) block[0];

  /* First configure the colourmap */
  DoColourMap_4(state);

  if (DisplayHeight<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_2bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_2bpp: 0 or -ve display width\n");
    return;
  };

  /* Cope with screwy display widths/height */
  if (DisplayHeight>MonitorHeight) DisplayHeight=MonitorHeight;
  if (DisplayWidth>MonitorWidth)
    VisibleDisplayWidth=MonitorWidth;
  else
    VisibleDisplayWidth=DisplayWidth;


  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/4,ImgPtr+=MonitorWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,VisibleDisplayWidth/4))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/4, Buffer);

      for(x=0;x<VisibleDisplayWidth;x+=4) {
        int pixel;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(pixel=0;pixel<4;pixel++) {
          ImgPtr[x+pixel]=(Buffer[x/4]>>(pixel*2)) &3;
        }; /* pixel */
      }; /* x */
    }; /* Update test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_PseudoColor_2bpp */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_4bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/2];
  char *ImgPtr;
  int block[2];

  block[0]=148;
  block[1]=-1;

  _swi(OS_ReadVduVariables, _INR(0,1), &block, &block);

  ImgPtr=(char *) block[0];

  /* First configure the colourmap */
  DoColourMap_16(state);

  if (DisplayHeight<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_4bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_4bpp: 0 or -ve display width\n");
    return;
  };

  //fprintf(stderr,"RefreshDisplay_PseudoColor_4bpp: DisplayWidth=%d DisplayHeight=%d\n",
  //        DisplayWidth,DisplayHeight);

  /* Cope with screwy display widths/height */
  if (DisplayHeight>MonitorHeight) DisplayHeight=MonitorHeight;
  if (DisplayWidth>MonitorWidth)
    VisibleDisplayWidth=MonitorWidth;
  else
    VisibleDisplayWidth=DisplayWidth;

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=(DisplayWidth/2),ImgPtr+=MonitorWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,VisibleDisplayWidth/2))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/2, Buffer);

      for(x=0;x<VisibleDisplayWidth;x+=2) {
        int pixel;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(pixel=0;pixel<2;pixel++) {
          ImgPtr[x+pixel]=(Buffer[x/2]>>(pixel*4)) &15;
        }; /* pixel */
      }; /* x */
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_PseudoColor_4bpp */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_8bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int y,memoffset;
  int VisibleDisplayWidth;
  char *ImgPtr;
  int block[2];

  block[0]=148;
  block[1]=-1;

  _swi(OS_ReadVduVariables, _INR(0,1), &block, &block);

  ImgPtr=(char *) block[0];

  /* First configure the colourmap */
  DoColourMap_256(state);

  if (DisplayHeight<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_8bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_8bpp: 0 or -ve display width\n");
    return;
  };

  /* Cope with screwy display widths/height */
  if (DisplayHeight>MonitorHeight) DisplayHeight=MonitorHeight;
  if (DisplayWidth>MonitorWidth)
    VisibleDisplayWidth=MonitorWidth;
  else
    VisibleDisplayWidth=DisplayWidth;

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth,ImgPtr+=MonitorWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,VisibleDisplayWidth))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth, ImgPtr);
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_PseudoColor_8bpp */

/*----------------------------------------------------------------------------*/
#endif

static void RefreshDisplay(ARMul_State *state) {
  DC.AutoRefresh=AUTOREFRESHPOLL;
  ioc.IRQStatus|=8; /* VSync */
  ioc.IRQStatus |= 0x20; /* Sound - just an experiment */
  IO_UpdateNirq();

  DC.miny=MonitorHeight-1;
  DC.maxy=0;

  RefreshMouse(state);

#ifndef DIRECT_DISPLAY
  /* First figure out number of BPP */
    switch ((VIDC.ControlReg & 0xc)>>2) {
      case 0: /* 1bpp */
        RefreshDisplay_PseudoColor_1bpp(state);
        break;
      case 1: /* 2bpp */
        RefreshDisplay_PseudoColor_2bpp(state);
        break;
      case 2: /* 4bpp */
        RefreshDisplay_PseudoColor_4bpp(state);
        break;
      case 3: /* 8bpp */
        RefreshDisplay_PseudoColor_8bpp(state);
        break;
    };
#else
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
#endif

  /*fprintf(stderr,"RefreshDisplay: Refreshed %d-%d\n",DC.miny,DC.maxy); */

}; /* RefreshDisplay */

/*-----------------------------------------------------------------------------*/
void DisplayKbd_Init(ARMul_State *state) {
  int index;

#ifndef DIRECT_DISPLAY
  SelectROScreenMode(800, 600, 3);
#else
  SelectROScreenMode(640, 480, 2);
#endif

#ifndef DIRECT_DISPLAY
  /* Make messages just go in bottom of screen? */
  _swi(OS_WriteI+28, 0);
  _swi(OS_WriteI+0,  0);
  _swi(OS_WriteI+74, 0);
  _swi(OS_WriteI+99, 0);
  _swi(OS_WriteI+64, 0);
#endif

  DC.PollCount=0;
  KBD.KbdState=KbdState_JustStarted;
  KBD.MouseTransEnable=0;
  KBD.KeyScanEnable=0;
  KBD.KeyColToSend=-1;
  KBD.KeyRowToSend=-1;
  KBD.MouseXCount=0;
  KBD.MouseYCount=0;
  KBD.KeyUpNDown=0; /* When 0 it means the key to be sent is a key down event, 1 is up */
  KBD.HostCommand=0;
  KBD.BuffOcc=0;
  KBD.TimerIntHasHappened=0; /* if using AutoKey should be 2 Otherwise it never reinitialises the event routines */
  KBD.Leds=0;
  DC.DoingMouseFollow=0;
  DC.AutoRefresh=AUTOREFRESHPOLL; /* Surely this is set somewhere??? */

  /* Note the memory model sets its to 1 - note the ordering */
  /* i.e. it must be updated */
  for(index=0;index<(512*1024)/UPDATEBLOCKSIZE;index++)
    DC.UpdateFlags[index]=0;

  ARMul_ScheduleEvent(&(enodes[xpollenode]),POLLGAP,DisplayKbd_XPoll);
  xpollenode^=1;
}; /* DisplayKbd_Init */


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
/* Send the first byte of a data transaction with the host                     */
static void Kbd_StartToHost(ARMul_State *state) {
  /* Start a particular transmission - base don the HostCommand vlaue */
  /* This is either a request for keyboard id, request for mouse, or 0
     if its 0 we should check to see if a key needs sending or mouse data needs
     sending. */
  if (KBD.KbdState!=KbdState_Idle) return;

#ifdef DEBUG_KBD
  if (KBD.HostCommand) fprintf(stderr,"Kbd_StartToHost: HostCommand=%d\n",KBD.HostCommand);
#endif

  switch (KBD.HostCommand) {
    case 0x20: /* Request keyboard id */
      /* Apparently 1 - hmm!  '1' */
      if (IOC_WriteKbdRx(state,0x81)!=-1) {
        KBD.KbdState=KbdState_Idle;
        KBD.HostCommand=0;
      };
      return;

    case 0x22: /* Request mouse position */
      KBD.MouseXToSend=KBD.MouseXCount;
      KBD.MouseYToSend=KBD.MouseYCount;
      if (IOC_WriteKbdRx(state,KBD.MouseXToSend)!=-1) {
        KBD.KbdState=KbdState_SentMouseByte1;
        KBD.HostCommand=0;
      };
      return;
  };

  /* OK - we'll have a look to see if we should be doing something else */
  if (KBD.KbdState!=KbdState_Idle) return;

  /* Perhaps some keyboard data */
  if (KBD.KeyScanEnable && (KBD.BuffOcc>0)) {
    int loop;
#ifdef DEBUG_KBD
    fprintf(stderr,"KBD_StartToHost - sending key -  BuffOcc=%d (%d,%d,%d)\n",KBD.BuffOcc,
       KBD.Buffer[0].KeyUpNDown,KBD.Buffer[0].KeyRowToSend,KBD.Buffer[0].KeyColToSend);
#endif
    KBD.KeyUpNDown=KBD.Buffer[0].KeyUpNDown;
    KBD.KeyRowToSend=KBD.Buffer[0].KeyRowToSend;
    KBD.KeyColToSend=KBD.Buffer[0].KeyColToSend;
    /* I should implement a circular buffer - but can't be bothered yet */
    for(loop=1;loop<KBD.BuffOcc;loop++)
      KBD.Buffer[loop-1]=KBD.Buffer[loop];

    if (IOC_WriteKbdRx(state,(KBD.KeyUpNDown?0xd0:0xc0) | KBD.KeyRowToSend)!=-1) {
      KBD.KbdState=KbdState_SentKeyByte1;
    };
    KBD.BuffOcc--;
    return;
  };

  /* NOTE: Mouse movement gets lower priority than key input */
  if ((KBD.MouseTransEnable) && (KBD.MouseXCount | KBD.MouseYCount)) {
    /* Send some mouse data why not! */
    KBD.MouseXToSend=KBD.MouseXCount;
    KBD.MouseYToSend=KBD.MouseYCount;
    KBD.MouseXCount=0;
    KBD.MouseYCount=0;
    if (IOC_WriteKbdRx(state,KBD.MouseXToSend)!=-1) {
      KBD.KbdState=KbdState_SentMouseByte1;
    };
    return;
  };

}; /* Kbd_StartToHost */

/*-----------------------------------------------------------------------------*/
/* Move the Control pane window                                                */
static void UpdateCursorPos(ARMul_State *state) {
  int internal_x, internal_y;
  char block[5];
  int xeig=_swi(OS_ReadModeVariable,_INR(0,1)|_RETURN(2),-1,4);
  int yeig=_swi(OS_ReadModeVariable,_INR(0,1)|_RETURN(2),-1,5);

  internal_x=VIDC.Horiz_CursorStart-(VIDC.Horiz_DisplayStart*2);
  internal_y=VIDC.Vert_CursorStart-VIDC.Vert_DisplayStart;

  block[0]=5;
  {
    short x = internal_x << xeig;
    block[1] = x & 255;
    block[2] = x >> 8;
  }
  {
    short y = (MonitorHeight-internal_y) << yeig;
    block[3] = y & 255;
    block[4] = y >> 8;
  }

  _swi(OS_Word, _INR(0,1), 21, &block);

}; /* UpdateCursorPos */
/*-----------------------------------------------------------------------------*/
/* Called when their is some data in the serial tx register on the IOC         */
static void Kbd_CodeFromHost(ARMul_State *state, unsigned char FromHost) {
#ifdef DEBUG_KBD
  fprintf(stderr,"Kbd_CodeFromHost: FromHost=0x%x State=%d\n",FromHost,KBD.KbdState);
#endif

  switch (KBD.KbdState) {
    case KbdState_JustStarted:
      /* Only valid code is Reset */
      if (FromHost==0xff) {
        if (IOC_WriteKbdRx(state,0xff)!=-1) {
          KBD.KbdState=KbdState_SentHardReset;
          fprintf(stderr,"KBD: Received Reset and sent Reset\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to Reset - Kart full\n");
        };
      } else {
        fprintf(stderr,"KBD: JustStarted; Got bad code 0x%x\n",FromHost);
      };
      break;

    case KbdState_SentHardReset:
      /* Only valid code is ack1 */
      if (FromHost==0xfe) {
        if (IOC_WriteKbdRx(state,0xfe)!=-1) {
          KBD.KbdState=KbdState_SentAck1;
          fprintf(stderr,"KBD: Received Ack1 and sent Ack1\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to Ack1 - Kart full\n");
        };
      } else {
        fprintf(stderr,"KBD: SentAck1; Got bad code 0x%x - sending reset\n",FromHost);
        IOC_WriteKbdRx(state,0xff);
        KBD.KbdState=KbdState_SentHardReset; /* Or should that be just started? */
      };
      break;

    case KbdState_SentAck1:
      /* Only valid code is Reset Ak 2 */
      if (FromHost==0xfd) {
        if (IOC_WriteKbdRx(state,0xfd)!=-1) {
          KBD.KbdState=KbdState_SentAck2;
          fprintf(stderr,"KBD: Received and ack'd to Ack2\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to Ack2 - Kart full\n");
        };
      } else {
        fprintf(stderr,"KBD: SentAck2; Got bad code 0x%x\n",FromHost);
      };
      break;

    default:
      if (FromHost==0xff) {
        if (IOC_WriteKbdRx(state,0xff)!=-1) {
          KBD.KbdState=KbdState_SentHardReset;
          fprintf(stderr,"KBD: Received and ack'd to hardware reset\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to hardware reset - Kart full\n");
        };
        return;
      };

      switch (FromHost & 0xf0) {
        case 0: /* May be LED switch */
          if ((FromHost & 0x08)==0x08) {
            fprintf(stderr,"KBD: Received bad code: 0x%x\n",FromHost);
            return;
          }
          /*printf("KBD: LED state now: 0x%x\n",FromHost & 0x7); */
          if (KBD.Leds!=(FromHost & 0x7)) {
            KBD.Leds=FromHost & 0x7;
            /*ControlPane_RedrawLeds(state);*/
          };
#ifdef LEDENABLE
          /* I think we have to acknowledge - but I don't know with what */
          if (IOC_WriteKbdRx(state,0xa0 | (FromHost & 0x7))) {
            fprintf(stderr,"KBD: acked led's\n");
          } else {
            fprintf(stderr,"KBD: Couldn't respond to LED - Kart full\n");
          };
#endif
          break;

        case 0x20: /* Host requests keyboard id - or mouse position */
          KBD.HostCommand=FromHost;
          Kbd_StartToHost(state);
          fprintf(stderr,"KBD: Host requested keyboard id\n");
          return;

        case 0x30: /* Its probably an ack of some type */
          switch (FromHost & 0xf) {
            case 0: /* Disables everything */
            case 1: /* Disables mouse */
            case 2: /* Disables keys */
            case 3: /* all enabled */
              if ((KBD.KbdState!=KbdState_SentKeyByte1) &&
                  (KBD.KbdState!=KbdState_SentMouseByte1)) {
                KBD.KbdState=KbdState_Idle;
                KBD.MouseTransEnable=(FromHost & 2)>>1;
                KBD.KeyScanEnable=FromHost & 1;
              } else {
                /* Hmm - we just sent a first byte - we shouldn't get one of these! */
#ifdef DEBUG_KBD
                fprintf(stderr,"KBD: Got last byte ack after first byte!\n");
#endif
              };
              break;

            case 0xf: /* First byte ack */
              if ((KBD.KbdState!=KbdState_SentKeyByte1) &&
                  (KBD.KbdState!=KbdState_SentMouseByte1)) {
                fprintf(stderr,"KBD: Got 1st byte ack when we haven't sent one!\n");
              } else {
                if (KBD.KbdState==KbdState_SentMouseByte1) {
                  if (IOC_WriteKbdRx(state,KBD.MouseYToSend)==-1) {
                    fprintf(stderr,"KBD: Couldn't send 2nd byte of mouse value - Kart full\n");
                  };
                  KBD.KbdState=KbdState_SentMouseByte2;
                } else {
                  if (IOC_WriteKbdRx(state,(KBD.KeyUpNDown?0xd0:0xc0) | KBD.KeyColToSend)==-1) {
                    fprintf(stderr,"KBD: Couldn't send 2nd byte of key value - Kart full\n");
                  };
                  KBD.KbdState=KbdState_SentKeyByte2;
                  /* Indicate that the key has been sent */
                  KBD.KeyRowToSend=-1;
                  KBD.KeyColToSend=-1;
                };
              }; /* Have sent 1st byte test */
              break;

            default:
              fprintf(stderr,"KBD: Bad ack type received 0x%x\n",FromHost);
              break;
          }; /* bottom nybble of ack switch */
          return;

        case 0x40: /* Host just sends us some data....*/
          fprintf(stderr,"KBD: Host sent us some data: 0x%x\n",FromHost);
          return;

        default:
          fprintf(stderr,"KBD: Unknown code received from host 0x%x\n",FromHost);
          return;
      }; /* FromHost top nybble switch */
  }; /* current state switch */
}; /* Kbd_CodeFromHost */
/*-----------------------------------------------------------------------------*/
/* Called on an X motion event */
static void MouseMoved(ARMul_State *state, int mousex, int mousey/*,XMotionEvent *xmotion*/) {
  int xdiff,ydiff;
  /* Well the coordinates of the mouse cursor are now in xmotion->x and
    xmotion->y, I'm going to compare those against the cursor position
    and transmit the difference.  This can't possibly take care of the OS's
    hotspot offsets */

  /* We are now only using differences from the reference position */
  if ((mousex==MonitorWidth/2) && (mousey==MonitorHeight/2)) return;

  {
    char block[5];
    int x=MonitorWidth/2;
    int y=MonitorHeight/2;

    block[0]=3;
    block[1]=x % 256;
    block[2]=x / 256;
    block[3]=y % 256;
    block[4]=y / 256;

    _swi(OS_Word, _INR(0,1), 21, &block);
  }

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: CursorStart=%d xmotion->x=%d\n",
          VIDC.Horiz_CursorStart,mousex);
#endif
  xdiff=mousex-MonitorWidth/2;
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

  ydiff=mousey-MonitorHeight/2;
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
/* Called using an ARM_ScheduleEvent - it also sets itself up to be called
   again.                                                                     */
unsigned int DisplayKbd_XPoll(void *data) {
  ARMul_State *state = data;
  int KbdSerialVal;
  static int KbdPollInt=0;
  static int discconttog=0;

  /* Our POLLGAP runs at 125 cycles, HDC (and fdc) want callback at 250 */
  if (discconttog) HDC_Regular(state);
  discconttog^=1;

  if ((KbdPollInt++)>100) {
    KbdPollInt=0;
    /* Keyboard check */
    if (KbdSerialVal=IOC_ReadKbdTx(state),KbdSerialVal!=-1) {
      Kbd_CodeFromHost(state,KbdSerialVal);
    } else {
      if (KBD.TimerIntHasHappened>2) {
        KBD.TimerIntHasHappened=0;
        if (KBD.KbdState==KbdState_Idle) Kbd_StartToHost(state);
      };
    };
  };

  /* Keyboard handling */
  {
    int key;
    int transition;

    if (_swi (ArcEmKey_GetKey, _RETURN(0)|_OUTR(1,2), &transition, &key))
    {
      //printf("Processing key %d, transition %d\n",key, transition);
      ProcessKey(state, key, transition);
    }
  }

  /* Mouse handling */
  {
    int mousex;
    int mousey;
    _swi(OS_Mouse, _OUTR(0,1), &mousex, &mousey);

    MouseMoved(state, mousex, mousey);
  }

  if (--(DC.AutoRefresh)<0) RefreshDisplay(state);

  ARMul_ScheduleEvent(&(enodes[xpollenode]),POLLGAP,DisplayKbd_XPoll);
  xpollenode^=1;

  return 0;
}; /* DisplayKbd_XPoll */


#ifdef DIRECT_DISPLAY
static void UpdateROScreenFromVIDC(void)
{
  SelectROScreenMode((VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) * 2,
                      VIDC.Vert_DisplayEnd -  VIDC.Vert_DisplayStart,
                     (VIDC.ControlReg & 0xc) >> 2);
}
#endif


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
        UpdateROScreenFromVIDC();
#endif
      break;

    case 0x90:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz display end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayEnd,(val>>14) & 0x3ff);
#ifdef DIRECT_DISPLAY
      //if (DC.MustRedraw)
        UpdateROScreenFromVIDC();
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
        UpdateROScreenFromVIDC();
#endif
      break;

    case 0xb0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert disp end register val=%d\n",val>>14);
#endif
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_DisplayEnd,(val>>14) & 0x3ff);
#ifdef DIRECT_DISPLAY
      //if (DC.MustRedraw)
        UpdateROScreenFromVIDC();
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
        UpdateROScreenFromVIDC();
#endif
      break;

    default:
      fprintf(stderr,"Write to unknown VIDC register reg=0x%x val=0x%x\n",addr,val);
      break;

  }; /* Register switch */
}; /* PutValVIDC */

static void SelectROScreenMode(int x, int y, int bpp)
{
  int block[8];

  if (x<0 || x>1024 || y<0 || y>768 || bpp<0 || bpp>3)
    return;

  if (x==MonitorWidth && y==MonitorHeight && bpp==MonitorBpp)
    return;

  printf("setting screen mode to %dx%d at %d bpp\n",x, y, bpp);

  MonitorWidth  = x;
  MonitorHeight = y;
  MonitorBpp    = bpp;

  block[0] = 1;
  block[1] = x;
  block[2] = y;
  block[3] = bpp;
  block[4] = -1;

  switch (bpp)
  {
    case 3:
      block[5] = 3;
      block[6] = 255;
      break;

    default:
      block[5] =-1;
      break;
  }

  block[7] = -1;
  _swix(OS_ScreenMode, _INR(0,1), 0, &block);

  /* Remove text cursor from real RO */
  _swi(OS_RemoveCursors, 0);

#ifdef DIRECT_DISPLAY
  block[0] = 148;
  block[1] = 150;
  block[2] = -1;

  _swi(OS_ReadVduVariables, _INR(0,1), &block, &block);

  ROScreenMemory = (int *)block[0];
  ROScreenExtent = block[1];
#endif
}


