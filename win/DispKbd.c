#include <string.h>



#include "../armdefs.h"
#include "armarc.h"
#include "../arch/keyboard.h"
#include "DispKbd.h"
#include "win.h"
#include "KeyTable.h"


#define HD HOSTDISPLAY
#define DC DISPLAYCONTROL



#define KEYREENABLEDELAY 1000
#define AUTOREFRESHPOLL 2500

#define MonitorWidth 800
#define MonitorHeight 600



extern unsigned short *dibbmp;
extern unsigned short *curbmp;



int rMouseX = 0;
int rMouseY = 0;
int rMouseHeight = 0;
int oldMouseX = 0;
int oldMouseY = 0;



static void ProcessKey(ARMul_State *state);
static void ProcessButton(ARMul_State *state);



static unsigned long get_pixelval(ARMul_State *state,unsigned int red, unsigned int green, unsigned int blue) {
    return (((red >> (16 - HD.red_prec)) << HD.red_shift) +
           ((green >> (16 - HD.green_prec)) << HD.green_shift) +
           ((blue >> (16 - HD.blue_prec)) << HD.blue_shift));
} /* get_pixval */



static void MouseMoved(ARMul_State *state) {
  int xdiff, ydiff;

  xdiff = -(oldMouseX - nMouseX);
  ydiff = oldMouseY - nMouseY;

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: xdiff = %d  ydiff = %d\n",
          xdiff, ydiff);
#endif

  if (xdiff > 63)
    xdiff=63;
  if (xdiff < -63)
    xdiff=-63;

  if (ydiff>63)
    ydiff=63;
  if (ydiff<-63)
    ydiff=-63;

  oldMouseX = nMouseX;
  oldMouseY = nMouseY;

  KBD.MouseXCount = xdiff & 127;
  KBD.MouseYCount = ydiff & 127;

  mouseMF = 0;
#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: generated counts %d,%d xdiff=%d ydifff=%d\n",KBD.MouseXCount,KBD.MouseYCount,xdiff,ydiff);
#endif
}; /* MouseMoved */



int
DisplayKbd_PollHost(ARMul_State *state)
{
  if (keyF) {
    ProcessKey(state);
  }
  if (buttF) {
    ProcessButton(state);
  }
  if (mouseMF) {
    MouseMoved(state);
  }
  return 0;
}



/*-----------------------------------------------------------------------------*/






/*-----------------------------------------------------------------------------*/
/* Configure the TrueColor pixelmap for the standard 1,2,4 bpp modes           */
static void DoPixelMap_Standard(ARMul_State *state)
{
  char tmpstr[16];
  XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) {
    return;
  }

  for(c=0;c<16;c++) {
    tmp.red   = (VIDC.Palette[c] & 15) << 12;
    tmp.green = ((VIDC.Palette[c] >> 4) & 15) << 12;
    tmp.blue  = ((VIDC.Palette[c] >> 8) & 15) << 12;
    sprintf(tmpstr, "#%4x%4x%4x", tmp.red, tmp.green, tmp.blue);
//    tmp.pixel=c;
//printf("color %d = %s\n", c, tmpstr);
    /* I suppose I should do something with the supremacy bit....*/
    HD.pixelMap[c] = get_pixelval(state, tmp.red, tmp.green, tmp.blue);
  }
//getchar();
  /* Now do the ones for the cursor */
  for(c = 0; c < 3; c++) {
    tmp.red   = (VIDC.CursorPalette[c] &15) << 12;
    tmp.green = ((VIDC.CursorPalette[c] >> 4) & 15) << 12;
    tmp.blue  = ((VIDC.CursorPalette[c] >> 8) & 15) << 12;

    /* Entry 0 is transparent */
    HD.cursorPixelMap[c + 1] = get_pixelval(state, tmp.red,tmp.green,tmp.blue);
  }

  DC.MustResetPalette = 0;
} /* DoPixelMap_Standard */

/*-----------------------------------------------------------------------------*/
/* Configure the true colour pixel map for the 8bpp modes                      */
static void DoPixelMap_256(ARMul_State *state)
{
  XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) {
    return;
  }

  for(c=0; c<256; c++) {
    int palentry = c & 15;
    int L4  = (c >> 4) & 1;
    int L65 = (c >> 5) & 3;
    int L7  = (c >> 7) & 1;

    tmp.red   = ((VIDC.Palette[palentry] & 7) | (L4 << 3)) << 12;
    tmp.green = (((VIDC.Palette[palentry] >> 4) & 3) | (L65 << 2)) << 12;
    tmp.blue  = (((VIDC.Palette[palentry] >> 8) & 7) | (L7 << 3)) << 12;
    /* I suppose I should do something with the supremacy bit....*/
    HD.pixelMap[c] = get_pixelval(state, tmp.red, tmp.green, tmp.blue);
  }

  /* Now do the ones for the cursor */
  for(c=0; c<3; c++) {
//    tmp.flags=DoRed|DoGreen|DoBlue;
//    tmp.pixel=c+CURSORCOLBASE;
    tmp.red   = (VIDC.CursorPalette[c] &15) << 12;
    tmp.green = ((VIDC.CursorPalette[c] >> 4) &15) << 12;
    tmp.blue  = ((VIDC.CursorPalette[c] >> 8) &15) << 12;

    HD.cursorPixelMap[c + 1] = get_pixelval(state, tmp.red, tmp.green, tmp.blue);
  }

  DC.MustResetPalette = 0;
} /* DoPixelMap_256 */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_1bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight)
{
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth / 8];
  char *ImgPtr = HD.ImageData;

  /* First configure the colourmap */
  DoPixelMap_Standard(state);

  /* Cope with screwy display widths/height */
  if (DisplayHeight>MonitorHeight) {
    DisplayHeight=MonitorHeight;
  }
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
static void RefreshDisplay_TrueColor_2bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight) {
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/4];
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoPixelMap_Standard(state);

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

