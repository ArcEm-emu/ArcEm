/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */
/* Display and keyboard interface for the Arc emulator */

/* !!! Now does TrueColor and PseudoColor X modes; but doesnt handle palette changes
properly in TrueColor - in particular it doesnt force a redraw if the palette is changed
which it really needs to */

#define MOUSEKEY XK_KP_Add

#define KEYREENABLEDELAY 1000
#define POLLGAP 125
//#define POLLGAP 1250
/*#define DEBUG_VIDCREGS*/
/* NOTE: Can't use ARMul's refresh function because it has a small limit on the time
   delay from posting the event to it executing */
/* Its actually decremented once every POLLGAP - that is called with the ARMul
   schedular */
#define AUTOREFRESHPOLL 2500

#include <stdio.h>
#include <limits.h>

#include "X11/X.h"
#include "X11/Xlib.h"
#include "X11/Xutil.h"
#include "X11/keysym.h"
#include "X11/extensions/shape.h"

#include "../armdefs.h"
#include "armarc.h"
#include "DispKbd.h"
#include "KeyTable.h"
#include "archio.h"
#include "hdc63463.h"

#include "ControlPane.h" 

#define MonitorWidth 800
#define MonitorHeight 600
#define ControlHeight 30
#define CURSORCOLBASE 250

/* HOSTDISPLAY is too verbose here - but HD could be hard disc somewhere else! */
#define HD HOSTDISPLAY
#define DC DISPLAYCONTROL

static unsigned AutoKey(ARMul_State *state);
unsigned DisplayKbd_XPoll(ARMul_State *state);

static struct EventNode enodes[4];
//static int autokeyenode=0; /* Flips between 0 and 1 */
static int xpollenode=2; /* Flips between 2 and 3 */


/*-----------------------------------------------------------------------------*/
/* From the GIMP Drawing Kit, in the GTK+ toolkit from GNOME                   */
/* Given a colour mask from a visual extract shift and bit precision values    */
static void
gdk_visual_decompose_mask (unsigned long  mask,
         int   *shift,
         int   *prec)
{ 
  *shift = 0;
  *prec = 0;

  while (!(mask & 0x1))
    {
      (*shift)++;
      mask >>= 1;
    }

  while (mask & 0x1)
    {
      (*prec)++;
      mask >>= 1;
    }
}

/*-----------------------------------------------------------------------------*/
/* Also borrowed from GDK (with a little rework).  Get the XPixel value (as
   passed to XPutPixel) based on 16 bit colour values                          */
static unsigned long get_pixelval(unsigned int red, unsigned int green, unsigned int blue) {
    return (((red >> (16 - HD.red_prec)) << HD.red_shift) +
           ((green >> (16 - HD.green_prec)) << HD.green_shift) +
           ((blue >> (16 - HD.blue_prec)) << HD.blue_shift));

} /* get_pixval */

/*-----------------------------------------------------------------------------*/
static unsigned AutoKey(ARMul_State *state) {
  /*fprintf(stderr,"AutoKey!\n"); */
  KBD.TimerIntHasHappened+=2;

  return 0;
};
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

/*-----------------------------------------------------------------------------*/
/* Check to see if the area of memory has changed.                             */
/* Returns true if there is any chance that the given area has changed         */
int QueryRamChange(ARMul_State *state,int offset, int len) {
  unsigned int Vinit=MEMC.Vinit;
  unsigned int Vstart=MEMC.Vstart;
  unsigned int Vend=MEMC.Vend;
  unsigned int startblock,endblock,currentblock;

  /* Figure out if 'offset' starts between Vinit-Vend or Vstart-Vinit */
  if ((offset)<(((Vend-Vinit)+1)*16)) {
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
      /* It's split - so copy the bit upto Vend and then the rest */
      int tmplen;

      offset+=Vinit*16;
      tmplen=(Vend+1)*16-offset;
      /*fprintf(stderr,"CopyScreenRAM: Split over Vend offset now=0x%x tmplen=%d\n",offset,tmplen); */
      memcpy(Buffer,MEMC.PhysRam+(offset/sizeof(ARMword)),tmplen);
      memcpy(Buffer+tmplen,MEMC.PhysRam+((Vstart*16)/sizeof(ARMword)),len-tmplen);
    } else {
      /* It's all there */
      /*fprintf(stderr,"CopyScreenRAM: All in one piece between Vinit..Vend offset=0x%x\n",offset); */
      offset+=Vinit*16;
      memcpy(Buffer,MEMC.PhysRam+(offset/sizeof(ARMword)),len);
    };
  } else {
    /* Vstart-Vinit */
    /* It's all in one place */
    offset-=((Vend-Vinit)+1)*16; /* That's the bit after Vinit */
    offset+=Vstart*16; /* so the bit we copy is somewhere after Vstart */
    /*fprintf(stderr,"CopyScreenRAM: All in one piece between Vstart..Vinit offset=0x%x\n",offset); */
    memcpy(Buffer,MEMC.PhysRam+(offset/sizeof(ARMword)),len);
  };

