/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info
   Nintendo DS version by Cameron Cawley, 2020-2023 */
/* Display and keyboard interface for the Arc emulator */

#include <string.h>

#include "../armdefs.h"
#include "../arch/armarc.h"
#include "../arch/dbugsys.h"
#include "../arch/keyboard.h"
#include "../arch/displaydev.h"
#include "../eventq.h"
#include "ControlPane.h"

#include <nds.h>

ARMul_State nds_statestr DTCM_BSS;
void *state_alloc(int s) { return &nds_statestr; }
void state_free(void *p) {}

static inline s32 calculate_scale(s32 width, s32 height) {
	const s32 screenAspect = intToFixed(SCREEN_WIDTH, 8) / SCREEN_HEIGHT;
	s32 rectAspect = intToFixed(width, 8) / height;

	s32 scaleFactor;
	if (screenAspect > rectAspect)
		scaleFactor = intToFixed(SCREEN_HEIGHT, 8) / height;
	else
		scaleFactor = intToFixed(SCREEN_WIDTH, 8) / width;

	return scaleFactor;
}

static inline u16 ConvertPhysToColour(int phys)
{
	/* Convert to 5-bit component values */
	int r = (phys & 0x00f) << 1;
	int g = (phys & 0x0f0) >> 3;
	int b = (phys & 0xf00) >> 7;
	/* May want to tweak this a bit at some point? */
	r |= r>>4;
	g |= g>>4;
	b |= b>>4;
	return RGB15(r, g, b);
}

static void RefreshMouse(ARMul_State *state);

/*-----------------------------------------------------------------------------*/
/* Palettised display code */
#define PDD_Name(x) pdd_##x
#define PDD_MonitorWidth 1024
#define PDD_MonitorHeight 512
#define PDD_BgSize BgSize_B8_1024x512

static int background;

static s32 xscale = intToFixed(1, 8), yscale = intToFixed(1, 8);
static s32 xoffset = 0, yoffset = 0;

/**
 * This uses an intermediate buffer since cache hits in main RAM are faster than VRAM writes.
 * It would have been nice to put this into TCM, but that would prevent it from being used for DMA.
 */
static ARMword RowBuffer[PDD_MonitorWidth/4] ALIGN(32);

typedef struct {
	ARMword *src;
	uint8_t *dst;
	unsigned int count;
} PDD_Row;


static void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz);

static inline void PDD_Name(Host_SetPaletteEntry)(ARMul_State *state,int i,uint_fast16_t phys)
{
	BG_PALETTE[i] = ConvertPhysToColour(phys);
}

static inline void PDD_Name(Host_SetCursorPaletteEntry)(ARMul_State *state,int i,uint_fast16_t phys)
{
	// TODO: Implement this!
}

static inline void PDD_Name(Host_SetBorderColour)(ARMul_State *state,uint_fast16_t phys)
{
	// TODO: Implement this!
}

static inline PDD_Row PDD_Name(Host_BeginRow)(ARMul_State *state,int row,int offset,int *alignment)
{
	PDD_Row drow;
	drow.dst = ((uint8_t *)bgGetGfxPtr(background)) + (row * PDD_MonitorWidth) + offset;
	drow.src = RowBuffer;
	drow.count = 0;
	*alignment = 0;
	return drow;
}

static inline void PDD_Name(Host_EndRow)(ARMul_State *state,PDD_Row *row)
{
	while (dmaBusy(3));
}

static inline ARMword *PDD_Name(Host_BeginUpdate)(ARMul_State *state,PDD_Row *row,unsigned int count,int *outoffset)
{
	row->count = count;
	*outoffset = 0;
	return row->src;
}

static inline void PDD_Name(Host_EndUpdate)(ARMul_State *state,PDD_Row *row)
{
	unsigned int count = row->count;

	DC_FlushRange(row->src, count>>3);
	while (dmaBusy(3));
	dmaCopyWordsAsynch(3, row->src, row->dst, count>>3);

	row->src += count>>5;
}

