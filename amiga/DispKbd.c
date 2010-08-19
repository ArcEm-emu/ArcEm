/* Amiga DispKbd.c for ArcEm by Chris Young <chris@unsatisfactorysoftware.co.uk> */
/* Some code based on DispKbd.c for other platforms                              */

#include <string.h>

#include "../armdefs.h"
#include "armarc.h"
#include "../arch/keyboard.h"
#include "DispKbd.h"
#include "KeyTable.h"
#include "platform.h"
#include "../arch/Version.h"
#include "arexx.h"

#include <intuition/pointerclass.h>
#include <dos/dostags.h>
#include <libraries/keymap.h>
#include <devices/input.h>
#include <devices/inputevent.h>

struct Window *window = NULL;
struct Screen *screen = NULL;
struct MsgPort *port = NULL;
static struct RastPort mouseptr;
APTR mouseobj = NULL;
struct FileRequester *filereq;
static struct RastPort friend;
struct RastPort tmprp;

static struct BitMap *mouse_bm;
static struct RastPort mouse_rp;
PLANEPTR mask;

/*
static struct SimpleSprite ptr_sprite;
static int ptr_sprite_num;
static UWORD *ptr_sprite_img;
*/

struct IOStdReq *ir;
struct MsgPort *mport;

ULONG oldid=INVALID_ID;
ULONG oldwidth = 0;
ULONG oldheight = 0;
ULONG olddepth = 0;

static ULONG OldMouseX = 0;
static ULONG OldMouseY = 0;

//#define HD HOSTDISPLAY
#define HD HostDisplay
#define DC DISPLAYCONTROL

#define KEYREENABLEDELAY 1000
#define AUTOREFRESHPOLL 2500

static void refreshmouse(ARMul_State *state);
void writepixel(struct RastPort *,ULONG,ULONG,ULONG);
void ChangeDisplayMode(ARMul_State *,long,long,int);
void CloseDisplay(void);

void CloseDisplay(void)
{
	if(window)
		IIntuition->CloseWindow(window);

	IIntuition->IDoMethod(arexx_obj, AM_EXECUTE, "QUIT", "ARCEMPANEL", NULL, NULL, NULL, NULL);

	if(screen)
	{
		while(!IIntuition->CloseScreen(screen));
	}

	window = NULL;
	screen = NULL;
	port = NULL;
}

