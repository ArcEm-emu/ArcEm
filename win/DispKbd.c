
#include <string.h>

#include "../armdefs.h"
#include "armarc.h"
#include "DispKbd.h"
#include "win.h"
#include "KeyTable.h"


#define HD HOSTDISPLAY
#define DC DISPLAYCONTROL

#define KEYREENABLEDELAY 1000
#define POLLGAP 125
#define AUTOREFRESHPOLL 2500

#define MonitorWidth 800
#define MonitorHeight 600

extern unsigned short *dibbmp;
extern unsigned short *curbmp;

static struct EventNode enodes[4];
static int xpollenode=2; /* Flips between 2 and 3 */

int rMouseX=0;
int rMouseY=0;
int rMouseHeight=0;
int oldMouseX = 0;
int oldMouseY = 0;

static void RefreshDisplay(ARMul_State *state);
static void Kbd_CodeFromHost(ARMul_State *state, unsigned char FromHost);
static void Kbd_StartToHost(ARMul_State *state);
static void ProcessKey(ARMul_State *state);
static void ProcessButton(ARMul_State *state);

static unsigned long get_pixelval(unsigned int red, unsigned int green, unsigned int blue) {
    return (((red >> (16 - HD.red_prec)) << HD.red_shift) +
           ((green >> (16 - HD.green_prec)) << HD.green_shift) +
           ((blue >> (16 - HD.blue_prec)) << HD.blue_shift));

} /* get_pixval */


static void MouseMoved(ARMul_State *state) {
  int xdiff,ydiff;

  /* We are now only using differences from the reference position */
//  if ((nMouseX==MonitorWidth/2) && (nMouseY==MonitorHeight/2)) return;

//  XWarpPointer(HD.disp,None,HD.MainPane,0,0,9999,9999,MonitorWidth/2,MonitorHeight/2);

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: CursorStart=%d xmotion->x=%d\n",
          VIDC.Horiz_CursorStart,xmotion->x);
#endif

  xdiff = -(oldMouseX - nMouseX);
  ydiff = oldMouseY - nMouseY;

  if (xdiff>63) xdiff=63;
  if (xdiff<-63) xdiff=-63;

  if (ydiff>63) ydiff=63;
  if (ydiff<-63) ydiff=-63;


  oldMouseX = nMouseX;
  oldMouseY = nMouseY;

  KBD.MouseXCount=xdiff & 127;
  KBD.MouseYCount=ydiff & 127;

  mouseMF = 0;
#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: generated counts %d,%d xdiff=%d ydifff=%d\n",KBD.MouseXCount,KBD.MouseYCount,xdiff,ydiff);
#endif
}; /* MouseMoved */

unsigned int DisplayKbd_XPoll(void *data)
{
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
      Kbd_CodeFromHost(state, (unsigned char)KbdSerialVal);
    } else {
      if (KBD.TimerIntHasHappened>2) {
        KBD.TimerIntHasHappened=0;
        if (KBD.KbdState == KbdState_Idle) Kbd_StartToHost(state);
      };
    };
  };

  if (keyF) ProcessKey(state);
  if (buttF) ProcessButton(state);
  if (mouseMF) MouseMoved(state);

  if (--(DC.AutoRefresh)<0) RefreshDisplay(state);
  ARMul_ScheduleEvent(&(enodes[xpollenode]),POLLGAP,DisplayKbd_XPoll);
  xpollenode^=1;
  return 0;
}