static inline void PDD_Name(Host_TransferUpdate)(ARMul_State *state,PDD_Row *row,unsigned int count,const ARMword *src)
{
	DC_FlushRange(src, count>>3);
	while (dmaBusy(3));
	dmaCopyWordsAsynch(3, src, row->dst, count>>3);
}

static inline void PDD_Name(Host_AdvanceRow)(ARMul_State *state,PDD_Row *row,unsigned int count)
{
	row->dst += count>>3;
}

static inline void PDD_Name(Host_PollDisplay)(ARMul_State *state)
{
	RefreshMouse(state);
	oamUpdate(&oamMain);
	bgUpdate();
	ControlPane_Redraw();
}

static inline void PDD_Name(Host_DrawBorderRect)(ARMul_State *state,int x,int y,int width,int height)
{
	// TODO: Implement this!
}

#include "../arch/paldisplaydev.c"

static void PDD_Name(Host_ChangeMode)(ARMul_State *state,int width,int height,int depth,int hz)
{
	s32 xs, ys;

	if((width > PDD_MonitorWidth) || (height > PDD_MonitorHeight))
	{
		warn("Mode %dx%d too big\n",width,height);
		DC.ModeChanged = 1;
		return;
	}

	HD.Width = width;
	HD.Height = height;
	HD.XScale = 1;
	HD.YScale = 1;

	/* Calculate expansion params */
	if(depth == 3)
	{
		/* No expansion */;
		HD.ExpandTable = NULL;
	}
	else
	{
		/* Expansion! */
		static ARMword expandtable[256] DTCM_BSS;
		HD.ExpandFactor = (3-depth);
		HD.ExpandTable = expandtable;
		GenExpandTable(HD.ExpandTable,1<<depth,HD.ExpandFactor,1);
	}

	/* Try and detect rectangular pixel modes */
	if (width >= height*2) {
		xscale = yscale = calculate_scale(width, height * 2);
		yscale *= 2;
	} else if (height >= width) {
		xscale = yscale = calculate_scale(width * 2, height);
		xscale *= 2;
	} else {
		xscale = yscale = calculate_scale(width, height);
	}

	xs = (1<<16) / xscale;
	ys = (1<<16) / yscale;

	xoffset = (SCREEN_WIDTH - fixedToInt(width * xscale, 8)) >> 1;
	yoffset = (SCREEN_HEIGHT - fixedToInt(height * yscale, 8)) >> 1;

	bgSet(background, 0, xs, ys, -xoffset << 8, -yoffset << 8, 0, 0);
	oamRotateScale(&oamMain, 0, 0, xs, ys);

	/* Screen is expected to be cleared */
	dmaFillHalfWords(0, bgGetGfxPtr(background), PDD_MonitorWidth * PDD_MonitorHeight);
}


/*-----------------------------------------------------------------------------*/

typedef struct { u16 data[8][2]; } Tile;
typedef struct { Tile data[8][4]; } Sprite;
Sprite *cursorData;
int oldHeight = 64;

static inline u16 Convert2bppTo4bpp(u8 byte) {
	return (byte & 0x03) | ((byte & 0x0C) << 2) | ((byte & 0x30) << 4) | ((byte & 0xC0) << 6);
}

