/* Emulation of 1772 fdc - (C) David Alan Gilbert 1995 */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef FDC1772
#define FDC1772

#include "../armdefs.h"

/**
 * FDC_Init
 *
 * Called on program startup, initialise the 1772 disk controller
 *
 * @param state Emulator state IMPROVE unused
 */
void FDC_Init(ARMul_State *state);

/**
 * FDC_Read
 *
 * Read from the 1772 FDC controller's registers
 *
 * @param state  Emulator State
 * @param offset Containing the FDC register
 * @returns      Value of register, or 0 on register not handled
 */
ARMword FDC_Read(ARMul_State *state, ARMword offset);

/**
 * FDC_Write
 *
 * Write to the 1772 FDC controller's registers
 *
 * @param state  Emulator State
 * @param offset Containing the FDC register
 * @param data   Data field to write
 * @param bNw    byteNotWord IMPROVE unused parameter
 * @returns      IMRPOVE always 0
 */
ARMword FDC_Write(ARMul_State *state, ARMword offset, ARMword data, bool bNw);

/**
 * FDC_LatchAChange
 *
 * Callback function, whenever LatchA is written too (regardless of
 * whether the value has changed).
 *
 * @param state Emulator state
 */
void FDC_LatchAChange(ARMul_State *state);

/**
 * FDC_LatchBChange
 *
 * Callback function, whenever LatchA is written too (regardless of
 * whether the value has changed).
 *
 * @param state Emulator state
 */
void FDC_LatchBChange(ARMul_State *state);

/**
 * FDC_ReOpen
 *
 * Deprecated.  See FDC_InsertFloppy() and FDC_EjectFloppy().
 * IMPROVE remove the usage of this by the Mac OS X code
 *
 * @param state
 * @param drive
 */
void FDC_ReOpen(ARMul_State *state, int drive);

/**
 * FDC_InsertFloppy
 *
 * Associate disc image with drive.Drive must be empty
 * on startup or having been previously ejected.
 *
 * @oaram drive Drive number to load image into [0-3]
 * @param image Filename of image to load
 * @returns NULL on success or string of error message
 */
const char *FDC_InsertFloppy(int drive, const char *image);

/**
 * FDC_EjectFloppy
 *
 * Close and forget about the disc image associated with drive.  Disc
 * must be inserted.
 *
 * @param drive Drive number to unload image [0-3]
 * @returns NULL on success or string of error message
 */
const char *FDC_EjectFloppy(int drive);

/**
 * FDC_Regular
 *
 * Called regulaly from the DispKbd poll loop.
 *
 * @param state State of the emulator
 */
unsigned FDC_Regular(ARMul_State *state);

/**
 * FDC_SetLEDsChangeFunc
 *
 * If your platform wants to it can assign a function
 * to be called back each time the Floppy Drive LEDs
 * change.
 * Each time they change the callback function will recieve
 * the state of the LEDs in the 'leds' parameter. See
 * X/ControlPane.c  draw_floppy_leds() for an example of
 * how to process the parameter.
 *
 * @param leds_changed Function to callback on LED changes
 */
void FDC_SetLEDsChangeFunc(void (*leds_changed)(unsigned int leds));

#endif
