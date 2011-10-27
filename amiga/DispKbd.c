/* Amiga DispKbd.c for ArcEm by Chris Young <chris@unsatisfactorysoftware.co.uk> */
/* Some code based on DispKbd.c for other platforms                              */

#include <string.h>
#include <limits.h>

#include "../armdefs.h"
#include "armarc.h"
#include "../arch/keyboard.h"
#include "displaydev.h"
#include "KeyTable.h"
#include "platform.h"
#include "../arch/Version.h"
#include "arexx.h"
#include "../armemu.h"
#include "arch/displaydev.h"

#include <intuition/pointerclass.h>
#include <dos/dostags.h>
#include <libraries/keymap.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <graphics/blitattr.h>

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

struct IOStdReq *ir;
struct MsgPort *mport;

ULONG oldid=INVALID_ID;
ULONG oldwidth = 0;
ULONG oldheight = 0;
ULONG oldlog2bpp = 0;

static ULONG OldMouseX = 0;
static ULONG OldMouseY = 0;

static int redraw_miny = INT_MAX;
static int redraw_maxy = 0; 


void writepixel(struct RastPort *,ULONG,ULONG,ULONG);
void ChangeDisplayMode(ARMul_State *,long,long,int);
void CloseDisplay(void);

int ChangeMode(int width,int height,int log2bpp,int *xscale,int *yscale)
{
	ULONG id = INVALID_ID;

	printf("New display mode: %d x %d x %d ",width,height,1<<log2bpp);

	if(width == oldwidth && height == oldheight && log2bpp == oldlog2bpp)
	{
		printf("-> Existing mode\n");
		return;
	}

	if((width<=0) || (height <= 0) || (log2bpp < 0))
	{
		printf("-> Invalid mode\n");
		exit(EXIT_FAILURE);
	}

	*xscale = 1;
	*yscale = 1;
#if 0 /* TODO - Get scaling working on amiga. Cursor image will need to be scaled, for a start. */
	/* Try and detect rectangular pixel modes */
	if(width >= height*2)
	{
		*yscale = 2;
		height *= 2;
	}
	else if(height >= width*2)
	{
		*xscale = 2;
		width *= 2;
	}
#endif

	/* This forces ArcEm to use an 8-bit Intuition screen, which avoids unnecessary
       screen opening/closing.  However, it is likely to be slower or less memory
       efficient, especially on planar display hardware. */
	if(force8bit && (log2bpp < 3)) log2bpp = 3;


	/* Call BestModeID() to hopefully stop crazy screenmodes */
	id = IGraphics->BestModeID(BIDTAG_NominalWidth,width,
								BIDTAG_NominalHeight,height,
								BIDTAG_DesiredWidth,width,
								BIDTAG_DesiredHeight,height,
								BIDTAG_Depth,1<<log2bpp,
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
											SA_Depth,1<<log2bpp,
											SA_Quiet,TRUE,
											SA_ShowTitle,FALSE,
//											SA_Type,CUSTOMSCREEN,
											SA_PubName,"ArcEm",
											SA_DisplayID,id,
											TAG_DONE);

	if(screen)
	{
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

		IIntuition->SetWindowPointer(window,WA_Pointer,mouseobj,TAG_DONE);

		if(mouse_bm) IGraphics->FreeBitMap(mouse_bm);

		mouse_bm = IGraphics->AllocBitMap(32, 32, 1<<log2bpp, BMF_CLEAR, window->RPort->BitMap);
		mouse_rp.BitMap = mouse_bm;
	}
	else
	{
		printf("-> Failed to create screen\n");
		exit(EXIT_FAILURE);
	}

	if(!window)
	{
		printf("-> Failed to create window\n");
		exit(EXIT_FAILURE);
	}

	IIntuition->PubScreenStatus(screen,0);

	port = window->UserPort;

	friend.BitMap = IGraphics->AllocBitMap(width,height,1<<log2bpp,BMF_CLEAR | BMF_DISPLAYABLE | BMF_INTERLEAVED,window->RPort->BitMap);

    IExec->CopyMem(&friend,&tmprp,sizeof(struct RastPort));
    tmprp.Layer = NULL;
    tmprp.BitMap = IGraphics->AllocBitMap(width,1,1<<log2bpp,0,NULL);

	oldheight = height;
	oldwidth = width;
	oldlog2bpp = log2bpp;
	oldid=id;

	return log2bpp;
}

