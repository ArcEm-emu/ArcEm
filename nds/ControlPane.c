/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "../arch/archio.h"
#include "../arch/armarc.h"
#include "ControlPane.h"
#include "../arch/dbugsys.h"
#include "../arch/keyboard.h"

#include "KeyTable.h"

#include "img/bg.h"
#include "img/keys.h"

#include <stdarg.h>
#include <stdio.h>

#include <nds.h>

enum {
	DRAG_NONE,
	DRAG_MOUSE
};

static int old_px = -1;
static int old_py = -1;
static int drag_mode = DRAG_NONE;

static bool keys_caps = false;
static arch_key_id key_pressed = -1;

static bool has_console = false;
static bool has_keyboard = false;

static void ControlPane_InitKeyboard(ARMul_State *state);

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
	videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);
	vramSetBankH(VRAM_H_SUB_BG);
	vramSetBankI(VRAM_I_SUB_SPRITE);
	oamInit(&oamSub, SpriteMapping_1D_128, false);

#ifndef NDEBUG
	scanKeys();

	int held = keysHeld();
	if (held & KEY_SELECT) {
		consoleInit(NULL, 1, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
		has_console = true;
	}
#endif

	if (!has_console)
	{
		BGCTRL_SUB[0] = BG_TILE_BASE(1) | BG_MAP_BASE(0) | BG_COLOR_16 | BG_32x32;

		decompress(bgTiles, (void *)CHAR_BASE_BLOCK_SUB(1), LZ77Vram);
		decompress(bgMap, (void *)SCREEN_BASE_BLOCK_SUB(0), LZ77Vram);
		dmaCopy(bgPal, BG_PALETTE_SUB, bgPalLen);

		ControlPane_InitKeyboard(state);

		FDC_SetLEDsChangeFunc(draw_floppy_leds);
		ControlPane_Redraw();
	}
}

void ControlPane_Error(bool fatal,const char *fmt,...)
{
	/* TODO: Allow continuing emulation when !fatal */
	va_list args;
	va_start(args,fmt);

	if (!has_console)
		consoleDemoInit();

	/* Log it */
	vprintf(fmt,args);

	while(1) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START)
			break;
	}

	/* Quit */
	exit(EXIT_FAILURE);
}

/*----------------------------------------------------------------------------*/

static void ControlPane_UpdateKeyboardLEDs(uint8_t leds);

static void ControlPane_InitKeyboard(ARMul_State *state)
{
	static const u16 keysPal[] = {
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
	int i;

	/* We manage sprite VRAM manually here instead of using oamAllocateGfx() */
	decompress(keysTiles, SPRITE_GFX_SUB, LZ77Vram);
	dmaCopy(keysPal, SPRITE_PALETTE_SUB, sizeof(keysPal));

	KBD.leds_changed = ControlPane_UpdateKeyboardLEDs;

	has_keyboard = true;
}

static void ControlPane_UpdateKeyboardLEDs(uint8_t leds)
{
	keys_caps = (leds & KBD_LED_CAPSLOCK);
}

static bool ControlPane_ClickKeyboard(ARMul_State *state, int px, int py)
{
	const dvk_to_vkeybd *ktvk;
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
	if (key_pressed != -1) {
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
			oamSet(&oamSub, i++, x, y, 0, palette, SpriteSize_16x16, SpriteColorFormat_16Color,
			       &SPRITE_GFX_SUB[sprite++ * 64], -1, false, false, false, false, false);
			x += 16;
		} while (--width);
	}
}

/*----------------------------------------------------------------------------*/

static void ControlPane_ClickFloppy(ARMul_State *state, int drive)
{
	const char *err;
	char tmp[256];

	if (FDC_IsFloppyInserted(drive)) {
		err = FDC_EjectFloppy(drive);
		if (err == NULL)
			draw_floppy(drive, false);
		/* TODO: Report warning if this fails */
	} else {
		sprintf(tmp, "FloppyImage%d", drive);
		err = FDC_InsertFloppy(drive, tmp);
		if (err == NULL)
			draw_floppy(drive, true);
		/* TODO: Report warning if this fails */
	}
}

/*----------------------------------------------------------------------------*/

static void ControlPane_ClickTouchpad(ARMul_State *state, int px, int py)
{
	drag_mode = DRAG_MOUSE;
	old_px = px;
	old_py = py;
}

static void ControlPane_DragTouchpad(ARMul_State *state, int px, int py)
{
	int newMouseX, newMouseY, xdiff, ydiff;

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

	KBD.MouseXCount = xdiff & 127;
	KBD.MouseYCount = -ydiff & 127;
}

/*----------------------------------------------------------------------------*/

bool ControlPane_ProcessTouchPressed(ARMul_State *state, int px, int py)
{
	if (has_keyboard && ControlPane_ClickKeyboard(state, px, py))
		return true;

	if (py > 183 || has_console) {
		int drive = 3 - (px / 64);
		ControlPane_ClickFloppy(state, drive);
		return true;
	} else if (py < 78) {
		ControlPane_ClickTouchpad(state, px, py);
		return true;
	}

	return false;
}

bool ControlPane_ProcessTouchHeld(ARMul_State *state, int px, int py)
{
	if (drag_mode == DRAG_MOUSE) {
		ControlPane_DragTouchpad(state, px, py);
		return true;
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

	return false;
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