/*-----------------------------------------------------------------------------*/
/* Refresh the mouse's image                                                   */
static void RefreshMouse(ARMul_State *state) {
	int x = 0, y = 0, i = 0;
	int HorizPos = 0, VertPos = 0;
	int Height = ((int)VIDC.Vert_CursorEnd - (int)VIDC.Vert_CursorStart)*HD.YScale;
	if (Height < 0) Height = 0;

	DisplayDev_GetCursorPos(state,&HorizPos,&VertPos);
	HorizPos = fixedToInt(HorizPos * xscale, 8);
	VertPos = fixedToInt(VertPos * yscale, 8);

	if (Height > 64) Height = 64;
	if (VertPos < 0) VertPos = 0;

	/* TODO: This might not be as fast as it could be */
	u8 *src = ((u8 *)MEMC.PhysRam) + (MEMC.Cinit * 16);
	for (y = 0; y < Height; y++) {
		int ytile = y / 8;
		int ydata = y % 8;
		for (x = 0; x < 32; x += 8) {
			int xtile = x / 8;
			cursorData->data[ytile][xtile].data[ydata][0] = Convert2bppTo4bpp(*src++);
			cursorData->data[ytile][xtile].data[ydata][1] = Convert2bppTo4bpp(*src++);
		}
	}

	if (oldHeight > Height) {
		for (y = Height; y < oldHeight; y++) {
			int ytile = y / 8;
			int ydata = y % 8;
			for (x = 0; x < 32; x += 8) {
				int xtile = x / 8;
				cursorData->data[ytile][xtile].data[ydata][0] = 0;
				cursorData->data[ytile][xtile].data[ydata][1] = 0;
			}
		}
	}
	oldHeight = Height;

	/* Cursor palette */
	SPRITE_PALETTE[0] = 0;
	for(i = 0; i < 3; i++) {
		SPRITE_PALETTE[i + 1] = ConvertPhysToColour(VIDC.CursorPalette[i]);
	}

	oamSet(&oamMain, 0, HorizPos, VertPos, 0, 0, SpriteSize_32x64, SpriteColorFormat_16Color,
	       cursorData, -1, false, false, false, false, false);
}; /* RefreshMouse */

/*-----------------------------------------------------------------------------*/
bool
DisplayDev_Init(ARMul_State *state)
{
	/* Setup display and cursor bitmaps */
	videoSetMode(MODE_6_2D | DISPLAY_BG2_ACTIVE);

	vramSetPrimaryBanks(VRAM_A_MAIN_BG,VRAM_B_MAIN_BG,VRAM_C_MAIN_BG,VRAM_D_MAIN_BG);
	background = bgInit(2, BgType_Bmp8, PDD_BgSize, 0, 0);

	vramSetBankE(VRAM_E_MAIN_SPRITE);
	oamInit(&oamMain, SpriteMapping_1D_32, false);
	cursorData = (Sprite *)oamAllocateGfx(&oamMain, SpriteSize_32x64, SpriteColorFormat_16Color);

	ControlPane_Init(state);

	return DisplayDev_Set(state,&PDD_DisplayDev);
}

/*-----------------------------------------------------------------------------*/

typedef struct {
    int sym;
    arch_key_id kid;
} button_to_arch_key;

/* TODO: Provide a GUI for remapping the buttons */
static const button_to_arch_key button_to_arch_key_map[] = {
    { KEY_Y,     ARCH_KEY_left      },
    { KEY_B,     ARCH_KEY_down      },
    { KEY_A,     ARCH_KEY_right     },
    { KEY_X,     ARCH_KEY_up        },
    { KEY_L,     ARCH_KEY_shift_r   },
    { KEY_R,     ARCH_KEY_control_r },
    { KEY_LEFT,  ARCH_KEY_button_1  },
    { KEY_DOWN,  ARCH_KEY_button_2  },
    { KEY_RIGHT, ARCH_KEY_button_3  },
    { KEY_UP,    ARCH_KEY_space     },
    { 0, 0 }
};

static void ProcessButtons(ARMul_State *state, int pressed, int released) {
	const button_to_arch_key *btak;
	for (btak = button_to_arch_key_map; btak->sym; btak++) {
		if ((pressed & btak->sym) && btak->kid != 0)
			keyboard_key_changed(&KBD, btak->kid, 0);
		if ((released & btak->sym) && btak->kid != 0)
			keyboard_key_changed(&KBD, btak->kid, 1);
	}
}; /* ProcessButtons */

/*-----------------------------------------------------------------------------*/
int
Kbd_PollHostKbd(ARMul_State *state)
{
	touchPosition touch;
	scanKeys();

	int pressed = keysDown();
	int released = keysUp();
	int held = keysHeld();
	ProcessButtons(state, pressed, released);

	if (pressed & KEY_TOUCH) {
		touchRead(&touch);
		ControlPane_ProcessTouchPressed(state, touch.px, touch.py);
	} else if (held & KEY_TOUCH) {
		touchRead(&touch);
		ControlPane_ProcessTouchHeld(state, touch.px, touch.py);
	} else if (released & KEY_TOUCH) {
		ControlPane_ProcessTouchReleased(state);
	}

	return 0;
}