/*-----------------------------------------------------------------------------*/
/* I'm not confident that this is completely correct - if its wrong all hell
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
static int QueryRamChange(ARMul_State *state, unsigned int offset, int len) {
  unsigned int Vinit=MEMC.Vinit;
  unsigned int Vstart=MEMC.Vstart;
  unsigned int Vend=MEMC.Vend;
  unsigned int startblock,endblock,currentblock;

  /* Figure out if 'offset' starts between Vinit-Vend or Vstart-Vinit */

  if (offset < (((Vend-Vinit)+1)*16)) {
    /* Vinit-Vend */
    /* Now check to see if the whole buffer is in that area */
    if ((offset+len)>=(((Vend-Vinit)+1)*16)) {
      /* Its split - so copy the bit upto Vend and then the rest */

      /* Don't bother - this isn't going to happen much - lets say it changed */
      return(1);
    } else {
      offset+=Vinit*16;
    };
  } else {
    /* Vstart-Vinit */
    /* its all in one place */
    offset-=((Vend-Vinit)+1)*16; /* Thats the bit after Vinit */
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
static void CopyScreenRAM(ARMul_State *state, unsigned int offset, int len, char *Buffer) {
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
    /* its all in one place */
    offset-=((Vend-Vinit)+1)*16; /* Thats the bit after Vinit */
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
/* Configure the TrueColor pixelmap for the standard 1,2,4 bpp modes           */
static void DoPixelMap_Standard(ARMul_State *state) {
  char tmpstr[16];
  XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;


  for(c=0;c<16;c++) {
    tmp.red=(VIDC.Palette[c] & 15)<<12;
    tmp.green=((VIDC.Palette[c]>>4) & 15)<<12;
    tmp.blue=((VIDC.Palette[c]>>8) & 15)<<12;
    sprintf(tmpstr,"#%4x%4x%4x",tmp.red,tmp.green,tmp.blue);
//    tmp.pixel=c;
//printf("color %d = %s\n", c, tmpstr);
    /* I suppose I should do something with the supremacy bit....*/
    HD.pixelMap[c]=get_pixelval(tmp.red,tmp.green,tmp.blue);
  };
//getchar();
  /* Now do the ones for the cursor */
  for(c=0;c<3;c++) {
    tmp.red=(VIDC.CursorPalette[c] &15)<<12;
    tmp.green=((VIDC.CursorPalette[c]>>4) &15)<<12;
    tmp.blue=((VIDC.CursorPalette[c]>>8) &15)<<12;

    /* Entry 0 is transparent */
    HD.cursorPixelMap[c+1]=get_pixelval(tmp.red,tmp.green,tmp.blue);
  };

  DC.MustResetPalette=0;
}; /* DoPixelMap_Standard */
/*-----------------------------------------------------------------------------*/
/* Configure the true colour pixel map for the 8bpp modes                      */
static void DoPixelMap_256(ARMul_State *state) {
  XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<256;c++) {
    int palentry=c &15;
    int L4=(c>>4) &1;
    int L65=(c>>5) &3;
    int L7=(c>>7) &1;

    tmp.red=((VIDC.Palette[palentry] & 7) | (L4<<3))<<12;
    tmp.green=(((VIDC.Palette[palentry] >> 4) & 3) | (L65<<2))<<12;
    tmp.blue=(((VIDC.Palette[palentry] >> 8) & 7) | (L7<<3))<<12;
    /* I suppose I should do something with the supremacy bit....*/
    HD.pixelMap[c]=get_pixelval(tmp.red,tmp.green,tmp.blue);
  };

  /* Now do the ones for the cursor */
  for(c=0;c<3;c++) {
//    tmp.flags=DoRed|DoGreen|DoBlue;
//    tmp.pixel=c+CURSORCOLBASE;
    tmp.red=(VIDC.CursorPalette[c] &15)<<12;
    tmp.green=((VIDC.CursorPalette[c]>>4) &15)<<12;
    tmp.blue=((VIDC.CursorPalette[c]>>8) &15)<<12;

    HD.cursorPixelMap[c+1]=get_pixelval(tmp.red,tmp.green,tmp.blue);
  };

  DC.MustResetPalette=0;
}; /* DoPixelMap_256 */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_1bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/8];
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoPixelMap_Standard(state);

  if (DisplayHeight<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_1bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_1bpp: 0 or -ve display width\n");
    return;
  };

  /* Cope with screwy display widths/height */
  if (DisplayHeight>MonitorHeight) DisplayHeight=MonitorHeight;
  if (DisplayWidth>MonitorWidth) 
    VisibleDisplayWidth=MonitorWidth;
  else
    VisibleDisplayWidth=DisplayWidth;

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/8,ImgPtr+=MonitorWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,VisibleDisplayWidth/8))) {
      int yp = (MonitorHeight-y)*MonitorWidth;
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/8, Buffer);


      for(x=0;x<VisibleDisplayWidth;x+=8) {
	int xl = x+yp;
	int pixel = Buffer[x>>3];
	dibbmp[xl]   = (unsigned short)HD.pixelMap[pixel &1];
	dibbmp[xl+1] = (unsigned short)HD.pixelMap[(pixel>>1) &1];
	dibbmp[xl+2] = (unsigned short)HD.pixelMap[(pixel>>2) &1];
	dibbmp[xl+3] = (unsigned short)HD.pixelMap[(pixel>>3) &1];
	dibbmp[xl+4] = (unsigned short)HD.pixelMap[(pixel>>4) &1];
	dibbmp[xl+5] = (unsigned short)HD.pixelMap[(pixel>>5) &1];
	dibbmp[xl+6] = (unsigned short)HD.pixelMap[(pixel>>6) &1];
	dibbmp[xl+7] = (unsigned short)HD.pixelMap[(pixel>>7) &1];
      }; /* x */
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_TrueColor_1bpp */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_2bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/4];
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoPixelMap_Standard(state);

  if (DisplayHeight<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_2bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_2bpp: 0 or -ve display width\n");
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
      int yp = (MonitorHeight-y)*MonitorWidth;
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/4, Buffer);


      for(x=0;x<VisibleDisplayWidth;x+=4) {
	int pixs = yp + x;
        int pixel = Buffer[x>>2];

	dibbmp[pixs  ] = (unsigned short)HD.pixelMap[pixel      &3];
	dibbmp[pixs+1] = (unsigned short)HD.pixelMap[(pixel>>2) &3];
	dibbmp[pixs+2] = (unsigned short)HD.pixelMap[(pixel>>4) &3];
	dibbmp[pixs+3] = (unsigned short)HD.pixelMap[(pixel>>6) &3];

      }; /* x */
    }; /* Update test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_TrueColor_2bpp */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_4bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/2];
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoPixelMap_Standard(state);

  if (DisplayHeight<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_4bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_4bpp: 0 or -ve display width\n");
    return;
  };

  /*fprintf(stderr,"RefreshDisplay_TrueColor_4bpp: DisplayWidth=%d DisplayHeight=%d\n",
          DisplayWidth,DisplayHeight); */
  /* Cope with screwy display widths/height */
  if (DisplayHeight>MonitorHeight) DisplayHeight=MonitorHeight;
  if (DisplayWidth>MonitorWidth) 
    VisibleDisplayWidth=MonitorWidth;
  else
    VisibleDisplayWidth=DisplayWidth;

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=(DisplayWidth/2),ImgPtr+=MonitorWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,VisibleDisplayWidth/2))) {
      int yp = (MonitorHeight-y)*MonitorWidth;
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/2, Buffer);


      for(x=0;x<VisibleDisplayWidth;x+=2) {
	int pixs = x + yp;
	int pixel = Buffer[x>>1];

	dibbmp[pixs]     = (unsigned short)HD.pixelMap[pixel &15];
	dibbmp[pixs + 1] = (unsigned short)HD.pixelMap[(pixel>>4) &15];
      }; /* x */
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_TrueColor_4bpp */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_8bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int x,y,memoffset;
  int VisibleDisplayWidth;
  unsigned char Buffer[MonitorWidth];
  char *ImgPtr=HD.ImageData;
  
  /* First configure the colourmap */
  DoPixelMap_256(state);

  if (DisplayHeight<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_8bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
//    fprintf(stderr,"RefreshDisplay_TrueColor_8bpp: 0 or -ve display width\n");
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
      int yp = (MonitorHeight-y)*MonitorWidth;
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth, Buffer);


      for(x=0;x<VisibleDisplayWidth;x++) {

		dibbmp[x+yp] = (unsigned short)HD.pixelMap[Buffer[x]];
      }; /* X loop */
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_TrueColor_8bpp */