void ChangeDisplayMode(ARMul_State *state,long width,long height,int vidcdepth)
{
	int depth = 0;
	ULONG id = INVALID_ID;

	switch(vidcdepth)
	{
		case 0:
			depth=1;
		break;
		case 1:
			depth=2;
		break;
		case 2:
			depth=4;
		break;
		case 3:
			depth=8;
		break;
	}

	printf("New display mode: %ld x %ld x %d ",width,height,depth);

	if(width == oldwidth && height == oldheight && depth == olddepth)
	{
		printf("-> Existing mode\n");
		return;
	}

	if((width<=0) || (height <= 0) || (depth <= 0))
	{
		printf("-> Invalid mode\n");
		return;
	}

	/* This forces ArcEm to use an 8-bit Intuition screen, which avoids unnecessary
       screen opening/closing.  However, it is likely to be slower or less memory
       efficient, especially on planar display hardware. */
	if(force8bit) depth = 8;

	/* Call BestModeID() to hopefully stop crazy screenmodes */
	id = IGraphics->BestModeID(BIDTAG_NominalWidth,width,
								BIDTAG_NominalHeight,height,
								BIDTAG_DesiredWidth,width,
								BIDTAG_DesiredHeight,height,
								BIDTAG_Depth,depth,
								TAG_DONE);

	if(id == INVALID_ID)
	{
		printf("-> No suitable mode found\n");
		return;
	}

	printf("-> ModeID: %lx\n",id);

	IGraphics->WaitBlit();

	if(friend.BitMap)
		IGraphics->FreeBitMap(friend.BitMap);

	if(tmprp.BitMap)
  		IGraphics->FreeBitMap(tmprp.BitMap);

	CloseDisplay();

	screen = IIntuition->OpenScreenTags(NULL,SA_Width,width,
											SA_Height,height,
											SA_Depth,depth,
											SA_Quiet,TRUE,
											SA_ShowTitle,FALSE,
//											SA_Type,CUSTOMSCREEN,
											SA_PubName,"ArcEm",
											SA_DisplayID,id,
											TAG_DONE);

	if(screen)
	{
//		screen->ViewPort.Modes |= SPRITES;

		window = IIntuition->OpenWindowTags(NULL,WA_Width,width,
					WA_Height,height,
					WA_RMBTrap,TRUE,
					WA_NoCareRefresh,TRUE,
					WA_CustomScreen,screen,
					WA_Borderless,TRUE,
					WA_Activate,TRUE,
					WA_Backdrop,TRUE,
					WA_WindowName,"ArcEm",
					WA_ReportMouse,TRUE,
					WA_MouseQueue,500,
					WA_IDCMP,IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_DELTAMOVE | IDCMP_EXTENDEDMOUSE,
					TAG_DONE);

		if(mouse_bm) IGraphics->FreeBitMap(mouse_bm);

		mouse_bm = IGraphics->AllocBitMap(32, 32, depth, BMF_CLEAR, window->RPort->BitMap);
		mouse_rp.BitMap = mouse_bm;
	}
	else
	{
		return;
	}

	if(!window)
		return;

	IIntuition->PubScreenStatus(screen,0);

	port = window->UserPort;

	friend.BitMap = IGraphics->AllocBitMap(width,height,depth,BMF_CLEAR | BMF_DISPLAYABLE | BMF_INTERLEAVED,window->RPort->BitMap);

    IExec->CopyMem(&friend,&tmprp,sizeof(struct RastPort));
    tmprp.Layer = NULL;
    tmprp.BitMap = IGraphics->AllocBitMap(width,1,depth,0,NULL);

	/* Completely new screen and window, so we have to set the palette and redraw the display */

	DC.MustRedraw = 1;
	DC.MustResetPalette = 1;

	oldheight = height;
	oldwidth = width;
	olddepth = depth;
	oldid=id;
}

void cleanup(void)
{
#ifdef SOUND_SUPPORT
	sound_exit();
#endif

	IGraphics->WaitBlit();

//	IGraphics->FreeSprite(ptr_sprite_num);
//	IExec->FreeVec(ptr_sprite_img);

	if(mouseobj)
	{
		IIntuition->SetWindowPointer(window,TAG_DONE);
		IIntuition->DisposeObject(mouseobj);
	}

	if(mouseptr.BitMap)
	{
		IGraphics->FreeRaster(mouseptr.BitMap->Planes[0],32,32);
		IGraphics->FreeRaster(mouseptr.BitMap->Planes[1],32,32);
		IExec->FreeVec(mouseptr.BitMap);
//		IGraphics->FreeBitMap(mouseptr.BitMap);
		mouseptr.BitMap=NULL;
	}

	if(friend.BitMap)
		IGraphics->FreeBitMap(friend.BitMap);

	if(tmprp.BitMap)
  		IGraphics->FreeBitMap(tmprp.BitMap);

	if(mouse_bm) IGraphics->FreeBitMap(mouse_bm);

	if(mask) IGraphics->FreeRaster(mask,32,32);

	CloseDisplay();

	ARexx_Cleanup();

/* input.device cleanup
	if(ir)
		IExec->FreeSysObject(ASOT_IOREQUEST,ir);

	if(mport)
		IExec->FreeSysObject(ASOT_PORT,mport);
*/

	if(IGraphics)
	{
		IExec->DropInterface((struct Interface *)IGraphics);
		IExec->CloseLibrary(GfxBase);
	}

	if(IDOS)
	{
		IExec->DropInterface((struct Interface *)IDOS);
		IExec->CloseLibrary(DOSBase);
	}

	if(IAsl)
	{
		IExec->DropInterface((struct Interface *)IAsl);
		IExec->CloseLibrary(AslBase);
	}

	if(IIntuition)
	{
		IExec->DropInterface((struct Interface *)IIntuition);
		IExec->CloseLibrary(IntuitionBase);
	}

	exit(0);
}

