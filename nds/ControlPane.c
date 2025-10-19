/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "../arch/archio.h"
#include "../arch/armarc.h"
#include "ControlPane.h"
#include "../arch/dbugsys.h"
#include "../arch/keyboard.h"

#include "KeyTable.h"

#include "bg_gfx.h"
#include "bg_map.h"
#include "bg_pal.h"
#include "font_gfx.h"
#include "font_pal.h"
#include "keys_gfx.h"

#include <stdarg.h>
#include <stdio.h>

#include <nds.h>

enum {
	DRAG_NONE,
	DRAG_MOUSE,
	DRAG_WINDOW_1,
	DRAG_WINDOW_2,
	DRAG_WINDOW_3
};

enum {
	SIDEL = 1,
	SIDER = 1 | TILE_FLIP_H,
	SIDET = 2,
	SIDEB = 2 | TILE_FLIP_V,
	BARV  = 3,
	BARH  = 4,
	CORNERTL = 5,
	CORNERTR = 5 | TILE_FLIP_H,
	CORNERBL = 5 | TILE_FLIP_V,
	CORNERBR = 5 | TILE_FLIP_H | TILE_FLIP_V,
	PIPET = 7,
	PIPEB = 7 | TILE_FLIP_V,
	PIPEL = 8,
	PIPER = 8 | TILE_FLIP_H,
	PIPEIT = 9,
	PIPEIB = 9 | TILE_FLIP_V,
	PIPEIL = 10,
	PIPEIR = 10 | TILE_FLIP_H,
	PIPEC = 11,

	CLOSE = 0x84,
	DOWN = 0x8a,
	UP = 0x8b,
	ELLIPSIS = 0x8c
};

typedef struct {
	int px, py, pw, ph;
	int bx, by, bw, bh;
	int closex;
	int layer;
	u16 *map;
} Window;

static int old_px = -1;
static int old_py = -1;
static int drag_mode = DRAG_NONE;

static bool keys_caps = false;
static arch_key_id key_pressed = -1;

static bool has_console = false;
static bool has_keyboard = false;
static bool has_windows = false;

static Window windows[3];

static void ControlPane_InitKeyboard(ARMul_State *state);
static void ControlPane_InitWindows(void);

static Window *ControlPane_MessageBox(int layer, const char *title, const char *message);

static void draw_floppy_leds(unsigned int leds)
{
	unsigned int floppy;

	for (floppy = 0; floppy < 4; floppy++) {
		BG_PALETTE_SUB[15 - floppy] = (leds & (1 << floppy)) ? RGB8(0xff, 0xbb, 0x00) : RGB8(0x99, 0x99, 0x99);
	}
}

static void draw_floppy(unsigned int floppy, bool inserted) {
	if (inserted) {
		BG_PALETTE_SUB[10 - (floppy * 2)] = RGB8(0xff, 0xbb, 0x00);
		BG_PALETTE_SUB[11 - (floppy * 2)] = RGB8(0xdd, 0x00, 0x00);
	} else {
		BG_PALETTE_SUB[10 - (floppy * 2)] = 0;
		BG_PALETTE_SUB[11 - (floppy * 2)] = 0;
	}
}

/*----------------------------------------------------------------------------*/