/*-----------------------------------------------------------------------------*/
/* Refresh the mouses image                                                    */
static void RefreshMouse(ARMul_State *state) {
  int x,y,height,offset, pix;
  int memptr;
  unsigned short *ImgPtr;
  int HorizPos = (int)VIDC.Horiz_CursorStart - (int)VIDC.Horiz_DisplayStart*2;
  int Height = (int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart; //+1;
  int VertPos;

  VertPos = (int)VIDC.Vert_CursorStart;
  VertPos -= (signed int)VIDC.Vert_DisplayStart;

  if (Height < 1) Height = 1;
  if (VertPos < 0) VertPos = 0;

  rMouseX = HorizPos;
  rMouseY = VertPos;
  rMouseHeight = Height;

  offset=0;
  memptr=MEMC.Cinit*16;
  height=(VIDC.Vert_CursorEnd-VIDC.Vert_CursorStart)+1;
  ImgPtr=(unsigned short *) HD.CursorImageData;
  for(y=0;y<height;y++,memptr+=8,offset+=8) {
    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

        for(x=0;x<32;x++,ImgPtr++) {
	    pix = ((tmp[x/16]>>((x & 15)*2)) & 3);
	    if (pix) curbmp[x+(MonitorHeight-y)*32] = (unsigned short)HD.cursorPixelMap[pix];
	    else curbmp[x+(MonitorHeight-y)*32] = dibbmp[rMouseX+x+(MonitorHeight-rMouseY-y)*MonitorWidth];
//curbmp[x+(MonitorHeight-y)*32] = HD.cursorPixelMap[((tmp[x/16]>>((x & 15)*2)) & 3)];
        }; /* x */
    } else return;
  }; /* y */
  updateDisplay();
}; /* RefreshMouse */