void MouseMoved(ARMul_State *state,int xdiff,int ydiff)
{
				if (xdiff > 63)
    				xdiff=63;
  				if (xdiff < -63)
    				xdiff=-63;
	  			if (ydiff>63)
    				ydiff=63;
  				if (ydiff<-63)
    				ydiff=-63;

	  			KBD.MouseXCount = xdiff & 127;
  				KBD.MouseYCount = -ydiff & 127;

//refreshmouse(state);
}

int
DisplayKbd_PollHost(ARMul_State *state)
{
	struct IntuiMessage *msg;
	int keyup,key;
	struct IntuiWheelData *wheel = NULL;
	if(!port) return(0);

	while((msg=(struct IntuiMessage *)IExec->GetMsg(port)))
	{
		switch(msg->Class)
		{
			case IDCMP_MOUSEBUTTONS:

				switch(msg->Code)
				{
					case SELECTDOWN:
						keyboard_key_changed(&KBD, ARCH_KEY_button_1,FALSE);
					break;

					case SELECTUP:
						keyboard_key_changed(&KBD, ARCH_KEY_button_1,TRUE);
					break;

					case MENUDOWN:
						keyboard_key_changed(&KBD, ARCH_KEY_button_3,FALSE);
					break;

					case MENUUP:
						keyboard_key_changed(&KBD, ARCH_KEY_button_3,TRUE);
					break;

					case MIDDLEDOWN:
						keyboard_key_changed(&KBD, ARCH_KEY_button_2,FALSE);
					break;

					case MIDDLEUP:
						keyboard_key_changed(&KBD, ARCH_KEY_button_2,TRUE);
					break;
				}
			break;

			case IDCMP_EXTENDEDMOUSE:

				if(msg->Code == IMSGCODE_INTUIWHEELDATA)
				{
					wheel = (struct IntuiWheelData *)msg->IAddress;

					if(wheel->WheelY < 0)
						keyboard_key_changed(&KBD, ARCH_KEY_button_4,TRUE);

					if(wheel->WheelY > 0)
						keyboard_key_changed(&KBD, ARCH_KEY_button_5,TRUE);
				}
			break;

			case IDCMP_RAWKEY:

				switch(msg->Code)
				{

					case 0x67:
					{
						BPTR *in,*out;

						in = IDOS->Open("NIL:",MODE_OLDFILE);
						out = IDOS->Open("NIL:",MODE_NEWFILE);

						IDOS->SystemTags("arcempanel",SYS_Asynch,TRUE,SYS_Input,in,SYS_Output,out,TAG_DONE);
					}
					break;

					default:
						if(msg->Code >= 0x80)
						{
							key = msg->Code - 0x80;
							keyup = TRUE;
						}
						else
						{
							key = msg->Code;
							keyup = FALSE;
						}

						if(key != -1)
							keyboard_key_changed(&KBD,amirawkey[key],keyup);
					break;
				}
    		break;

			case IDCMP_MOUSEMOVE:
				MouseMoved(state, msg->MouseX,msg->MouseY);
			break;
		}

		if(msg) IExec->ReplyMsg((struct Message *)msg);
	}

	ARexx_Handle();

	if(arexx_quit)
		cleanup();

  return 0;
}

/*-----------------------------------------------------------------------------*/
/* Configure the TrueColor pixelmap for the standard 1,2,4 bpp modes           */
static void DoPixelMap_Standard(ARMul_State *state,int colours) {
  struct XColor tmp;
  int c;

/* We only need to reset the palette if this is set, and we don't need to redraw
   to reset it as we are using a palette-mapped screen */
  if (!DC.MustResetPalette  || !screen) return;

//  DC.MustRedraw = 1;

  for(c=0;c<colours;c++) {
    tmp.red=(VIDC.Palette[c] & 15)<<28; //12
    tmp.green=((VIDC.Palette[c]>>4) & 15)<<28;
    tmp.blue=((VIDC.Palette[c]>>8) & 15)<<28;

	IGraphics->SetRGB32(&screen->ViewPort,c,tmp.red,tmp.green,tmp.blue);
  };

  /* Now do the ones for the cursor */

	ULONG 	col_reg = 16+((0 & 0x06) << 1);

  for(c=0;c<3;c++) {
    tmp.red=(VIDC.CursorPalette[c] &15)<<28;
    tmp.green=((VIDC.CursorPalette[c]>>4) &15)<<28;
    tmp.blue=((VIDC.CursorPalette[c]>>8) &15)<<28;

	IGraphics->SetRGB32(&screen->ViewPort,col_reg + 1 + c,tmp.red,tmp.green,tmp.blue);
	
    /* Entry 0 is transparent */
  };

  DC.MustResetPalette=0;
}; /* DoPixelMap_Standard */