#if 0
/* ------------------------------------------------------------------ */

/* Standard display device */

typedef unsigned short SDD_HostColour;
#define SDD_Name(x) sdd_##x
static const int SDD_RowsAtOnce = 1;
typedef SDD_HostColour *SDD_Row;

static void sdd_refreshmouse(ARMul_State *state);

static SDD_HostColour *ImageData,*CursorImageData;

static SDD_HostColour SDD_Name(Host_GetColour)(ARMul_State *state,unsigned int col)
{
	/* Convert to 5-bit component values */
	int r = (col & 0x00f) << 1;
	int g = (col & 0x0f0) >> 3;
	int b = (col & 0xf00) >> 7;
	/* May want to tweak this a bit at some point? */
	r |= r>>4;
	g |= g>>4;
	b |= b>>4;
	/* TODO - Check that this is correct for 16bpp on amiga */
	return (r<<10) | (g<<5) | (b);
}  

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz);

static inline SDD_Row SDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset)
{
#error TODO - Return a pointer to a buffer we can write pixels into.
}

static inline void SDD_Name(Host_EndRow)(ARMul_State *state,SDD_Row *row)
{
#error TODO - Do whatever's necessary to flush the updated pixels to the screen. Probably better to do this via BeginUpdate/EndUpdate though, since we won't always write to the row.
}

static inline void SDD_Name(Host_BeginUpdate)(ARMul_State *state,SDD_Row *row,unsigned int count)
{
#error TODO -' This is called when we're about to write to 'count' pixels
}

static inline void SDD_Name(Host_EndUpdate)(ARMul_State *state,SDD_Row *row)
{
#error TODO -' This is called once we've finished writing to the pixels
}

static inline void SDD_Name(Host_SkipPixels)(ARMul_State *state,SDD_Row *row,unsigned int count)
{
#error TODO - Skip ahead 'count' pixels
}

static inline void SDD_Name(Host_WritePixel)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix)
{
#error TODO - Write a pixel and move ahead one
}

static inline void SDD_Name(Host_WritePixels)(ARMul_State *state,SDD_Row *row,SDD_HostColour pix,unsigned int count)
{
#error TODO - Fill 'count' pixels with the given colour. 'count' may be zero!
}

void SDD_Name(Host_PollDisplay)(ARMul_State *state);

#include "../arch/stddisplaydev.c"

static void SDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int hz)
{
	int realdepth = ChangeMode(width,height,4,&HD.XScale,&HD.YScale);

	HD.Width = oldwidth;
	HD.Height = oldheight;
}

