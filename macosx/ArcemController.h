
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
 * Name   : ArcemController.h
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/

/* ArcemController */

#import <Cocoa/Cocoa.h>
#import "ArcemEmulator.h"

@class PreferenceController;

@interface ArcemController : NSObject
{
    IBOutlet id arcemView;		// Main screen display
    IBOutlet id menuItemEject0;
    IBOutlet id menuItemEject1;
    IBOutlet id menuItemEject2;
    IBOutlet id menuItemEject3;
    IBOutlet id menuItemMount0;
    IBOutlet id menuItemMount1;
    IBOutlet id menuItemMount2;
    IBOutlet id menuItemMount3;
    IBOutlet id menuItemHDEject0;
    IBOutlet id menuItemHDEject1;
    IBOutlet id menuItemHDMount0;
    IBOutlet id menuItemHDMount1;
    IBOutlet id menuItemDoubleX;
    IBOutlet id menuItemDoubleY;
    IBOutlet id menuItemsEject[4];
    IBOutlet id menuItemsMount[4];
    IBOutlet id menuItemsHDEject[2];
    IBOutlet id menuItemsHDMount[2];
    
    NSMutableData *screenBmp;		// Raw screen bitmap
    NSBitmapImageRep *screenImg;	// Image for drawing screen
    unsigned char **screenPlanes;	// Planes for screen
    NSMutableData *cursorBmp;		// Raw cursos bitmap
    NSBitmapImageRep *cursorImg;	// Image for drawing cursor
    unsigned char **cursorPlanes;	// Planes for cursor

    ArcemEmulator *emuThread;		// Thread controling the emulator

    PreferenceController *preferenceController;

    BOOL	bFullScreen;		// Are we running in full screen mode?

    int		mountDrive;
}
- (IBAction)showPreferencePanel:(id)sender;
- (IBAction)newSim:(id)sender;
- (IBAction)fullScreen:(id)sender;
- (IBAction)lockMouse:(id)sender;

- (IBAction)menuMount0:(id)sender;
- (IBAction)menuMount1:(id)sender;
- (IBAction)menuMount2:(id)sender;
- (IBAction)menuMount3:(id)sender;

- (IBAction)menuEject0:(id)sender;
- (IBAction)menuEject1:(id)sender;
- (IBAction)menuEject2:(id)sender;
- (IBAction)menuEject3:(id)sender;

- (IBAction)menuHDMount0:(id)sender;
- (IBAction)menuHDMount1:(id)sender;

- (IBAction)menuHDEject0:(id)sender;
- (IBAction)menuHDEject1:(id)sender;

- (IBAction)menuDoubleX:(id)sender;
- (IBAction)menuDoubleY:(id)sender;

- (IBAction)menuReset:(id)sender;

- (void)applicationHide:(NSNotification*)aNotification;
- (void)destroyEmulatorThread;

- (void)openPanelDidEnd: (NSOpenPanel *)openPanel
             returnCode: (int)returnCode
            contextInfo: (void *)x;
- (void)openPanelHardDiscDidEnd: (NSOpenPanel *)openPanel
                     returnCode: (int)returnCode
                    contextInfo: (void *)x;

@end