#ifdef HOST_BIGENDIAN
/* Hacking of the buffer now - OK - I know that I should do this neater */
  for(offset = 0; offset < len; offset += 4) {
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
/* Configure the colourmap for the standard 1,2,4 bpp modes                    */
static void DoColourMap_Standard(ARMul_State *state) {
  XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<16;c++) {
    tmp.flags=DoRed|DoGreen|DoBlue;
    tmp.pixel=c;
    tmp.red=(VIDC.Palette[c] & 15)<<12;
    tmp.green=((VIDC.Palette[c]>>4) & 15)<<12;
    tmp.blue=((VIDC.Palette[c]>>8) & 15)<<12;
    /* I suppose I should do something with the supremacy bit....*/
    XStoreColor(HD.disp,HD.ArcsColormap,&tmp); /* Should use XStoreColors */
  };

  /* Now do the ones for the cursor */
  for(c=0;c<2;c++) {
    tmp.flags=DoRed|DoGreen|DoBlue;
    tmp.pixel=c+CURSORCOLBASE;
    tmp.red=(VIDC.CursorPalette[c] &15)<<12;
    tmp.green=((VIDC.CursorPalette[c]>>4) &15)<<12;
    tmp.blue=((VIDC.CursorPalette[c]>>8) &15)<<12;

    XStoreColor(HD.disp,HD.ArcsColormap,&tmp);
  };

  DC.MustResetPalette=0;
}; /* DoColourMap_Standard */
/*-----------------------------------------------------------------------------*/
/* Configure the colourmap for the 8bpp modes                                  */
static void DoColourMap_256(ARMul_State *state) {
  XColor tmp;
  int c;

  if (!(DC.MustRedraw || DC.MustResetPalette)) return;

  for(c=0;c<256;c++) {
    int palentry=c &15;
    int L4=(c>>4) &1;
    int L65=(c>>5) &3;
    int L7=(c>>7) &1;

    tmp.flags=DoRed|DoGreen|DoBlue;
    tmp.pixel=c;
    tmp.red=((VIDC.Palette[palentry] & 7) | (L4<<3))<<12;
    tmp.green=(((VIDC.Palette[palentry] >> 4) & 3) | (L65<<2))<<12;
    tmp.blue=(((VIDC.Palette[palentry] >> 8) & 7) | (L7<<3))<<12;
    /* I suppose I should do something with the supremacy bit....*/
    XStoreColor(HD.disp,HD.ArcsColormap,&tmp); /* Should use XStoreColors */
  };
  DC.MustResetPalette=0;
}; /* DoColourMap_Standard */

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
    tmp.pixel=c;
    /* I suppose I should do something with the supremacy bit....*/
    HD.pixelMap[c]=get_pixelval(tmp.red,tmp.green,tmp.blue);
  };

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
    tmp.flags=DoRed|DoGreen|DoBlue;
    tmp.pixel=c+CURSORCOLBASE;
    tmp.red=(VIDC.CursorPalette[c] &15)<<12;
    tmp.green=((VIDC.CursorPalette[c]>>4) &15)<<12;
    tmp.blue=((VIDC.CursorPalette[c]>>8) &15)<<12;

    HD.cursorPixelMap[c+1]=get_pixelval(tmp.red,tmp.green,tmp.blue);
  };

  DC.MustResetPalette=0;
}; /* DoPixelMap_256 */