/*-----------------------------------------------------------------------------*/
/* Refresh the mouse pointer image                                                */
static void sdd_refreshmouse(ARMul_State *state) {
  int x,y,height,offset;
  int memptr;
  int HorizPos;
  int height = (int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart;
  int VertPos;
  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
	ULONG transcol = 1;
	SDD_HostColour line[32];
	UBYTE maskbit;

	if(!window) return;
	if(!screen) return;

	if(!mouse_bm) return;

 offset=0;
  memptr=MEMC.Cinit*16;

	IGraphics->BltBitMap(friend.BitMap,OldMouseX,OldMouseY,
			window->RPort->BitMap,OldMouseX,OldMouseY,
			32,32,0x0C0,0xff,NULL);

	mask[0] = 0;
	mask[1] = 0;
	mask[2] = 0;
	mask[3] = 0;
	mask[4] = 0;
	mask[5] = 0;
	mask[6] = 0;
	mask[7] = 0;
	mask[8] = 0;
	mask[9] = 0;
	mask[10] = 0;
	mask[11] = 0;
	mask[12] = 0;
	mask[13] = 0;
	mask[14] = 0;
	mask[15] = 0;

  /* Cursor palette */
  SDD_HostColour cursorPal[4];
  cursorPal[0] = transcol;
  for(x=0; x<3; x++) {
    cursorPal[x+1] = SDD_Name(Host_GetColour)(state,VIDC.CursorPalette[x]);
  }

  for(y=0;y<32;y++,memptr+=8,offset+=8) {
// fixed height to 32

    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++) {

		int idx = 0;
		if(y<height)
		{
			idx = (tmp[x/16]>>((x & 15)*2)) & 3;
		}
		line[x] = cursorPal[idx];

		if(!idx) maskbit = 0;
			else maskbit = 1;

		mask[(y*4) + (x/8)] = (mask[(y*4) + (x/8)] << 1) | maskbit;

      }; /* x */
#error TODO - Needs updating for 16bpp
		IGraphics->WritePixelLine8(&mouse_rp,0,y,32,line,&tmprp); // &mouseptr
    } else return;
  }; /* y */

// HorizPos,VertPos
	IGraphics->BltBitMapTags(BLITA_Width, 32,
			BLITA_Height, height,
			BLITA_Source, mouse_bm,
			BLITA_SrcType, BLITT_BITMAP,
			BLITA_Dest, window->RPort,
			BLITA_DestType, BLITT_RASTPORT,
			BLITA_Minterm, (ABC|ABNC|ANBC),
			BLITA_MaskPlane, mask,
			BLITA_DestX, HorizPos,
			BLITA_DestY, VertPos,
			TAG_DONE);

	OldMouseX = HorizPos;
	OldMouseY = VertPos;

}; /* RefreshMouse */

/*-----------------------------------------------------------------------------*/

void
SDD_Name(Host_PollDisplay)(ARMul_State *state)
{
	sdd_refreshmouse(state);
}
#endif

/* ------------------------------------------------------------------ */

/* Palettised display code */
#define PDD_Name(x) pdd_##x

static int BorderPalEntry;

/* Technically this buffer only needs to be big enough to hold UPDATEBLOCKSIZE bytes * max X scale */
#define MAX_DISPLAY_WIDTH 2048
static ARMword RowBuffer[MAX_DISPLAY_WIDTH/4];

typedef struct {
	int x,y; /* Current coordinates in pixels */
	int width; /* Width of area being updated */
} PDD_Row;

static void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz);

static void PDD_Name(Host_SetPaletteEntry)(ARMul_State *state,int i,unsigned int phys)
{
	ULONG r = (phys & 0xf)*0x11111111;
	ULONG g = ((phys>>4) & 0xf)*0x11111111;
	ULONG b = ((phys>>8) & 0xf)*0x11111111;

	IGraphics->SetRGB32(&screen->ViewPort,i,r,g,b);
}

static void PDD_Name(Host_SetBorderColour)(ARMul_State *state,unsigned int phys)
{
	/* Set border palette entry */
	if(BorderPalEntry != 256)
	{
		ULONG r = (phys & 0xf)*0x11111111;
		ULONG g = ((phys>>4) & 0xf)*0x11111111;
		ULONG b = ((phys>>8) & 0xf)*0x11111111;

		IGraphics->SetRGB32(&screen->ViewPort,BorderPalEntry,r,g,b);
	}
}

static inline PDD_Row PDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset,int *alignment)
{
	PDD_Row drow;
	drow.x = offset;
	drow.y = row;
	*alignment = 0;
	return drow;
}

static inline void PDD_Name(Host_EndRow)(ARMul_State *state,PDD_Row *row) { /* nothing */ };