static void RefreshDisplay(ARMul_State *state) {
//sash
  DC.AutoRefresh=AUTOREFRESHPOLL;
  ioc.IRQStatus|=8; /* VSync */
  ioc.IRQStatus |= 0x20; /* Sound - just an experiment */
  IO_UpdateNirq();


//printf("refresh %d screen\n", (VIDC.ControlReg & 0xc)>>2);
//getchar();

  DC.miny=MonitorHeight-1;
  DC.maxy=0;

  /* First figure out number of BPP */
  switch ((VIDC.ControlReg & 0xc)>>2) {
      case 0: /* 1bpp */
        RefreshDisplay_TrueColor_1bpp(state);
        break;
      case 1: /* 2bpp */
        RefreshDisplay_TrueColor_2bpp(state);
        break;
      case 2: /* 4bpp */
        RefreshDisplay_TrueColor_4bpp(state);
        break;
      case 3: /* 8bpp */
        RefreshDisplay_TrueColor_8bpp(state);
        break;
  };
  
  /*fprintf(stderr,"RefreshDisplay: Refreshed %d-%d\n",DC.miny,DC.maxy); */
  /* Only tell X to redisplay those bits which we've replotted into the image */
  if (DC.miny<DC.maxy) {

//sash
//      RefreshMouse(state);

//      updateCursor();
      updateDisplay();
//    XPutImage(HD.disp,HD.MainPane,HD.MainPaneGC,HD.DisplayImage,
//              0,DC.miny, /* source pos. in image */
//              0,DC.miny, /* Position on window */
///              MonitorWidth,(DC.maxy-DC.miny)+1);

  };

//  XPutImage(HD.disp,HD.CursorPane,HD.MainPaneGC,HD.CursorImage,
//              0,0,
//              0,0,
//              32,((VIDC.Vert_CursorEnd-VIDC.Vert_CursorStart)-1));
}; /* RefreshDisplay */