/*-----------------------------------------------------------------------------*/
/* Configure the true colour pixel map for the 8bpp modes                      */
static void DoPixelMap_256(ARMul_State *state) {
  struct XColor tmp;
  ULONG c;

  if (!DC.MustResetPalette || !screen) return;

// This mostly fixes the Amiga screen depth < 8 crashing bug, but some problems remain.
	if(olddepth<8) return;

  // Don't need to redraw to change the palette on a palette-mapped screen
 // DC.MustRedraw = 1;

  for(c=0;c<256;c++) {
    int palentry=c &15;
    int L4=(c>>4) &1;
    int L65=(c>>5) &3;
    int L7=(c>>7) &1;

    tmp.red=((VIDC.Palette[palentry] & 7) | (L4<<3))<<28;
    tmp.green=(((VIDC.Palette[palentry] >> 4) & 3) | (L65<<2))<<28;
    tmp.blue=(((VIDC.Palette[palentry] >> 8) & 7) | (L7<<3))<<28;
    /* I suppose I should do something with the supremacy bit....   12*/

	IGraphics->SetRGB32(&screen->ViewPort,c,tmp.red,tmp.green,tmp.blue);
  };

  /* Now do the ones for the cursor */

	ULONG 	col_reg = 16+((0 & 0x06) << 1);

  for(c=0;c<3;c++) {
    tmp.red=(VIDC.CursorPalette[c] &15)<<28;
    tmp.green=((VIDC.CursorPalette[c]>>4) &15)<<28;
    tmp.blue=((VIDC.CursorPalette[c]>>8) &15)<<28;

	IGraphics->SetRGB32(&screen->ViewPort,col_reg + 1 + c,tmp.red,tmp.green,tmp.blue);
    /* Entry 0 is transparent */

  };

  DC.MustResetPalette=0;
}; /* DoPixelMap_256 */

/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_1bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight)
{
  int x,y,memoffset;
  char Buffer[DisplayWidth / 8];
  char *ImgPtr = HD.ImageData;
	UBYTE line[DisplayWidth];

  /* First configure the colourmap */
//	if(DC.MustResetPalette)
  		DoPixelMap_Standard(state,2);

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/8,ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,DisplayWidth/8))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,DisplayWidth/8, Buffer);


      for(x=0;x<DisplayWidth;x+=8) {
        int pixel = Buffer[x>>3];

		line[x] = (pixel) &1;
		line[x+1] = (pixel>>1) &1;
		line[x+2] = (pixel>>2) &1;
		line[x+3] = (pixel>>3) &1;
		line[x+4] = (pixel>>4) &1;
		line[x+5] = (pixel>>5) &1;
		line[x+6] = (pixel>>6) &1;
		line[x+7] = (pixel>>7) &1;
      }; /* x */
	IGraphics->WritePixelLine8(&friend,0,y,DisplayWidth,line,&tmprp);
    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;

  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_TrueColor_1bpp */


/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_2bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight) {
  int x,y,memoffset;
  char Buffer[DisplayWidth/4];
  char *ImgPtr=HD.ImageData;
	UBYTE line[DisplayWidth];

  /* First configure the colourmap */
//	if(DC.MustResetPalette)
  		DoPixelMap_Standard(state,4);

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth/4,ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,DisplayWidth/4))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,DisplayWidth/4, Buffer);


      for(x=0;x<DisplayWidth;x+=4) {
        int pixel = Buffer[x>>2];

		line[x] = (pixel) &3;
		line[x+1] = (pixel>>2) &3;
		line[x+2] = (pixel>>4) &3;
		line[x+3] = (pixel>>6) &3;

      }; /* x */
	IGraphics->WritePixelLine8(&friend,0,y,DisplayWidth,line,&tmprp);
    }; /* Update test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);

}; /* RefreshDisplay_TrueColor_2bpp */