static inline ARMword *PDD_Name(Host_BeginUpdate)(ARMul_State *state,PDD_Row *row,unsigned int count,int *outoffset)
{
	row->width = count>>3;
	*outoffset = 0;
	redraw_miny = MIN(redraw_miny,row->y);
	redraw_maxy = MAX(redraw_maxy,row->y);
	return RowBuffer;
}

static inline void PDD_Name(Host_EndUpdate)(ARMul_State *state,PDD_Row *row)
{
	IGraphics->WritePixelLine8(&friend,row->x,row->y,row->width,RowBuffer,&tmprp);
}

static inline void PDD_Name(Host_AdvanceRow)(ARMul_State *state,PDD_Row *row,unsigned int count)
{
	row->x += count>>3;
}

static void
PDD_Name(Host_PollDisplay)(ARMul_State *state);

static void PDD_Name(Host_DrawBorderRect)(ARMul_State *state,int x,int y,int width,int height)
{
	if(BorderPalEntry != 256)
	{
		/* TODO - Fill rectangle with border colour */
	}
}

#include "../arch/paldisplaydev.c"

void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz)
{
	int realdepth = ChangeMode(width,height,depth,&HD.XScale,&HD.YScale);

	HD.Width = oldwidth;
	HD.Height = oldheight;

	if(realdepth > depth)
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
	if((depth == 3) && (HD.XScale == 1))
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
		HD.ExpandFactor += (3-depth);
		HD.ExpandTable = expandtable;
		unsigned int mul = 1;
		int i;
		for(i=0;i<HD.XScale;i++)
		{
			mul |= 1<<(i*8);
		}
		GenExpandTable(HD.ExpandTable,1<<depth,HD.ExpandFactor,mul);
	}

	/* Screen is expected to be cleared */
	PDD_Name(Host_DrawBorderRect)(state,0,0,HD.Width,HD.Height);
}