static void RefreshDisplay_TrueColor_4bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight) {
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/2];
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoPixelMap_Standard(state);

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
static void RefreshDisplay_TrueColor_8bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight) {
  int x,y,memoffset;
  int VisibleDisplayWidth;
  unsigned char Buffer[MonitorWidth];
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoPixelMap_256(state);

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
/* Refresh the mouse's image                                                    */
static void RefreshMouse(ARMul_State *state) {
  int x,y,height,offset, pix;
  int memptr;
  unsigned short *ImgPtr;
  int HorizPos = (int)VIDC.Horiz_CursorStart - (int)VIDC.Horiz_DisplayStart*2;
  int Height = (int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart; //+1;
  int VertPos;
  int diboffs;

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
        diboffs = rMouseX + x + (MonitorHeight - rMouseY - y) * MonitorWidth;

        curbmp[x + (MonitorHeight - y) * 32] =
                (pix || diboffs < 0) ?
                (unsigned short)HD.cursorPixelMap[pix] : dibbmp[diboffs];
      }; /* x */
    } else return;
  }; /* y */
  updateDisplay();
}; /* RefreshMouse */

void
RefreshDisplay(ARMul_State *state,CycleCount nowtime)
{
  EventQ_RescheduleHead(state,nowtime+POLLGAP*AUTOREFRESHPOLL,RefreshDisplay);
  int DisplayHeight = VIDC.Vert_DisplayEnd - VIDC.Vert_DisplayStart;
  int DisplayWidth  = (VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) * 2;

  ioc.IRQStatus|=8; /* VSync */
  ioc.IRQStatus |= 0x20; /* Sound - just an experiment */
  IO_UpdateNirq(state);

  DC.miny=MonitorHeight-1;
  DC.maxy=0;

  if (DisplayHeight > 0 && DisplayWidth > 0) {
    /* First figure out number of BPP */
    switch ((VIDC.ControlReg & 0xc)>>2) {
      case 0: /* 1bpp */
        RefreshDisplay_TrueColor_1bpp(state, DisplayWidth, DisplayHeight);
        break;
      case 1: /* 2bpp */
        RefreshDisplay_TrueColor_2bpp(state, DisplayWidth, DisplayHeight);
        break;
      case 2: /* 4bpp */
        RefreshDisplay_TrueColor_4bpp(state, DisplayWidth, DisplayHeight);
        break;
      case 3: /* 8bpp */
        RefreshDisplay_TrueColor_8bpp(state, DisplayWidth, DisplayHeight);
        break;
    }
  }

  /* Only tell X to redisplay those bits which we've replotted into the image */
  if (DC.miny < DC.maxy) {
    updateDisplay();
  }

} /* RefreshDisplay */


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
  int UpNDown   = nButton >> 7;
  int ButtonNum = nButton & 3;

  /* Hey if you've got a 4 or more buttoned mouse hard luck! */
  if (ButtonNum < 0)  {
    return;
  }

  if (KBD.BuffOcc >= KBDBUFFLEN) {
#ifdef DEBUG_KBD
    fprintf(stderr, "KBD: Missed mouse event - buffer full\n");
#endif
    return;
  }

  /* Now add it to the buffer */
  KBD.Buffer[KBD.BuffOcc].KeyColToSend = ButtonNum;
  KBD.Buffer[KBD.BuffOcc].KeyRowToSend = 7;
  KBD.Buffer[KBD.BuffOcc].KeyUpNDown   = UpNDown;
  KBD.BuffOcc++;
#ifdef DEBUG_KBD
  fprintf(stderr,"ProcessButton: Got Col,Row=%d,%d UpNDown=%d BuffOcc=%d\n",
          KBD.Buffer[KBD.BuffOcc].KeyColToSend,
          KBD.Buffer[KBD.BuffOcc].KeyRowToSend,
          KBD.Buffer[KBD.BuffOcc].KeyUpNDown,
          KBD.BuffOcc);
#endif
  buttF = 0;
} /* ProcessButton */

void
DisplayKbd_InitHost(ARMul_State *state)
{
  /* Setup display and cursor bitmaps */
  createWindow(MonitorWidth, MonitorHeight);

  HD.red_prec    = 5;
  HD.green_prec  = 5;
  HD.blue_prec   = 5;
  HD.red_shift   = 10;
  HD.green_shift = 5;
  HD.blue_shift  = 0;
  EventQ_Insert(state,ARMul_Time+POLLGAP*AUTOREFRESHPOLL,RefreshDisplay);
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