static void RefreshDisplay_TrueColor_4bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight) {
  int x,y,memoffset;
  char Buffer[DisplayWidth/2];
  char *ImgPtr=HD.ImageData;
	UBYTE line[DisplayWidth];

  /* First configure the colourmap */
//	if(DC.MustResetPalette)
  		DoPixelMap_Standard(state,16);

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=(DisplayWidth/2),ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,DisplayWidth/2))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,DisplayWidth/2, Buffer);

      for(x=0;x<DisplayWidth;x+=2) {

    int pixel = Buffer[x>>1];

	line[x] = pixel &15;
	line[x+1] = (pixel>>4) &15;

      }; /* x */
	IGraphics->WritePixelLine8(&friend,0,y,DisplayWidth,line,&tmprp);

    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);
}; /* RefreshDisplay_TrueColor_4bpp */


/*-----------------------------------------------------------------------------*/
static void RefreshDisplay_TrueColor_8bpp(ARMul_State *state, int DisplayWidth, int DisplayHeight) {
  int y,memoffset;
  unsigned char Buffer[DisplayWidth];
  char *ImgPtr=HD.ImageData;


  /* First configure the colourmap */
  DoPixelMap_256(state);

  for(y=0,memoffset=0;y<DisplayHeight;
                      y++,memoffset+=DisplayWidth,ImgPtr+=DisplayWidth) {
    if ((DC.MustRedraw) || (QueryRamChange(state,memoffset,DisplayWidth))) {
      if (y<DC.miny) DC.miny=y;
      if (y>DC.maxy) DC.maxy=y;
      CopyScreenRAM(state,memoffset,DisplayWidth, Buffer);

		IGraphics->WritePixelLine8(&friend,0,y,DisplayWidth,Buffer,&tmprp);

    }; /* Refresh test */
  }; /* y */
  DC.MustRedraw=0;
  MarkAsUpdated(state,memoffset);

} /* RefreshDisplay_TrueColor_8bpp */