/*-----------------------------------------------------------------------------*/
static void ProcessKey(ARMul_State *state) {
  struct ArcKeyTrans *PresPtr;
  int sym;
 
  sym = nVirtKey & 255;
  keyF = 0;

  if (KBD.BuffOcc>=KBDBUFFLEN) {
#ifdef DEBUG_KBD
    fprintf(stderr,"KBD: Missed key - still busy sending another one\n");
#endif
    return;
  };

  /* Should set up KeyColToSend and KeyRowToSend and KeyUpNDown */
  for(PresPtr = transTable; PresPtr->row != -1; PresPtr++) {
    if (PresPtr->sym==sym) {
      /* Found the key */
      /* Now add it to the buffer */
      KBD.Buffer[KBD.BuffOcc].KeyColToSend=PresPtr->col;
      KBD.Buffer[KBD.BuffOcc].KeyRowToSend=PresPtr->row;
      KBD.Buffer[KBD.BuffOcc].KeyUpNDown=nKeyStat;
      KBD.BuffOcc++;
#ifdef DEBUG_KBD
      fprintf(stderr,"ProcessKey: Got Col,Row=%d,%d UpNDown=%d BuffOcc=%d\n", 
              KBD.Buffer[KBD.BuffOcc].KeyColToSend,
               KBD.Buffer[KBD.BuffOcc].KeyRowToSend,
               KBD.Buffer[KBD.BuffOcc].KeyUpNDown,
              KBD.BuffOcc);
#endif
      return;
    };
  }; /* Key search loop */

#ifdef DEBUG_KBD
  fprintf(stderr,"ProcessKey: Unknown key sym=%04X!\n",sym);
#endif
}; /* ProcessKey */


/*-----------------------------------------------------------------------------*/
static void ProcessButton(ARMul_State *state) {
  int UpNDown=nButton >> 7;
  int ButtonNum= nButton & 3;

  /* Hey if you've got a 4 or more buttoned mouse hard luck! */
  if (ButtonNum<0) return;

  if (KBD.BuffOcc>=KBDBUFFLEN) {
#ifdef DEBUG_KBD
    fprintf(stderr,"KBD: Missed mouse event - buffer full\n");
#endif
    return;
  };

  /* Now add it to the buffer */
  KBD.Buffer[KBD.BuffOcc].KeyColToSend=ButtonNum;
  KBD.Buffer[KBD.BuffOcc].KeyRowToSend=7;
  KBD.Buffer[KBD.BuffOcc].KeyUpNDown=UpNDown;
  KBD.BuffOcc++;
#ifdef DEBUG_KBD
  fprintf(stderr,"ProcessButton: Got Col,Row=%d,%d UpNDown=%d BuffOcc=%d\n", 
          KBD.Buffer[KBD.BuffOcc].KeyColToSend,
           KBD.Buffer[KBD.BuffOcc].KeyRowToSend,
           KBD.Buffer[KBD.BuffOcc].KeyUpNDown,
          KBD.BuffOcc);
#endif
  buttF = 0;
}; /* ProcessButton */


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
printf("Mouse request! ------------------\n");
getchar();
      KBD.MouseXToSend=KBD.MouseXCount;
      KBD.MouseYToSend=KBD.MouseYCount;
      if (IOC_WriteKbdRx(state, (unsigned char)KBD.MouseXToSend)!=-1) {
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

    if (IOC_WriteKbdRx(state, (unsigned char)((KBD.KeyUpNDown ? 0xd0 : 0xc0) | KBD.KeyRowToSend))!=-1) {
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
    if (IOC_WriteKbdRx(state, (unsigned char)KBD.MouseXToSend)!=-1) {
      KBD.KbdState=KbdState_SentMouseByte1;
    };
    return;
  };

}; /* Kbd_StartToHost */

