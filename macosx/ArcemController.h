
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
@class ArcemView;

@interface ArcemController : NSObject
{
    IBOutlet ArcemView *arcemView;		//!< Main screen display
    IBOutlet NSMenuItem *menuItemEject0;
    IBOutlet NSMenuItem *menuItemEject1;
    IBOutlet NSMenuItem *menuItemEject2;
    IBOutlet NSMenuItem *menuItemEject3;
    IBOutlet NSMenuItem *menuItemMount0;
    IBOutlet NSMenuItem *menuItemMount1;
    IBOutlet NSMenuItem *menuItemMount2;
    IBOutlet NSMenuItem *menuItemMount3;
    IBOutlet NSMenuItem *menuItemAspect;
    IBOutlet NSMenuItem *menuItemUpscale;
    NSMenuItem *menuItemsEject[4];
    NSMenuItem *menuItemsMount[4];
    
    NSMutableData *screenBmp;		//!< Raw screen bitmap
    CGContextRef screenImg;     	//!< Image for drawing screen
    NSMutableData *cursorBmp;		//!< Raw cursos bitmap
    CGContextRef cursorImg;     	//!< Image for drawing cursor

    ArcemEmulator *emuThread;		//!< Thread controling the emulator

    PreferenceController *preferenceController;

    BOOL	bFullScreen;		//!< Are we running in full screen mode?
}
- (IBAction)showPreferencePanel:(id)sender;
- (IBAction)newSim:(id)sender;
- (IBAction)fullScreen:(id)sender;
- (IBAction)lockMouse:(id)sender;

//! give the user a file select dialog box to let the user
//! mount a drive image. We don't want to let the user open a disk
//! with a disk already loaded there, but we don't test here as
//! the menu option should be disabled.
- (IBAction)menuMount0:(id)sender;
//! give the user a file select dialog box to let the user
//! mount a drive image. We don't want to let the user open a disk
//! with a disk already loaded there, but we don't test here as
//! the menu option should be disabled.
- (IBAction)menuMount1:(id)sender;
//! give the user a file select dialog box to let the user
//! mount a drive image. We don't want to let the user open a disk
//! with a disk already loaded there, but we don't test here as
//! the menu option should be disabled.
- (IBAction)menuMount2:(id)sender;
//! give the user a file select dialog box to let the user
//! mount a drive image. We don't want to let the user open a disk
//! with a disk already loaded there, but we don't test here as
//! the menu option should be disabled.
- (IBAction)menuMount3:(id)sender;

- (IBAction)menuEject0:(id)sender;
- (IBAction)menuEject1:(id)sender;
- (IBAction)menuEject2:(id)sender;
- (IBAction)menuEject3:(id)sender;

- (IBAction)menuAspect:(id)sender;
- (IBAction)menuUpscale:(id)sender;

- (IBAction)menuReset:(id)sender;

- (void)applicationHide:(NSNotification*)aNotification;
- (void)destroyEmulatorThread;

- (void)changeDriveImageAtIndex: (int)fdNum toURL: (NSURL*)newfile;

@end