void
RefreshDisplay(ARMul_State *state)
{
	if(!window) return;
	if(!friend.BitMap) return;

  int DisplayHeight = VIDC.Vert_DisplayEnd - VIDC.Vert_DisplayStart;
  int DisplayWidth  = (VIDC.Horiz_DisplayEnd - VIDC.Horiz_DisplayStart) * 2;

  DC.AutoRefresh=AUTOREFRESHPOLL;
  ioc.IRQStatus|=8; /* VSync */
  ioc.IRQStatus |= 0x20; /* Sound - just an experiment */
  IO_UpdateNirq(state);

  DC.miny=DisplayHeight-1;
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
	IGraphics->BltBitMap(friend.BitMap,0,DC.miny,
			window->RPort->BitMap,0,DC.miny,
			DisplayWidth,(DC.maxy-DC.miny)+1,0x0C0,0xff,NULL);

  }

} /* RefreshDisplay */
/*-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------*/
/* Refresh the mouse pointer image                                                */
static void refreshmouse(ARMul_State *state) {
  int x,y,height,offset;
  int memptr;
  unsigned short *ImgPtr;
  int HorizPos = (int)VIDC.Horiz_CursorStart - (int)VIDC.Horiz_DisplayStart*2;
  int Height = (int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart +1;
  int VertPos;
	UBYTE line[32];
	ULONG ptr_cols[4];
	ULONG 	col_reg = 16+((0 & 0x06) << 1);
	UBYTE maskbit;
	int maskcount = 0;

	if(!window) return;
	if(!screen) return;

#if 0
	if(ptr_sprite_img)
	{
		offset=0;
		memptr=MEMC.Cinit*16;
		height=(VIDC.Vert_CursorEnd-VIDC.Vert_CursorStart)+1;
		ImgPtr=(unsigned short *) HD.CursorImageData;

		ptr_sprite_img[0] = 0;
		ptr_sprite_img[1] = 0;

		for(y=0;y<32;y++,memptr+=8,offset+=8) {
		// fixed height to 32

    	if (offset<512*1024) {
			ARMword *pointer_data = MEMC.PhysRam + ((MEMC.Cinit * 16)/4);
			ARMword tmp[2];

			tmp[0]=MEMC.PhysRam[memptr/4];
			tmp[1]=MEMC.PhysRam[memptr/4+1];

			for(x=0;x<32;x+=2,ImgPtr+=2) {

			if(y<height)
			{
				ptr_sprite_img[2 + (y * 2) + x] = tmp[0];
				ptr_sprite_img[2 + (y * 2) + x + 1] = tmp[1];
 //((tmp[x/16]>>((x & 15)*2)) & 3);
				//printf("%ld ",line[x]);
			}
      }; /* x */
    } else return;
  }; /* y */
	
		IGraphics->ChangeSprite(&screen->ViewPort, &ptr_sprite, ptr_sprite_img);

	}

  VertPos = (int)VIDC.Vert_CursorStart;
  VertPos -= (signed int)VIDC.Vert_DisplayStart;

	IGraphics->MoveSprite(&screen->ViewPort, &ptr_sprite, HorizPos, VertPos);
#endif

	if(!mouse_bm) return;

/*
	if(!mouseptr.BitMap)
	{
		mouseptr.BitMap=IExec->AllocVec(sizeof(struct BitMap),MEMF_CLEAR);
		IGraphics->InitBitMap(mouseptr.BitMap,2,32,32);
		mouseptr.BitMap->Planes[0] = IGraphics->AllocRaster(32,32);
		mouseptr.BitMap->Planes[1] = IGraphics->AllocRaster(32,32);

		ptr_cols[1] = IGraphics->FindColor(screen->ViewPort.ColorMap,
			VIDC.CursorPalette[0]
);


//		mouseptr.BitMap = IGraphics->AllocBitMap(32,32,2,BMF_DISPLAYABLE | BMF_CLEAR | BMF_INTERLEAVED,NULL);
	}
*/

  VertPos = (int)VIDC.Vert_CursorStart;
  VertPos -= (signed int)VIDC.Vert_DisplayStart;

 offset=0;
  memptr=MEMC.Cinit*16;
  height=(VIDC.Vert_CursorEnd-VIDC.Vert_CursorStart)+1;
  ImgPtr=(unsigned short *) HD.CursorImageData;

	IGraphics->BltBitMap(friend.BitMap,OldMouseX,OldMouseY,
			window->RPort->BitMap,OldMouseX,OldMouseY,
			32,32,0x0C0,0xff,NULL);

/*
	IGraphics->BltBitMap(mouse_bm,0,0,
			window->RPort->BitMap,OldMouseX,OldMouseY,
			32,32,0x0C0,0xff,NULL);

	IGraphics->BltBitMap(window->RPort->BitMap,HorizPos,VertPos,
			mouse_bm,0,0,
			32,32,0x0C0,0xff,NULL);
*/

  for(y=0;y<32;y++,memptr+=8,offset+=8) {
// fixed height to 32

    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++,ImgPtr++) {

		if(y<height)
		{
        	line[x] = ((tmp[x/16]>>((x & 15)*2)) & 3) + col_reg;

			if(line[x] == 0) maskbit = 0;
				else maskbit = 1;

			mask[(y*4) + (x/4)] &= (maskbit << (8 - maskcount));

			maskcount++;
			if(maskcount > 8) maskcount = 0;

//printf("%ld ",line[x]);
		}
		else
		{
			line[x] = 0;
		}
      }; /* x */
//printf("\n");
		IGraphics->WritePixelLine8(&mouse_rp,0,y,32,line,&tmprp); // &mouseptr
    } else return;
  }; /* y */

// HorizPos,VertPos
	IGraphics->BltMaskBitMapRastPort(mouse_bm,0,0,
			window->RPort,HorizPos,VertPos,
			32,height,ABC,mask);

/*
	IGraphics->BltBitMap(mouseptr.BitMap,0,0,
			window->RPort->BitMap,HorizPos,VertPos,
			32,height,0x0C0,0xff,NULL);
*/
	OldMouseX = HorizPos;
	OldMouseY = VertPos;

