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
#import "dagstandalone.h"
#import "ArcemView.h"
#import "ArcemController.h"
#import "PreferenceController.h"

#import <pthread.h>

extern unsigned char *screenbmp;
extern unsigned char *cursorbmp;
ArcemView* disp;
ArcemController* controller;
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


/*------------------------------------------------------------------------------
 *
 */
void arcem_exit(char* msg)
{
    [disp emulatorError: msg];
    
    //[pool release];
    //[NSThread exit];
    [controller destroyEmulatorThread];

    return;
}


@implementation ArcemEmulator


/*------------------------------------------------------------------------------
 * threadStart - called when the emulator thread is started.
 */
- (void)threadStart:(id)anObject
{
    NSArray *params = anObject;
    NSMutableData *screen, *cursor;
    NSString *dir;
    
    pool = [[NSAutoreleasePool alloc] init];

    disp = [params objectAtIndex: 0];
    screen = [params objectAtIndex: 1];
    cursor = [params objectAtIndex: 2];
    controller = [params objectAtIndex: 3];

    screenbmp = [screen mutableBytes];
    cursorbmp = [cursor mutableBytes];

    dir = [[NSUserDefaults standardUserDefaults] stringForKey:AEDirectoryKey];
    [dir getCString: arcemDir
          maxLength: 256];
    
    // Start ArcEm
    dagstandalone();

    [pool release];
    [NSThread exit];

    return;
}

/*------------------------------------------------------------------------------
 * threadKill
 */
- (void)threadKill
{
    [pool release];
    [NSThread exit];
}

@end