/*-----------------------------------------------------------------------------*/
/* Refresh the mouses image                                                    */
static void RefreshMouse(ARMul_State *state) {
  int x,y,height,offset;
  int memptr,TransBit;
  char *ImgPtr,*TransPtr;

  offset=0;
  memptr=MEMC.Cinit*16;
  height=(VIDC.Vert_CursorEnd-VIDC.Vert_CursorStart)+1;
  ImgPtr=HD.CursorImageData;
  TransPtr=HD.ShapePixmapData;
  TransBit=0;
  for(y=0;y<height;y++,memptr+=8,offset+=8,TransPtr+=4) {
    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      if (HD.visInfo.class==PseudoColor) {
        for(x=0;x<32;x++,ImgPtr++,TransBit<<=1) {
          *ImgPtr=CURSORCOLBASE+-1+((tmp[x/16]>>((x & 15)*2)) & 3);
          if ((x&7)==0) {
            TransPtr[x/8]=0xff;
            TransBit=1;
          };
          TransPtr[x/8]&=((tmp[x/16]>>((x & 15)*2)) & 3)?~TransBit:0xff;
        }; /* x */
      } else {
        for(x=0;x<32;x++,ImgPtr++,TransBit<<=1) {
          XPutPixel(HD.CursorImage,x,y,HD.cursorPixelMap[((tmp[x/16]>>((x & 15)*2)) & 3)]);
          if ((x&7)==0) {
            TransPtr[x/8]=0xff;
            TransBit=1;
          };
          TransPtr[x/8]&=((tmp[x/16]>>((x & 15)*2)) & 3)?~TransBit:0xff;
        }; /* x */
      }; /* True color */
    } else return;
  }; /* y */

  if (HD.ShapeEnabled) {
    HD.shape_mask=XCreatePixmapFromBitmapData(HD.disp,HD.CursorPane,HD.ShapePixmapData,
                                             32,MonitorHeight,0,1,1);
    /* Eek - a lot of this is copied from XEyes - unfortunatly the manual
    page for this call is somewhat lacking.  To quote its bugs entry:
      'This manual page needs a lot more work' */
    XShapeCombineMask(HD.disp,HD.CursorPane,ShapeBounding,0,0,HD.shape_mask,
                      ShapeSet);
    XFreePixmap(HD.disp,HD.shape_mask);
  }; /* Shape enabled */
}; /* RefreshMouse */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_PseudoColor_1bpp(ARMul_State *state) {
  int DisplayHeight=VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart;
  int DisplayWidth=(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2;
  int x,y,memoffset;
  int VisibleDisplayWidth;
  char Buffer[MonitorWidth/8];
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
   DoColourMap_Standard(state);

  if (DisplayHeight<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_1bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_1bpp: 0 or -ve display width\n");
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
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoColourMap_Standard(state);

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
  char *ImgPtr=HD.ImageData;

  /* First configure the colourmap */
  DoColourMap_Standard(state);

  if (DisplayHeight<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_4bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_PseudoColor_4bpp: 0 or -ve display width\n");
    return;
  };

  /*fprintf(stderr,"RefreshDisplay_PseudoColor_4bpp: DisplayWidth=%d DisplayHeight=%d\n",
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
  char *ImgPtr=HD.ImageData;
  
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
    fprintf(stderr,"RefreshDisplay_TrueColor_1bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_TrueColor_1bpp: 0 or -ve display width\n");
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
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/8, Buffer);

      for(x=0;x<VisibleDisplayWidth;x+=8) {
        int bit;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(bit=0;bit<=8;bit++) {
          XPutPixel(HD.DisplayImage,x+bit,y,HD.pixelMap[(Buffer[x/8]>>bit) &1]);
        }; /* bit */
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
    fprintf(stderr,"RefreshDisplay_TrueColor_2bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_TrueColor_2bpp: 0 or -ve display width\n");
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
          XPutPixel(HD.DisplayImage,x+pixel,y,HD.pixelMap[(Buffer[x/4]>>(pixel*2)) &3]);
        }; /* pixel */
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
    fprintf(stderr,"RefreshDisplay_TrueColor_4bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_TrueColor_4bpp: 0 or -ve display width\n");
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
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth/2, Buffer);

      for(x=0;x<VisibleDisplayWidth;x+=2) {
        int pixel;
        /* We are now running along the scan line */
        /* we'll get this a bit more efficient when it works! */
        for(pixel=0;pixel<2;pixel++) {
          XPutPixel(HD.DisplayImage,x+pixel,y,HD.pixelMap[(Buffer[x/2]>>(pixel*4)) &15]);
        }; /* pixel */
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
    fprintf(stderr,"RefreshDisplay_TrueColor_8bpp: 0 or -ve display height\n");
    return;
  };

  if (DisplayWidth<=0) {
    fprintf(stderr,"RefreshDisplay_TrueColor_8bpp: 0 or -ve display width\n");
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
      CopyScreenRAM(state,memoffset,VisibleDisplayWidth, Buffer);

      for(x=0;x<VisibleDisplayWidth;x++) {
        XPutPixel(HD.DisplayImage,x,y,HD.pixelMap[Buffer[x]]);
      }; /* X loop */
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
} /* RefreshDisplay_TrueColor_8bpp */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay(ARMul_State *state) {
  DC.AutoRefresh=AUTOREFRESHPOLL;
  ioc.IRQStatus|=8; /* VSync */
  ioc.IRQStatus |= 0x20; /* Sound - just an experiment */
  IO_UpdateNirq();

  RefreshMouse(state);

  DC.miny=MonitorHeight-1;
  DC.maxy=0;

  /* First figure out number of BPP */
  if (HD.visInfo.class==PseudoColor) {
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
  } else {
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
  };

  /*fprintf(stderr,"RefreshDisplay: Refreshed %d-%d\n",DC.miny,DC.maxy); */
  /* Only tell X to redisplay those bits which we've replotted into the image */
  if (DC.miny<DC.maxy) {
    XPutImage(HD.disp,HD.MainPane,HD.MainPaneGC,HD.DisplayImage,
              0,DC.miny, /* source pos. in image */
              0,DC.miny, /* Position on window */
              MonitorWidth,(DC.maxy-DC.miny)+1);
  };

  XPutImage(HD.disp,HD.CursorPane,HD.MainPaneGC,HD.CursorImage,
              0,0,
              0,0,
              32,((VIDC.Vert_CursorEnd-VIDC.Vert_CursorStart)-1));
}; /* RefreshDisplay */

/*-----------------------------------------------------------------------------*/
static int DisplayKbd_XError(Display* disp, XErrorEvent *err) {
  char errbuf[1024];
  XGetErrorText(disp,err->error_code,errbuf,1023);

  fprintf(stderr,"Oh heck! We just got an X error - it said: '%s'\n",errbuf);
  fprintf(stderr,"\n\nThat probably shouldn't have happened!\n\nBUT\n");
  fprintf(stderr,"Oh - you'll probably get an error if you just close my window to shut me down - but don't worry about that...\n");
  fprintf(stderr,"\n\nIf that is NOT the problem then report the bug to Dave at arcem@treblig.org\n");
  exit(1);
};
/*-----------------------------------------------------------------------------*/
void DisplayKbd_Init(ARMul_State *state) {
  XColor tmpcol;
  XGCValues gctmpval;
  int prescol;
  int index;
  XTextProperty name;
  char *tmpptr;
  int shape_event_base, shape_error_base;

  HD.disp=XOpenDisplay(NULL);
  if (HD.disp==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't open display\n");
    exit(1);
  };

  XSetErrorHandler(DisplayKbd_XError);

  XSynchronize(HD.disp,1);

  HD.xScreen=XDefaultScreenOfDisplay(HD.disp);
  HD.ScreenNum=XScreenNumberOfScreen(HD.xScreen);
  HD.RootWindow=DefaultRootWindow(HD.disp);

  /* Try and find a visual we can work with */
  if (XMatchVisualInfo(HD.disp,HD.ScreenNum,8,PseudoColor,&(HD.visInfo))) {
    /* Its 8 bpp, Pseudo colour - the easiest to deal with */
    printf("Ah - a nice easy 8bpp visual....\n");
  } else {
    /* Could probably cope with a wider range of visuals - particularly
    any other true colour probably? */
    /* NOTE: this is using short circuit eval to only evaluate upto the first match */
    if ((XMatchVisualInfo(HD.disp,HD.ScreenNum,24,TrueColor,&(HD.visInfo))) ||
        (XMatchVisualInfo(HD.disp,HD.ScreenNum,32,TrueColor,&(HD.visInfo))) || 
        (XMatchVisualInfo(HD.disp,HD.ScreenNum,16,TrueColor,&(HD.visInfo))) ||
        (XMatchVisualInfo(HD.disp,HD.ScreenNum,15,TrueColor,&(HD.visInfo)))) {
      /* True colour - extract the shift and precision values */
      gdk_visual_decompose_mask(HD.visInfo.visual->red_mask,&(HD.red_shift),&(HD.red_prec));
      gdk_visual_decompose_mask(HD.visInfo.visual->green_mask,&(HD.green_shift),&(HD.green_prec));
      gdk_visual_decompose_mask(HD.visInfo.visual->blue_mask,&(HD.blue_shift),&(HD.blue_prec));
      printf("Shift/masks = r/g/b = %d/%d,%d/%d,%d/%d\n",HD.red_shift,HD.red_prec,
                                                         HD.green_shift,HD.green_prec,
                                                         HD.blue_shift,HD.blue_prec);
    } else {
      fprintf(stderr,"DisplayKbd_Init: Failed to find a matching visual - I'm looking for either 8 bit Pseudo colour, or 32,24,16,  or 15 bit TrueColour - sorry\n");
      exit(1);
    };
  };
  HD.BackingWindow=XCreateWindow(HD.disp,
                                          HD.RootWindow,
                                          500,500, /* Position on root */
                                          MonitorWidth,MonitorHeight,
                                          0, /* border width */
                                          HD.visInfo.depth, /* depth */
                                          InputOutput,
                                          HD.visInfo.visual,
                                          0, /* valuemask */
                                          NULL /* attribs */);
  tmpptr="Arc emulator - Main display";
  if (XStringListToTextProperty(&tmpptr,1,&name)==0) {
    fprintf(stderr,"Could not allocate window name\n");
    exit(1);
  };
  XSetWMName(HD.disp,HD.BackingWindow,&name);
  XFree(name.value);

  HD.MainPane=XCreateWindow(HD.disp,
                                     HD.BackingWindow,
                                     0,0, /* Bottom left of backing window */
                                     MonitorWidth,MonitorHeight,
                                     0, /* Border width */
                                     CopyFromParent, /* depth */
                                     InputOutput, /* class */
                                     CopyFromParent, /* visual */
                                     0, /* valuemask */
                                     NULL /* attribs */);

  HD.CursorPane=XCreateWindow(HD.disp,
                              HD.MainPane,
                              0,0, 
                              32,MonitorHeight,
                              0, /* Border width */
                              CopyFromParent, /* depth */
                              InputOutput, /* class */
                              CopyFromParent, /* visual */
                              0, /* valuemask */
                              NULL /* attribs */);



  /* Allocate the memory for the actual display image */
  //TODO!! Need to allocate more for truecolour
  HD.ImageData=malloc(4*(MonitorWidth+32)*MonitorHeight);
  if (HD.ImageData==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't allocate image memory\n");
    exit(1);
  };

  HD.DisplayImage=XCreateImage(HD.disp,DefaultVisual(HD.disp,HD.ScreenNum),
                               HD.visInfo.depth,ZPixmap,0,HD.ImageData,
                               MonitorWidth,MonitorHeight,32,
                               0);
  if (HD.DisplayImage==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't create image\n");
    exit(1);
  };

  /* Now the same for the cursor image */
  HD.CursorImageData=malloc(4*64*MonitorHeight);
  if (HD.CursorImageData==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't allocate cursor image memory\n");
    exit(1);
  };

  HD.CursorImage=XCreateImage(HD.disp,DefaultVisual(HD.disp,HD.ScreenNum),
                               HD.visInfo.depth,ZPixmap,0,HD.CursorImageData,
                               32,MonitorHeight,32,
                               0);
  if (HD.CursorImage==NULL) {
    fprintf(stderr,"DisplayKbd_Init: Couldn't create cursor image\n");
    exit(1);
  };


  XSelectInput(HD.disp,HD.BackingWindow,ExposureMask |
                                   VisibilityChangeMask |
                                   KeyPressMask | KeyReleaseMask);
  XSelectInput(HD.disp,HD.MainPane,ExposureMask |
                                   VisibilityChangeMask |
                                   FocusChangeMask | PointerMotionMask |
                                   EnterWindowMask | LeaveWindowMask | /* For changing colour maps */
                                   KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
  XSelectInput(HD.disp,HD.CursorPane,ExposureMask |
                                   VisibilityChangeMask |
                                   FocusChangeMask |
                                   KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
  HD.DefaultColormap=DefaultColormapOfScreen(HD.xScreen);

  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"white",&(HD.White),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'white'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"black",&(HD.Black),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'black'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"red",&(HD.Red),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'red'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"green",&(HD.Green),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'green'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"gray10",&(HD.GreyDark),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'gray10'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"gray90",&(HD.GreyLight),&tmpcol))
    fprintf(stderr,"Failed to allocate colour 'gray90'\n");
  if (!XAllocNamedColor(HD.disp,HD.DefaultColormap,"PapayaWhip",&(HD.OffWhite),&tmpcol)) {
    /* A great shame - a rather nice colour.... */
    HD.OffWhite=HD.White;
    fprintf(stderr,"Failed to allocate colour 'PapayaWhip'\n");
  };

  /* I think the main monitor window will need its own colourmap
     since we need at least 256 colours for 256 colour mode */
  if (HD.visInfo.class==PseudoColor) {
    HD.ArcsColormap=XCreateColormap(HD.disp,HD.MainPane,DefaultVisual(HD.disp,HD.ScreenNum),
                                    AllocAll);

    for(prescol=0;prescol<256;prescol++) {
      tmpcol.flags=DoRed|DoGreen|DoBlue;
      tmpcol.pixel=prescol;
      tmpcol.red=(prescol &1)?65535:0;
      tmpcol.green=(prescol &2)?65535:0;
      tmpcol.blue=(prescol &2)?65535:0;
      XStoreColor(HD.disp,HD.ArcsColormap,&tmpcol); /* Should use XStoreColors */
    };

    XSetWindowColormap(HD.disp,HD.MainPane,HD.ArcsColormap);
    /* The following seems to be necessary to get the colourmap to switch -
    but I don't understand why! */
    XSetWindowColormap(HD.disp,HD.BackingWindow,HD.ArcsColormap);

    /* I think is to get the window manager to automatically set the colourmap ... */
    XSetWMColormapWindows(HD.disp,HD.BackingWindow,&(HD.MainPane),1);

    /*HD.ArcsColormap=DefaultColormapOfScreen(HD.xScreen); */
  } else {
    /* TrueColor - the colourmap is actually a fixed thing */
    HD.ArcsColormap=XCreateColormap(HD.disp,HD.MainPane,HD.visInfo.visual,
                                    AllocNone);
    for(prescol=0;prescol<256;prescol++) {
      tmpcol.flags=DoRed|DoGreen|DoBlue;
      tmpcol.red=(prescol &1)?65535:0;
      tmpcol.green=(prescol &2)?65535:0;
      tmpcol.blue=(prescol &2)?65535:0;
      tmpcol.pixel=prescol;
      /*XQueryColor(HD.disp,HD.ArcsColormap,&tmpcol);*/
      HD.pixelMap[prescol]=0;
    };
  };

  gctmpval.function=GXcopy;
  HD.MainPaneGC=XCreateGC(HD.disp,HD.MainPane,GCFunction,&gctmpval);
  XCopyGC(HD.disp,DefaultGC(HD.disp,HD.ScreenNum),GCPlaneMask|GCSubwindowMode,
          HD.MainPaneGC);

  /* Calloc to clear it as well */
  HD.ShapePixmapData=(char *)calloc(32*MonitorHeight,1);
  if (HD.ShapePixmapData==NULL) {
    fprintf(stderr,"Couldn't allocate memory for pixmap data\n");
    exit(0);
  };

  /* Shape stuff for the mouse cursor window */
  if (!XShapeQueryExtension(HD.disp,&shape_event_base,&shape_error_base)) {
    HD.ShapeEnabled=0;
    HD.shape_mask=0;
  } else {
    HD.ShapeEnabled=1;

    /*HD.shape_mask=XCreatePixmap(HD.disp,HD.CursorPane,32,MonitorHeight,1); */
    HD.shape_mask=XCreatePixmapFromBitmapData(HD.disp,HD.CursorPane,HD.ShapePixmapData,
                                             32,MonitorHeight,0,1,1);
    /* Eek - a lot of this is copied from XEyes - unfortunatly the manual
    page for this call is somewhat lacking.  To quote its bugs entry:
      'This manual page needs a lot more work' */
    XShapeCombineMask(HD.disp,HD.CursorPane,ShapeBounding,0,0,HD.shape_mask,
                      ShapeSet);
    XFreePixmap(HD.disp,HD.shape_mask);
  };
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

  XMapWindow(HD.disp,HD.BackingWindow);
  XMapSubwindows(HD.disp,HD.BackingWindow);
  XMapSubwindows(HD.disp,HD.MainPane);

  ControlPane_Init(state);

  ARMul_ScheduleEvent(&(enodes[xpollenode]),POLLGAP,DisplayKbd_XPoll);
  xpollenode^=1;
}; /* DisplayKbd_Init */

/*-----------------------------------------------------------------------------*/
static void BackingWindow_Event(ARMul_State *state,XEvent *e) {
  fprintf(stderr,"BackingWindow_Event type=%d\n",e->type);
}; /* BackingWindow_Event */

/*-----------------------------------------------------------------------------*/
static void ProcessKey(ARMul_State *state,XKeyEvent *key) {
  struct ArcKeyTrans *PresPtr;
  KeySym sym;

  /* Just take the unshifted version of the key */
  sym=XLookupKeysym(key,0);

  /* Trap the special key for mouse following */
  if (sym==MOUSEKEY) {
    /* And when it is pressed toggle the mouse follow mode */
    if (key->type==KeyPress) {
      fprintf(stderr,"Doing Mouse tracking\n");
      DC.DoingMouseFollow^=1;
      if (DC.DoingMouseFollow)
      {
	XColor black, dummy;
	Pixmap bm_no;
	static unsigned char bm_no_data[] = { 0,0,0,0, 0,0,0,0 };
       XAllocNamedColor(HD.disp,HD.ArcsColormap,"black",&black,&dummy);
	bm_no = XCreateBitmapFromData(HD.disp, HD.MainPane, bm_no_data, 8,8);
       XDefineCursor(HD.disp, HD.MainPane, XCreatePixmapCursor(HD.disp, bm_no, bm_no, &black, &black,0, 0));
      }
      else
	XDefineCursor(HD.disp, HD.MainPane, XCreateFontCursor(HD.disp, 132));
    };
    return;
  };

  if (KBD.BuffOcc>=KBDBUFFLEN) {
    fprintf(stderr,"KBD: Missed key - still busy sending another one\n");
    return;
  };


  /* Should set up KeyColToSend and KeyRowToSend and KeyUpNDown */
  for(PresPtr=transTable;PresPtr->row!=-1;PresPtr++) {
    if (PresPtr->sym==sym) {
      /* Found the key */
      /* Now add it to the buffer */
      KBD.Buffer[KBD.BuffOcc].KeyColToSend=PresPtr->col;
      KBD.Buffer[KBD.BuffOcc].KeyRowToSend=PresPtr->row;
      KBD.Buffer[KBD.BuffOcc].KeyUpNDown=(key->type==KeyPress)?0:1;
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

  fprintf(stderr,"ProcessKey: Unknown key sym=%ld!\n",sym);
}; /* ProcessKey */

/*-----------------------------------------------------------------------------*/
static void ProcessButton(ARMul_State *state,XButtonEvent *button) {
  int UpNDown=(button->type!=ButtonPress);
  int ButtonNum=-1;

  if (button->button==Button1) ButtonNum=0;
  if (button->button==Button2) ButtonNum=1;
  if (button->button==Button3) ButtonNum=2;

  /* Hey if you've got a 4 or more buttoned mouse hard luck! */
  if (ButtonNum<0) return;

  if (KBD.BuffOcc>=KBDBUFFLEN) {
    fprintf(stderr,"KBD: Missed mouse event - buffer full\n");
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
  int HorizPos=(int)VIDC.Horiz_CursorStart-(int)VIDC.Horiz_DisplayStart*2;
  int VertPos,tmp;
  int Height=(int)VIDC.Vert_CursorEnd-(int)VIDC.Vert_CursorStart+1;
#ifdef DEBUG_CURSOR
  fprintf(stderr,"UpdateCursorPos: Horiz_CursorStart=0x%x\n",VIDC.Horiz_CursorStart);
  fprintf(stderr,"UpdateCursorPos: Vert_CursorStart=0x%x\n",VIDC.Vert_CursorStart);
  fprintf(stderr,"UpdateCursorPos: Vert_CursorEnd=0x%x\n",VIDC.Vert_CursorEnd);
  fprintf(stderr,"UpdateCursorPos: Horiz_DisplayStart=0x%x\n",VIDC.Horiz_DisplayStart);
  fprintf(stderr,"UpdateCursorPos: Vert_DisplayStart=0x%x\n",VIDC.Vert_DisplayStart);
#endif
  VertPos=(int)VIDC.Vert_CursorStart;
  tmp=(signed int)VIDC.Vert_DisplayStart;
  VertPos-=tmp;
  if (Height<1) Height=1;
  
  if (VertPos<0) VertPos=0;
  if (HorizPos<0) HorizPos=0;

#ifdef DEBUG_CURSOR
  fprintf(stderr,"UpdateCursorPos: Height=%d VertPos=%d HorizPos=%d\n",Height,VertPos,HorizPos);
#endif
  XMoveResizeWindow(HD.disp,HD.CursorPane,HorizPos,VertPos,32,Height);
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
            ControlPane_RedrawLeds(state);
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
static void MouseMoved(ARMul_State *state,XMotionEvent *xmotion) {
  int xdiff,ydiff;
  /* Well the coordinates of the mouse cursor are now in xmotion->x and
    xmotion->y, I'm going to compare those against the cursor position
    and transmit the difference.  This can't possibly take care of the OS's
    hotspot offsets */

  /* We are now only using differences from the reference position */
  if ((xmotion->x==MonitorWidth/2) && (xmotion->y==MonitorHeight/2)) return;

  XWarpPointer(HD.disp,None,HD.MainPane,0,0,9999,9999,MonitorWidth/2,MonitorHeight/2);

#ifdef DEBUG_MOUSEMOVEMENT
  fprintf(stderr,"MouseMoved: CursorStart=%d xmotion->x=%d\n",
          VIDC.Horiz_CursorStart,xmotion->x);
#endif
  xdiff=xmotion->x-MonitorWidth/2;
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

  ydiff=MonitorHeight/2-xmotion->y;
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
/*-----------------------------------------------------------------------------*/
static void MainPane_Event(ARMul_State *state,XEvent *e) {
  switch (e->type) {
    case EnterNotify:
      /*fprintf(stderr,"MainPane: Enter notify!\n"); */
      if (HD.visInfo.class==PseudoColor) XInstallColormap(HD.disp,HD.ArcsColormap);
      DC.PollCount=0;
      XAutoRepeatOff(HD.disp);
      break;

    case LeaveNotify:
      /*fprintf(stderr,"MainPane: Leave notify!\n"); */
      if (HD.visInfo.class==PseudoColor) XUninstallColormap(HD.disp,HD.ArcsColormap);
      XAutoRepeatOn(HD.disp);
      DC.PollCount=0;
      break;

    case Expose:
      XPutImage(HD.disp,HD.MainPane,HD.MainPaneGC,HD.DisplayImage,
                e->xexpose.x,e->xexpose.y, /* source pos. in image */
                e->xexpose.x,e->xexpose.y, /* Position on window */
                e->xexpose.width,e->xexpose.height);
      break;

    case ButtonPress:
    case ButtonRelease:
      ProcessButton(state,&(e->xbutton));
      break;

    case KeyPress:
    case KeyRelease:
      ProcessKey(state,&(e->xkey));
      break;

    case MotionNotify:
#ifdef DEBUG_MOUSEMOVEMENT
      fprintf(stderr,"Motion Notify in mainpane\n");
#endif
      if (DC.DoingMouseFollow) {
        MouseMoved(state,&(e->xmotion));
      };
      break;

    default:
      /*fprintf(stderr,"MainPane_Event type=%d\n",e->type); */
      break;
  };
}; /* MainPane_Event */

/*-----------------------------------------------------------------------------*/
static void CursorPane_Event(ARMul_State *state,XEvent *e) {
  /*fprintf(stderr,"CursorPane_Event type=%d\n",e->type); */
  switch (e->type) {
    case EnterNotify:
      fprintf(stderr,"CursorPane: Enter notify!\n");
      if (HD.visInfo.class==PseudoColor) XInstallColormap(HD.disp,HD.ArcsColormap);
      XAutoRepeatOff(HD.disp);
      break;

    case LeaveNotify:
      fprintf(stderr,"CursorPane: Leave notify!\n");
      if (HD.visInfo.class==PseudoColor) XUninstallColormap(HD.disp,HD.ArcsColormap);
      XAutoRepeatOn(HD.disp);
      DC.PollCount=0;
      break;

    case Expose:
      XPutImage(HD.disp,HD.CursorPane,HD.MainPaneGC,HD.CursorImage,
                e->xexpose.x,e->xexpose.y, /* source pos. in image */
                e->xexpose.x,e->xexpose.y, /* Position on window */
                e->xexpose.width,e->xexpose.height);
      break;

    case ButtonPress:
    case ButtonRelease:
      ProcessButton(state,&(e->xbutton));
      break;

    case KeyPress:
    case KeyRelease:
      ProcessKey(state,&(e->xkey));
      break;

    default:
      fprintf(stderr,"MainPane_Event type=%d\n",e->type);
      break;
  };
}; /* CursorPane_Event */

/*-----------------------------------------------------------------------------*/
/* Called using an ARM_ScheduleEvent - it also sets itself up to be called
   again.                                                                      */
unsigned DisplayKbd_XPoll(ARMul_State *state) {
  XEvent e;
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


  /* See if there is an event - if there is deal with it */
  if (XCheckMaskEvent(HD.disp,ULONG_MAX,&e)) {
    /* Handle event */
    if (e.xany.window==HD.BackingWindow) BackingWindow_Event(state,&e);
    if (e.xany.window==HD.MainPane) MainPane_Event(state,&e);
    if (e.xany.window==HD.ControlPane) ControlPane_Event(state,&e);
    if (e.xany.window==HD.CursorPane) CursorPane_Event(state,&e);

  }; /* Pending event ? */

  if (--(DC.AutoRefresh)<0) RefreshDisplay(state);

  ARMul_ScheduleEvent(&(enodes[xpollenode]),POLLGAP,DisplayKbd_XPoll);
  xpollenode^=1;

  return 0;
}; /* DisplayKbd_XPoll */
/*-----------------------------------------------------------------------------*/
void VIDC_PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw) {
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
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayStart,((val>>14) & 0x3ff));
      break;

    case 0x90:
#ifdef DEBUG_VIDCREGS   
      fprintf(stderr,"VIDC Horiz display end register val=%d\n",val>>14);
#endif   
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayEnd,(val>>14) & 0x3ff);
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
      break;

    case 0xb0:
#ifdef DEBUG_VIDCREGS   
      fprintf(stderr,"VIDC Vert disp end register val=%d\n",val>>14);
#endif   
      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_DisplayEnd,(val>>14) & 0x3ff);
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
      break;

    default:
      fprintf(stderr,"Write to unknown VIDC register reg=0x%x val=0x%x\n",addr,val);
      break;

  }; /* Register switch */
}; /* PutValVIDC */