return;
#if 0
	if(!mouseptr.BitMap)
	{
		mouseptr.BitMap=IExec->AllocVec(sizeof(struct BitMap),MEMF_CLEAR);
		IGraphics->InitBitMap(mouseptr.BitMap,2,32,32);
		mouseptr.BitMap->Planes[0] = IGraphics->AllocRaster(32,32);
		mouseptr.BitMap->Planes[1] = IGraphics->AllocRaster(32,32);

//		mouseptr.BitMap = IGraphics->AllocBitMap(32,32,2,BMF_DISPLAYABLE | BMF_CLEAR | BMF_INTERLEAVED,NULL);
		mouseobj = IIntuition->NewObject(NULL,"pointerclass",POINTERA_BitMap,mouseptr.BitMap,POINTERA_WordWidth,2,POINTERA_XOffset,-22,POINTERA_YOffset,0,POINTERA_XResolution,POINTERXRESN_SCREENRES,POINTERA_YResolution,POINTERYRESN_SCREENRESASPECT,TAG_DONE);
	}


  VertPos = (int)VIDC.Vert_CursorStart;
  VertPos -= (signed int)VIDC.Vert_DisplayStart;

  if (Height < 1) Height = 1;
  if (VertPos < 1) VertPos = 1;

struct IBox ibox;

/* My graphics card has problems getting into the negatively numbered X positions,
   workaround is to enable SOFTSPRITE in the Radeon monitor tooltypes. */

ibox.Left = HorizPos+22;
ibox.Top = VertPos;
ibox.Width = 1;
ibox.Height = 1;

IIntuition->SetWindowAttrs(window,WA_MouseLimits,&ibox,TAG_DONE);
									//WA_GrabFocus,1,TAG_DONE);

/* Snippet of mouse positioning code by Andy Broad
In our situation, it doesn't work very well, leaving code here just in case but
not using it at present.

	if(!IExec->OpenDevice(INPUTNAME,0,ir,0))
	{
		struct InputEvent ie;
		struct IEPointerPixel pp;

		ir->io_Command = IND_WRITEEVENT;
		ir->io_Length = sizeof(struct InputEvent);
		ir->io_Data = &ie;

		ie.ie_NextEvent = NULL;
		ie.ie_Class = IECLASS_NEWPOINTERPOS;
		ie.ie_SubClass = IESUBCLASS_PIXEL;
		ie.ie_Code = 0;
		ie.ie_Qualifier = 0; //IEQUALIFIER_RELATIVEMOUSE;;
		ie.ie_EventAddress = &pp;

		pp.iepp_Screen = screen;
		pp.iepp_Position.X = HorizPos+22;
		pp.iepp_Position.Y = VertPos;

//IExec->DebugPrintF("%ld,%ld,%ld\n",HorizPos+22,VertPos,VIDC.Horiz_CursorStart);

		IExec->DoIO(ir);
	
		IExec->CloseDevice(ir);
	}

*/
  offset=0;
  memptr=MEMC.Cinit*16;
  height=(VIDC.Vert_CursorEnd-VIDC.Vert_CursorStart)+1;
  ImgPtr=(unsigned short *) HD.CursorImageData;
  for(y=0;y<32;y++,memptr+=8,offset+=8) {
// fixed height to 32

    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++,ImgPtr++) {

		if(y<height)
		{
        	line[x] = ((tmp[x/16]>>((x & 15)*2)) & 3);
//printf("%ld ",line[x]);
		}
		else
		{
			line[x] = 0;
		}
      }; /* x */
//printf("\n");
		IGraphics->WritePixelLine8(&mouseptr,0,y,32,line,&tmprp);
    } else return;
  }; /* y */

	IIntuition->SetWindowPointer(window,WA_Pointer,mouseobj,TAG_DONE);

/*
	IGraphics->BltBitMap(mouseptr.BitMap,0,0,
			window->RPort->BitMap,HorizPos,VertPos,
			32,height,0x0C0,0xff,NULL);
*/
#endif
}; /* RefreshMouse */