void ControlPane_Init(ARMul_State *state)
{
	static bool init = false;
	if (init)
		return;
	init = true;

	videoSetModeSub(MODE_0_2D | DISPLAY_BG3_ACTIVE);
	vramSetBankH(VRAM_H_SUB_BG);
	vramSetBankI(VRAM_I_SUB_SPRITE);
	oamInit(&oamSub, SpriteMapping_1D_128, false);

#ifndef NDEBUG
	scanKeys();

	int held = keysHeld();
	if (held & KEY_SELECT) {
		consoleInit(NULL, 3, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
		has_console = true;
	}
#endif

	if (!has_console)
	{
		BGCTRL_SUB[3] = BG_TILE_BASE(0) | BG_MAP_BASE(5) | BG_COLOR_16 | BG_32x32;

		dmaCopy(bg_gfx, (void *)(CHAR_BASE_BLOCK_SUB(0) + (0x100 * 32)), bg_gfx_size);
		dmaCopy(bg_map, (void *)SCREEN_BASE_BLOCK_SUB(5), bg_map_size);
		dmaCopy(bg_pal, BG_PALETTE_SUB, bg_pal_size);

		ControlPane_InitWindows();
		ControlPane_InitKeyboard(state);

		if (state)
			FDC_SetLEDsChangeFunc(draw_floppy_leds);
		ControlPane_Redraw();
	}
}

void ControlPane_Error(bool fatal,const char *fmt,...)
{
	/* TODO: Allow continuing emulation when !fatal */
	char msg[256];
	Window *w;
	va_list args;
	va_start(args,fmt);

	ControlPane_Init(NULL);

#ifndef NDEBUG
	if (has_console) {
		/* Log it */
		vprintf(fmt,args);
		va_end(args);

#ifdef __CALICO__
		while(pmMainLoop())
#else
		while(1)
#endif
		{
			swiWaitForVBlank();
			scanKeys();
			if (keysDown() & KEY_START)
				break;
		}

		/* Quit */
		exit(EXIT_FAILURE);
	}
#endif

	vsnprintf(msg, 256, fmt, args);
	va_end(args);

	w = ControlPane_MessageBox(0, "Error", msg);

#ifdef __CALICO__
	while(pmMainLoop())
#else
	while(1)
#endif
	{
		if (w->layer < 0)
			break;
		swiWaitForVBlank();
		Kbd_PollHostKbd(NULL);
	}

	/* Quit */
	exit(EXIT_FAILURE);
}

/*----------------------------------------------------------------------------*/

#if 0
static void ControlPane_TextDrawChar(Window *window, u16 c, int x, int y)
{
	x += window->bx;
	y += window->by;
	window->map[x + y * 32] = (1 << 12) | c;
}
#endif

static void ControlPane_TextDrawString(Window *window, const char *c, int x, int y, unsigned int w, bool centred)
{
	u16 *ptr, *last;
	size_t len = strlen(c);

	x += window->bx;
	y += window->by;

	ptr = window->map + (y * 32) + x;
	last = ptr + w - 1;

	if (len < w && centred)
		ptr += (w - len) / 2;

	while (*c) {
		if (ptr == last && c[1] != 0) {
			*ptr++ = (1 << 12) | ELLIPSIS;
			break;
		}

		*ptr++ = (1 << 12) | *c++;
	}
}

static void ControlPane_TextDrawBlock(Window *window, const u16 *c, int x, int y, int w, int h)
{
	int i, j;
	x += window->bx;
	y += window->by;

	for (i = y; i < y + h; i++) {
		for (j = x; j < x + w; j++) {
			window->map[(i * 32) + j] = (1 << 12) | *c++;
		}
	}
}

static void ControlPane_TextDrawLineH(Window *window, u16 c, int x, int y, int w)
{
	int i;
	x += window->bx;
	y += window->by;

	for (i = x; i < x + w; i++) {
		window->map[(y * 32) + i] = (1 << 12) | c;
	}
}

static void ControlPane_TextDrawLineV(Window *window, u16 c, int x, int y, int h)
{
	int i;
	x += window->bx;
	y += window->by;

	for (i = y; i < y + h; i++) {
		window->map[(i * 32) + x] = (1 << 12) | c;
	}
}

static Window *ControlPane_CreateWindow(int layer, unsigned int w, unsigned int h, const char *title, bool scroll)
{
	static const u16 TL[1*3] = {
		CORNERTL,
		SIDEL,
		PIPEL
	};
	static const u16 BL[1*1] = {
		CORNERBL
	};
	static const u16 TR[3*3] = {
		PIPET,  SIDET, CORNERTR,
		BARV,   CLOSE, SIDER,
		PIPEIB, BARH,  PIPER
	};
	static const u16 BR[1*1] = {
		CORNERBR
	};
	static const u16 TR_S[3*5] = {
		PIPET,  SIDET, CORNERTR,
		BARV,   CLOSE, SIDER,
		PIPEC,  BARH,  PIPER,

		BARV,   UP,   SIDER,
		PIPEIL, BARH, PIPER
	};
	static const u16 BR_S[3*3] = {
		PIPEIL, BARH,  PIPER,
		BARV,   DOWN,  SIDER,
		PIPEB,  SIDEB, CORNERBR
	};
	vu16 *scrollXY = REG_BGOFFSETS_SUB + (layer * 2);
	Window *window;
	int mapbase;
	size_t i;

	mapbase = 6 + (layer * 4);
	BGCTRL_SUB[layer] = BG_TILE_BASE(0) | BG_MAP_BASE(mapbase) | BG_COLOR_16 | BG_64x64;
	window = &windows[layer];
	window->map = (u16 *)SCREEN_BASE_BLOCK_SUB(mapbase);
	window->layer = layer;
	window->pw = (w + (scroll ? 4 : 2)) * 8;
	window->ph = (h + 4) * 8;
	window->px = (256 - window->pw) / 2;
	window->py = (192 - window->ph) / 2;
	window->bx = 1;
	window->by = 3;
	window->bw = w;
	window->bh = h;

	dmaFillHalfWords(1<<12, window->map, 64*64*2);

	/* Left hand side */
	ControlPane_TextDrawBlock(window, TL,    -1, -3, 1, 3);
	ControlPane_TextDrawLineV(window, SIDEL, -1, 0, h);
	ControlPane_TextDrawBlock(window, BL,    -1, h, 1, 1);

	/* Middle */
	ControlPane_TextDrawLineH(window, SIDET, 0, -3, w);
	ControlPane_TextDrawLineH(window, ' ',   0, -2, w);
	ControlPane_TextDrawLineH(window, BARH,  0, -1, w);
	for (i = 0; i < h; i++)
		ControlPane_TextDrawLineH(window, ' ',  0, i, w);
	ControlPane_TextDrawLineH(window, SIDEB, 0, h, w);


	if (scroll) {
		/* Title */
		ControlPane_TextDrawString(window, title, 0, -2, w, true);

		/* Right hand side */
		ControlPane_TextDrawBlock(window, TR_S,  w + 0, -3, 3, 5);
		ControlPane_TextDrawLineV(window, BARV,  w + 0, 2, h - 3);
		ControlPane_TextDrawLineV(window, ' ',   w + 1, 2, h - 3);
		ControlPane_TextDrawLineV(window, SIDER, w + 2, 2, h - 3);
		ControlPane_TextDrawBlock(window, BR_S,  w + 0, h - 2, 3, 3);

		window->closex = 1 + w;
	} else {
		/* Title */
		ControlPane_TextDrawString(window, title, 0, -2, w - 2, true);

		/* Right hand side */
		ControlPane_TextDrawBlock(window, TR,    w - 2, -3, 3, 3);
		ControlPane_TextDrawLineV(window, SIDER, w, 0, h);
		ControlPane_TextDrawBlock(window, BR,    w, h, 1, 1);

		window->closex = 1 + w - 2;
	}

	scrollXY[0] = 512 - window->px;
	scrollXY[1] = 512 - window->py;

	videoBgEnableSub(window->layer);

	return window;
}

static void ControlPane_CloseWindow(Window *window)
{
	videoBgDisableSub(window->layer);
	memset(window, 0, sizeof(Window));
	window->layer = -1;
}

static void ControlPane_InitWindows(void)
{
	size_t i;

	decompress(font_gfx, (void *)CHAR_BASE_BLOCK_SUB(0), LZ77Vram);
	dmaCopy(font_pal, BG_PALETTE_SUB + 16, font_pal_size);

	for (i = 0; i < sizeof(windows)/sizeof(*windows); i++) {
		ControlPane_CloseWindow(&windows[i]);
	}

	has_windows = true;
}

static bool ControlPane_ClickWindow(ARMul_State *state, int px, int py)
{
	Window *w;
	size_t i;

	for (i = 0; i < sizeof(windows)/sizeof(*windows); i++) {
		w = &windows[i];
		/* Is the window open? */
		if (w->layer < 0)
			continue;

		/* Are the co-ordinates within the window? */
		if (!(px >= w->px && px < w->px + w->pw && py >= w->py && py < w->py + w->ph))
			continue;

		/* Handle clicking on the title bar */
		if (py < w->py + (8 * w->by)) {
			if (px > w->px + (8 * w->closex)) {
				ControlPane_CloseWindow(w);
			} else {
				drag_mode = DRAG_WINDOW_1 + i;
				old_px = px;
				old_py = py;
			}
		}

		return true;
	}
	return false;
}

static bool ControlPane_DragWindow(ARMul_State *state, int px, int py)
{
	int xdiff, ydiff;
	Window *w = &windows[drag_mode - DRAG_WINDOW_1];
	vu16 *scrollXY = REG_BGOFFSETS_SUB + (w->layer * 2);

	xdiff = (px - old_px);
	ydiff = (py - old_py);

	w->px += xdiff;
	w->py += ydiff;
	scrollXY[0] = 512 - w->px;
	scrollXY[1] = 512 - w->py;

	old_px = px;
	old_py = py;
	return true;
}

/*----------------------------------------------------------------------------*/

static Window *ControlPane_MessageBox(int layer, const char *title, const char *message)
{
	char line[17], *msg, *token, *saveptr;
	size_t linepos = 0, toklen;
	Window *w;
	int y = 0;

	w = ControlPane_CreateWindow(layer, 16, 6, title, false);

	msg = strdup(message);
	memset(line, 0, sizeof(line));

	token = strtok_r(msg, " ", &saveptr);
	while (token != NULL) {
		toklen = strlen(token);
		if (linepos + toklen + 1 > 16) {
			ControlPane_TextDrawString(w, line, 0, y, 16, true);
			memset(line, 0, sizeof(line));
			linepos = 0;
			y++;
		}

		if (linepos > 0) {
			strcat(line, " ");
			linepos += toklen;
		}

		strcat(line, token);
		linepos += toklen + 1;
		token = strtok_r(NULL, " ", &saveptr);
	}

	if (linepos > 0)
		ControlPane_TextDrawString(w, line, 0, y, 16, true);
	free(msg);

	return w;
}

/*----------------------------------------------------------------------------*/

static void ControlPane_UpdateKeyboardLEDs(uint8_t leds);

static void ControlPane_InitKeyboard(ARMul_State *state)
{
	static const u16 keys_pal[] = {
		/* 0: Not pressed, shift off, leds off */
		0x5FBD,0x6F7B,0x5EF7,0x7FFF,0x39CE,0x4E73,0x5EF7,0x0000,
		0x0000,0x0000,0x6F7B,0x39CE,0x5FBD,0x022A,0x02FF,0x7EE0,
		/* 1: Not pressed, shift off, leds on */
		0x5FBD,0x6F7B,0x5EF7,0x7FFF,0x39CE,0x4E73,0x5EF7,0x0000,
		0x0000,0x0000,0x6F7B,0x0320,0x5FBD,0x022A,0x02FF,0x7EE0,
		/* 2: Not pressed, shift on, leds off */
		0x5FBD,0x6F7B,0x5EF7,0x7FFF,0x39CE,0x4E73,0x5EF7,0x0000,
		0x6F7B,0x0000,0x0000,0x39CE,0x5FBD,0x022A,0x02FF,0x7EE0,
		/* 3: Not pressed, shift on, leds on */
		0x5FBD,0x6F7B,0x5EF7,0x7FFF,0x39CE,0x4E73,0x5EF7,0x0000,
		0x6F7B,0x0000,0x0000,0x0320,0x5FBD,0x022A,0x02FF,0x7EE0,
		/* 4: Pressed, shift off, leds off */
		0x5FBD,0x6F7B,0x7FFF,0x5EF7,0x5EF7,0x4E73,0x39CE,0x0000,
		0x0000,0x0000,0x6F7B,0x39CE,0x5FBD,0x022A,0x02FF,0x7EE0,
		/* 5: Pressed, shift off, leds on */
		0x5FBD,0x6F7B,0x7FFF,0x5EF7,0x5EF7,0x4E73,0x39CE,0x0000,
		0x0000,0x0000,0x6F7B,0x0320,0x5FBD,0x022A,0x02FF,0x7EE0,
		/* 6: Pressed, shift on, leds off */
		0x5FBD,0x6F7B,0x7FFF,0x5EF7,0x5EF7,0x4E73,0x39CE,0x0000,
		0x6F7B,0x0000,0x0000,0x39CE,0x5FBD,0x022A,0x02FF,0x7EE0,
		/* 7: Pressed, shift on, leds on */
		0x5FBD,0x6F7B,0x7FFF,0x5EF7,0x5EF7,0x4E73,0x39CE,0x0000,
		0x6F7B,0x0000,0x0000,0x0320,0x5FBD,0x022A,0x02FF,0x7EE0
	};

	/* We manage sprite VRAM manually here instead of using oamAllocateGfx() */
	decompress(keys_gfx, SPRITE_GFX_SUB, LZ77Vram);
	dmaCopy(keys_pal, SPRITE_PALETTE_SUB, sizeof(keys_pal));

	if (state)
		KBD.leds_changed = ControlPane_UpdateKeyboardLEDs;

	has_keyboard = true;
}

static void ControlPane_UpdateKeyboardLEDs(uint8_t leds)
{
	keys_caps = (leds & KBD_LED_CAPSLOCK);
}

static bool ControlPane_ClickKeyboard(ARMul_State *state, unsigned int px, unsigned int py)
{
	const dvk_to_vkeybd *ktvk;

	if (!state)
		return false;

	for (ktvk = dvk_to_vkeybd_map; ktvk->sprite; ktvk++) {
		unsigned int width = ((ktvk->sprite) >> 8) * 16;
		unsigned int height = 16;

		if (px >= ktvk->x && px < ktvk->x + width &&
		    py >= ktvk->y && py < ktvk->y + height) {
			keyboard_key_changed(&KBD, ktvk->kid, 0);
			key_pressed = ktvk->kid;
			return true;
		}
	}
	return false;
}

static void ControlPane_ReleaseKeyboard(ARMul_State *state)
{
	if (key_pressed != (arch_key_id)-1) {
		if (state)
			keyboard_key_changed(&KBD, key_pressed, 1);
		key_pressed = -1;
	}
}

static void ControlPane_DrawKeyboard(void)
{
	const dvk_to_vkeybd *ktvk;
	unsigned int i = 0;

	for (ktvk = dvk_to_vkeybd_map; ktvk->sprite; ktvk++) {
		unsigned int sprite = (ktvk->sprite) & 0xFF;
		unsigned int width = (ktvk->sprite) >> 8;
		unsigned int x = ktvk->x, y = ktvk->y;
		unsigned int palette = 0;

		if (keys_caps) palette |= 1;
		if (ktvk->kid == key_pressed) palette |= 4;

		do {
			oamSet(&oamSub, i++, x, y, 3, palette, SpriteSize_16x16, SpriteColorFormat_16Color,
			       &SPRITE_GFX_SUB[sprite++ * 64], -1, false, false, false, false, false);
			x += 16;
		} while (--width);
	}
}

/*----------------------------------------------------------------------------*/

static bool ControlPane_ClickFloppy(ARMul_State *state, int drive)
{
	const char *err;
	char tmp[256];

	if (!state)
		return false;

	if (FDC_IsFloppyInserted(drive)) {
		err = FDC_EjectFloppy(drive);
		if (err == NULL)
			draw_floppy(drive, false);
		else
			ControlPane_MessageBox(1, "Error", err);
	} else {
		sprintf(tmp, "FloppyImage%d", drive);
		err = FDC_InsertFloppy(drive, tmp);
		if (err == NULL)
			draw_floppy(drive, true);
		else
			ControlPane_MessageBox(1, "Error", err);
	}
	return true;
}

/*----------------------------------------------------------------------------*/

static bool ControlPane_ClickTouchpad(ARMul_State *state, int px, int py)
{
	if (!state)
		return false;
	drag_mode = DRAG_MOUSE;
	old_px = px;
	old_py = py;
	return true;
}

static bool ControlPane_DragTouchpad(ARMul_State *state, int px, int py)
{
	int xdiff, ydiff;

	xdiff = (px - old_px) << 2;
	ydiff = (py - old_py) << 2;

	if (xdiff > 63)
		xdiff = 63;
	if (xdiff < -63)
		xdiff = -63;

	if (ydiff > 63)
		ydiff = 63;
	if (ydiff < -63)
		ydiff = -63;

	old_px += xdiff >> 2;
	old_py += ydiff >> 2;

	if (state) {
		KBD.MouseXCount = xdiff & 127;
		KBD.MouseYCount = -ydiff & 127;
	}
	return true;
}

/*----------------------------------------------------------------------------*/

bool ControlPane_ProcessTouchPressed(ARMul_State *state, int px, int py)
{
	if (has_windows && ControlPane_ClickWindow(state, px, py))
		return true;
	if (has_keyboard && ControlPane_ClickKeyboard(state, px, py))
		return true;

	if (has_console) {
		return ControlPane_ClickTouchpad(state, px, py);
	} else if (py > 183) {
		int drive = 3 - (px / 64);
		return ControlPane_ClickFloppy(state, drive);
	} else if (py < 78) {
		return ControlPane_ClickTouchpad(state, px, py);
	}

	return false;
}

bool ControlPane_ProcessTouchHeld(ARMul_State *state, int px, int py)
{
	switch (drag_mode) {
	case DRAG_MOUSE:
		return ControlPane_DragTouchpad(state, px, py);
	case DRAG_WINDOW_1:
	case DRAG_WINDOW_2:
	case DRAG_WINDOW_3:
		return ControlPane_DragWindow(state, px, py);
	}

	return false;
}

bool ControlPane_ProcessTouchReleased(ARMul_State *state)
{
	bool retval = false;

	ControlPane_ReleaseKeyboard(state);

	if (drag_mode != DRAG_NONE) {
		drag_mode = DRAG_NONE;
		old_px = -1;
		old_py = -1;
		retval = true;
	}

	return retval;
}

void ControlPane_Redraw(void)
{
	ControlPane_DrawKeyboard();
	oamUpdate(&oamSub);
}

/*----------------------------------------------------------------------------*/

void log_msgv(int type, const char *format, va_list ap)
{
  if (type >= LOG_WARN)
    vfprintf(stderr, format, ap);
  else
    vfprintf(stdout, format, ap);
}
