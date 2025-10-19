/*****************************************************************************
 * 
 * Copyright (C) 2002 Michael Dales
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 * Name   : ArcemEmulator.m
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/

//
//  ArcemEmulator.m
//  ArcEm
//
//  Created by Michael Dales on Wed May 15 2002.
//  Copyright (c) 2002 __MyCompanyName__. All rights reserved.
//

#import "ArcemEmulator.h"
#import "../armdefs.h"
#import "ArcemView.h"
#import "ArcemController.h"
#import "PreferenceController.h"
#include "../arch/keyboard.h"

#import <pthread.h>

extern ArcemConfig hArcemConfig;

extern uint32_t *screenbmp;
extern uint32_t *cursorbmp;
ArcemView* disp;
char arcemDir[256];

// This is where the imperative world meets the OO world. These functions could
// be in DispKey.c, but that would need more headers.

/*------------------------------------------------------------------------------
 * resizeWindow - called when the VIDC display sizes. Sometimes it's negative
 *                so we protect the view here.
 */
int resizeWindow(int hWidth, int hHeight)
{
    if ((hWidth > 0) && (hHeight > 0))
        [disp resizeToWidth: hWidth
                   toHeight: hHeight];
    [disp setNeedsDisplay: YES];
    return 0;
}


/*------------------------------------------------------------------------------
 * updateDisplay - called when ArcEm wants to refresh the display. If yield
 *                 is non-zero then we yield so that we have a more responsive
 *                 interactive env (well, that's the theory anyway).
 */
void updateDisplay(int x, int y, int width, int height, int yield)
{
    NSRect rect;

    rect.origin.x = x;
    rect.origin.y = y;
    rect.size.width = width;
    rect.size.height = height;
    
    [disp setNeedsScaledDisplayInRect: rect];
    
    //if (yield)
      //  sched_yield();
}


@implementation ArcemEmulator {
    bool restart;
}

/*------------------------------------------------------------------------------
 * initWithView - constructor
 */
- (id)initWithView:(ArcemView *)view
{
    if (self = [super init])
    {
        arcemView = view;
        bActive = NO;
        bRestart = NO;

        [self restart];
    }
    return self;
}


/*------------------------------------------------------------------------------
 * threadStart - called when the emulator thread is started.
 */
- (void)threadStart:(id)anObject
{
    int exit_code = EXIT_FAILURE;
    NSURL *dir;

    disp = anObject;

    screenbmp = [disp getScreenBytes];
    cursorbmp = [disp getCursorBytes];

    dir = [[NSUserDefaults standardUserDefaults] URLForKey:AEDirectoryKey];
	strlcpy(arcemDir, dir.fileSystemRepresentation, sizeof(arcemDir));

    /* Initialise */
    state = ARMul_NewState(&hArcemConfig);

    bActive = TRUE;
    [disp setEmulator:self];

    while (state) {
        /* Execute */
        exit_code = ARMul_DoProg(state);

        /* Finalise */
        ARMul_FreeState(state);

        if (bRestart) {
            /* Reinitialise */
            state = ARMul_NewState(&hArcemConfig);
            bRestart = FALSE;
        } else {
            break;
        }
    }

    [disp setEmulator:nil];
    bActive = FALSE;

    // TODO: Keep the application open so that settings can still be changed
    exit(exit_code);

    [NSThread exit];

    return;
}

/*------------------------------------------------------------------------------
 * restart
 */
- (void)restart
{
    if (bActive) {
        bRestart = TRUE;
        ARMul_Exit(state,0);
    } else {
        // Run the processing thread
        [NSThread detachNewThreadSelector: @selector(threadStart:)
                                 toTarget: self
                               withObject: (__bridge id)CFBridgingRetain(arcemView)];
    }
}

/*------------------------------------------------------------------------------
 * keyDown
 */
- (void)keyDown:(int)key
{
    keyboard_key_changed(&KBD, key, false);
}

/*------------------------------------------------------------------------------
 * keyUp
 */
- (void)keyUp:(int)key
{
    keyboard_key_changed(&KBD, key, true);
}

/*------------------------------------------------------------------------------
 * mouseMoved
 */
- (void)mouseMovedX:(int)xdiff
                  Y:(int)ydiff
{
    if (xdiff > 63) xdiff=63;
    if (xdiff < -63) xdiff=-63;

    if (ydiff>63) ydiff=63;
    if (ydiff<-63) ydiff=-63;

    KBD.MouseXCount =  xdiff & 127;
    KBD.MouseYCount = -ydiff & 127;
}

@end