/*-----------------------------------------------------------------------------*/
/* Called when there is some data in the serial tx register on the IOC         */
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
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received Reset and sent Reset\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to Reset - cart full\n");
#endif
        };
#ifdef DEBUG_KBD
      } else {
        fprintf(stderr,"KBD: JustStarted; Got bad code 0x%x\n",FromHost);
#endif
      };
      break;

    case KbdState_SentHardReset:
      /* Only valid code is ack1 */
      if (FromHost==0xfe) {
        if (IOC_WriteKbdRx(state,0xfe)!=-1) {
          KBD.KbdState=KbdState_SentAck1;
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received Ack1 and sent Ack1\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to Ack1 - Kart full\n");
#endif
        };
      } else {
#ifdef DEBUG_KBD
        fprintf(stderr,"KBD: SentAck1; Got bad code 0x%x - sending reset\n",FromHost);
#endif
        IOC_WriteKbdRx(state,0xff);
        KBD.KbdState=KbdState_SentHardReset; /* Or should that be just started? */
      };
      break;

    case KbdState_SentAck1:
      /* Only valid code is Reset Ak 2 */
      if (FromHost==0xfd) {
        if (IOC_WriteKbdRx(state,0xfd)!=-1) {
          KBD.KbdState=KbdState_SentAck2;
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received and ack'd to Ack2\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to Ack2 - Kart full\n");
#endif
        };
#ifdef DEBUG_KBD
      } else {
        fprintf(stderr,"KBD: SentAck2; Got bad code 0x%x\n",FromHost);
#endif
      };
      break;

    default:
      if (FromHost==0xff) {
        if (IOC_WriteKbdRx(state,0xff)!=-1) {
          KBD.KbdState=KbdState_SentHardReset;
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Received and ack'd to hardware reset\n");
        } else {
          fprintf(stderr,"KBD: Couldn't respond to hardware reset - Kart full\n");
#endif
        };
        return;
      };

      switch (FromHost & 0xf0) {
        case 0: /* May be LED switch */
          if ((FromHost & 0x08)==0x08) {
#ifdef DEBUG_KBD
            fprintf(stderr,"KBD: Received bad code: 0x%x\n",FromHost);
#endif
            return;
          }
          /*printf("KBD: LED state now: 0x%x\n",FromHost & 0x7); */
          if (KBD.Leds!=(FromHost & 0x7)) {
            KBD.Leds=FromHost & 0x7;
//            ControlPane_RedrawLeds(state);
          };
#ifdef LEDENABLE
          /* I think we have to acknowledge - but I don't know with what */
          if (IOC_WriteKbdRx(state,0xa0 | (FromHost & 0x7))) {
            fprintf(stderr,"KBD: acked LEDs\n");
          } else {
            fprintf(stderr,"KBD: Couldn't respond to LED - Kart full\n");
          };
#endif
          break;

        case 0x20: /* Host requests keyboard id - or mouse position */
          KBD.HostCommand=FromHost;
          Kbd_StartToHost(state);
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Host requested keyboard ID\n");
#endif
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
#ifdef DEBUG_KBD
                fprintf(stderr,"KBD: Got 1st byte ack when we haven't sent one!\n");
#endif
              } else {
                if (KBD.KbdState==KbdState_SentMouseByte1) {
                  if (IOC_WriteKbdRx(state, (unsigned char)KBD.MouseYToSend)==-1) {
#ifdef DEBUG_KBD
                    fprintf(stderr,"KBD: Couldn't send 2nd byte of mouse value - Kart full\n");
#endif
                  };
                  KBD.KbdState=KbdState_SentMouseByte2;
                } else {
                  if (IOC_WriteKbdRx(state, (unsigned char)((KBD.KeyUpNDown?0xd0:0xc0) | KBD.KeyColToSend))==-1) {
#ifdef DEBUG_KBD
                    fprintf(stderr,"KBD: Couldn't send 2nd byte of key value - Kart full\n");
#endif
                  };
                  KBD.KbdState=KbdState_SentKeyByte2;
                  /* Indicate that the key has been sent */
                  KBD.KeyRowToSend=-1;
                  KBD.KeyColToSend=-1;
                };
              }; /* Have sent 1st byte test */
              break;