void
DisplayKbd_InitHost(ARMul_State *state)
{
  /* Setup display and cursor bitmaps */

/* Need to add some error messages here, although if these aren't available you have bigger problems */

    IExec = (struct ExecIFace *)(*(struct ExecBase **)4)->MainInterface;

	if((IntuitionBase = IExec->OpenLibrary((char *)&"intuition.library",51)))
	{
		IIntuition = (struct IntuitionIFace *)IExec->GetInterface(IntuitionBase,(char *)&"main",1,NULL);
	}
	else
	{
		cleanup();
	}

	if((GfxBase = IExec->OpenLibrary((char *)&"graphics.library",51)))
	{
		IGraphics = (struct GraphicsIFace *)IExec->GetInterface(GfxBase,(char *)&"main",1,NULL);
	}
	else
	{
		cleanup();
	}

	if((AslBase = IExec->OpenLibrary((char *)&"asl.library",51)))
	{
		IAsl = (struct AslIFace *)IExec->GetInterface(AslBase,(char *)&"main",1,NULL);
	}
	else
	{
		cleanup();
	}

/* input.device related code
	if((mport = IExec->AllocSysObjectTags(ASOT_PORT,ASO_NoTrack,FALSE,TAG_DONE)))
	{
		if((ir = IExec->AllocSysObjectTags(ASOT_IOREQUEST,ASO_NoTrack,FALSE,ASOIOR_ReplyPort,mport,ASOIOR_Size,sizeof(struct IOStdReq))))
		{

		}
		else
		{
			cleanup();
		}
	}
	else
	{
		cleanup();
	}
*/

	ARexx_Init();

	IGraphics->InitRastPort(&mouseptr);
	IGraphics->InitRastPort(&mouse_rp);
	IGraphics->InitRastPort(&friend);
	mask = IGraphics->AllocRaster(32,32);
/*
	ptr_sprite_num = IGraphics->GetSprite(&ptr_sprite, -1);
	if(ptr_sprite_num == -1) cleanup();

	ptr_sprite_img = IExec->AllocVec((2 + 32) * 4, MEMF_CLEAR); // MEMF_CHIP | 
*/

	

	filereq = IAsl->AllocAslRequestTags(ASL_FileRequest,
									ASLFR_RejectIcons,TRUE,
	                             	ASLFR_InitialDrawer,"arexx",
									ASLFR_InitialPattern,"#?.arcem",
									ASLFR_DoPatterns,TRUE,
									TAG_DONE);

/*
	if(P96Base = IExec->OpenLibrary("Picasso96API.library",0))
	{
		IP96 = IExec->GetInterface(P96Base,"main",1,NULL);
	}
*/

  HD.red_prec    = 8;
  HD.green_prec  = 8;
  HD.blue_prec   = 8;
  HD.red_shift   = 8;
  HD.green_shift = 16;
  HD.blue_shift  = 24;

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

		ChangeDisplayMode(state,(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart,(VIDC.ControlReg & 0xc)>>2);


      break;

    case 0x90:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Horiz display end register val=%d\n",val>>14);
#endif

      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Horiz_DisplayEnd,(val>>14) & 0x3ff);

		ChangeDisplayMode(state,(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart,(VIDC.ControlReg & 0xc)>>2);

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
      refreshmouse(state);
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

		ChangeDisplayMode(state,(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart,(VIDC.ControlReg & 0xc)>>2);

      break;

    case 0xb0:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert disp end register val=%d\n",val>>14);
#endif

      VideoRelUpdateAndForce(DC.MustRedraw,VIDC.Vert_DisplayEnd,(val>>14) & 0x3ff);

	ChangeDisplayMode(state,(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart,(VIDC.ControlReg & 0xc)>>2);

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
      refreshmouse(state);
//sash
      break;

    case 0xbc:
#ifdef DEBUG_VIDCREGS
      fprintf(stderr,"VIDC Vert cursor end register val=%d\n",val>>14);
#endif
      VIDC.Vert_CursorEnd=(val>>14) & 0x3ff;
      refreshmouse(state);
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
	ChangeDisplayMode(state,(VIDC.Horiz_DisplayEnd-VIDC.Horiz_DisplayStart)*2,VIDC.Vert_DisplayEnd-VIDC.Vert_DisplayStart,(VIDC.ControlReg & 0xc)>>2);
      break;

    default:
      fprintf(stderr,"Write to unknown VIDC register reg=0x%x val=0x%x\n",addr,val);
      break;

  }; /* Register switch */
}

