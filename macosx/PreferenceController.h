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
 * Name   : PreferenceController.h
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/


#import <AppKit/AppKit.h>
#import "ArcemView.h"

extern NSString *const AEUseMouseEmulationKey;
extern NSString *const AEAdjustModifierKey;
extern NSString *const AEMenuModifierKey;
extern NSString *const AEDirectoryKey;

@interface PreferenceController : NSWindowController {
    IBOutlet NSButton *useMouseEmulation;
    IBOutlet NSMatrix *adjustModifier;
    IBOutlet NSMatrix *menuModifier;
    IBOutlet NSTextField *directoryText;

    ArcemView *__weak view;
}

- (IBAction)changeMouseEmulation:(id)sender;
- (IBAction)changeAdjustModifier:(id)sender;
- (IBAction)changeMenuModifier:(id)sender;
- (IBAction)changeDirText:(id)sender;
- (IBAction)chooseButton:(id)sender;
@property (weak) ArcemView *view;

@end