#ifdef DEBUG_KBD
            default:
              fprintf(stderr,"KBD: Bad ack type received 0x%x\n",FromHost);
              break;
#endif
          }; /* bottom nibble of ack switch */
          return;

        case 0x40: /* Host just sends us some data....*/
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Host sent us some data: 0x%x\n",FromHost);
#endif
          return;

        default:
#ifdef DEBUG_KBD
          fprintf(stderr,"KBD: Unknown code received from host 0x%x\n",FromHost);
#endif
          return;
      }; /* FromHost top nybble switch */
  }; /* current state switch */
}; /* Kbd_CodeFromHost */


void DisplayKbd_Init(ARMul_State *state)
{
  int index;
  HD.ImageData=malloc(4*(MonitorWidth+32)*MonitorHeight);
  if (HD.ImageData==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't allocate image memory\n");
    exit(1);
  };

  /* Now the same for the cursor image */
  HD.CursorImageData=malloc(4*64*MonitorHeight);
  if (HD.CursorImageData==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't allocate cursor image memory\n");
    exit(1);
  };


  createWindow(MonitorWidth, MonitorHeight);

  HD.red_prec = 5;
  HD.green_prec = 5;
  HD.blue_prec = 5;
  HD.red_shift = 10;
  HD.green_shift = 5;
  HD.blue_shift = 0;


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

  /* Note the memory model sets its to 1 - note the ordering */
  /* i.e. it must be updated */
  for(index=0;index<(512*1024)/UPDATEBLOCKSIZE;index++)
    DC.UpdateFlags[index]=0;

  ARMul_ScheduleEvent(&(enodes[xpollenode]),POLLGAP,DisplayKbd_XPoll);
  xpollenode^=1;
}

void VIDC_PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw)
{
  unsigned int addr, val;

  addr=(data>>24) & 255;
  val=data & 0xffffff;

  if (!(addr & 0xc0)) {
    int Log, Sup,Red,Green,Blue;

    /* This lot presumes its not 8bpp mode! */
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
      resizeWindow((VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,
		   VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart);
      break;

    case 0x90:
#ifdef DEBUG_VIDCREGS   
      fprintf(stderr,"VIDC Horiz display end register val=%d\n",val>>14);
#endif   
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayEnd,(val>>14) & 0x3ff);
      resizeWindow((VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,
		   VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart);
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
//      DC.MustRedraw = 1;
///sash
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
      resizeWindow((VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,
		   VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart);
      break;

    case 0xb0:
#ifdef DEBUG_VIDCREGS   
      fprintf(stderr,"VIDC Vert disp end register val=%d\n",val>>14);
#endif   
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_DisplayEnd,(val>>14) & 0x3ff);
      resizeWindow((VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,
		   VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart);
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
      RefreshMouse(state);
//sash
      break;

    case 0xbc:
#ifdef DEBUG_VIDCREGS   
      fprintf(stderr,"VIDC Vert cursor end register val=%d\n",val>>14);
#endif   
      VIDC.Vert_CursorEnd=(val>>14) & 0x3ff;
      RefreshMouse(state);
//sash
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
      break;

    default:
      fprintf(stderr,"Write to unknown VIDC register reg=0x%x val=0x%x\n",addr,val);
      break;

  }; /* Register switch */
}