/*-----------------------------------------------------------------------------*/
/* Refresh the mouse pointer image                                                */
static void pdd_refreshmouse(ARMul_State *state) {
  int x,y,offset;
  int memptr;
  int HorizPos;
  int height = (int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart;
  int VertPos;
  DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
	ULONG transcol = 16;
	UBYTE line[32];
	ULONG ptr_cols[4];
	ULONG 	col_reg = 16+((0 & 0x06) << 1);
	UBYTE maskbit;

	if(!window) return;
	if(!screen) return;

	if(!mouse_bm) return;

	/* Set up cursor palette */
	int i;
	for(i=1;i<4;i++)
	{
		int phys = VIDC.CursorPalette[i-1];
		ULONG r = (phys & 0xf)*0x11111111;
		ULONG g = ((phys>>4) & 0xf)*0x11111111;
		ULONG b = ((phys>>8) & 0xf)*0x11111111;
		IGraphics->SetRGB32(&screen->ViewPort,col_reg+i,r,g,b);
	}

 offset=0;
  memptr=MEMC.Cinit*16;

	IGraphics->BltBitMap(friend.BitMap,OldMouseX,OldMouseY,
			window->RPort->BitMap,OldMouseX,OldMouseY,
			32,32,0x0C0,0xff,NULL);

	mask[0] = 0;
	mask[1] = 0;
	mask[2] = 0;
	mask[3] = 0;
	mask[4] = 0;
	mask[5] = 0;
	mask[6] = 0;
	mask[7] = 0;
	mask[8] = 0;
	mask[9] = 0;
	mask[10] = 0;
	mask[11] = 0;
	mask[12] = 0;
	mask[13] = 0;
	mask[14] = 0;
	mask[15] = 0;

  for(y=0;y<32;y++,memptr+=8,offset+=8) {
// fixed height to 32

    if (offset<512*1024) {
      ARMword tmp[2];

      tmp[0]=MEMC.PhysRam[memptr/4];
      tmp[1]=MEMC.PhysRam[memptr/4+1];

      for(x=0;x<32;x++) {

		if(y<height)
		{
        	line[x] = ((tmp[x/16]>>((x & 15)*2)) & 3) + col_reg;
		}
		else
		{
			line[x] = transcol;
		}

			if(line[x] == transcol) maskbit = 0;
				else maskbit = 1;

			mask[(y*4) + (x/8)] = (mask[(y*4) + (x/8)] << 1) | maskbit;

      }; /* x */
		IGraphics->WritePixelLine8(&mouse_rp,0,y,32,line,&tmprp); // &mouseptr
    } else return;
  }; /* y */

// HorizPos,VertPos
	IGraphics->BltBitMapTags(BLITA_Width, 32,
			BLITA_Height, height,
			BLITA_Source, mouse_bm,
			BLITA_SrcType, BLITT_BITMAP,
			BLITA_Dest, window->RPort,
			BLITA_DestType, BLITT_RASTPORT,
			BLITA_Minterm, (ABC|ABNC|ANBC),
			BLITA_MaskPlane, mask,
			BLITA_DestX, HorizPos,
			BLITA_DestY, VertPos,
			TAG_DONE);

	OldMouseX = HorizPos;
	OldMouseY = VertPos;

}; /* RefreshMouse */

static void
PDD_Name(Host_PollDisplay)(ARMul_State *state)
{
	if(redraw_miny <= redraw_maxy)
	{
		IGraphics->BltBitMap(friend.BitMap,0,redraw_miny,
				window->RPort->BitMap,0,redraw_miny,
				HD.Width,(redraw_maxy-redraw_miny)+1,0x0C0,0xff,NULL);
		redraw_miny = INT_MAX;
		redraw_maxy = 0;
	}

	pdd_refreshmouse(state);
}

#undef DISPLAYINFO
#undef HOSTDISPLAY
#undef DC
#undef HD

/* ------------------------------------------------------------------ */

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

void cleanup(void)
{
#ifdef SOUND_SUPPORT
	sound_exit();
#endif

	IGraphics->WaitBlit();

	if(mouseobj)
	{
		IIntuition->SetWindowPointer(window,TAG_DONE);
		IIntuition->DisposeObject(mouseobj);
	}

	if(mouseptr.BitMap)
	{
		IGraphics->FreeBitMap(mouseptr.BitMap);
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
Kbd_PollHostKbd(ARMul_State *state)
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


/*-----------------------------------------------------------------------------*/

int
DisplayDev_Init(ARMul_State *state)
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

	ARexx_Init();

	IGraphics->InitRastPort(&mouseptr);
	IGraphics->InitRastPort(&mouse_rp);
	IGraphics->InitRastPort(&friend);
	mask = IGraphics->AllocRaster(32,32);

	/* blank mouse pointer image */
	mouseptr.BitMap = IGraphics->AllocBitMap(1,1,1,BMF_CLEAR,NULL);
	mouseobj = IIntuition->NewObject(NULL,"pointerclass",POINTERA_BitMap,mouseptr.BitMap,POINTERA_WordWidth,2,POINTERA_XOffset,0,POINTERA_YOffset,0,POINTERA_XResolution,POINTERXRESN_SCREENRES,POINTERA_YResolution,POINTERYRESN_SCREENRESASPECT,TAG_DONE);

	filereq = IAsl->AllocAslRequestTags(ASL_FileRequest,
									ASLFR_RejectIcons,TRUE,
	                             	ASLFR_InitialDrawer,"arexx",
									ASLFR_InitialPattern,"#?.arcem",
									ASLFR_DoPatterns,TRUE,
									TAG_DONE);

#if 0
	return DisplayDev_Set(state,&SDD_DisplayDev);
#else
	return DisplayDev_Set(state,&PDD_DisplayDev);
#endif
}
