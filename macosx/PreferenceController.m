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
 * Name   : PreferenceController.m
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/


#import "PreferenceController.h"
#import "KeyTable.h"

// The keys we use for our plist
NSString *AEUseMouseEmulationKey = @"Use Mouse Emulation";
NSString *AEAdjustModifierKey = @"Adjust Modifier";
NSString *AEMenuModifierKey = @"Menu Modifier";
NSString *AEDirectoryKey = @"Directory";

@implementation PreferenceController

const static int modifier_table[5] = {VK_ALT, VK_COMMAND, VK_CONTROL, VK_FUNCTION, VK_LSHIFT};

/*------------------------------------------------------------------------------
 *
 */
- (id)init
{
    self = [super initWithWindowNibName:@"Preferences"];
    return self;
}


/*------------------------------------------------------------------------------
 * windowDidLoad - once the nib file has loaded our window read the defaults.
 */
- (void)windowDidLoad
{
    NSUserDefaults *defaults;

    defaults = [NSUserDefaults standardUserDefaults];

    [useMouseEmulation setState: [defaults boolForKey:AEUseMouseEmulationKey]];
    [directoryText setStringValue: [defaults stringForKey:AEDirectoryKey]];

    [defaults synchronize];
}


/*------------------------------------------------------------------------------
 *
 */
- (void)setView: (ArcemView *) aview
{
    view = aview;
}


/*------------------------------------------------------------------------------
 *
 */
- (IBAction)changeMouseEmulation:(id)sender
{
    [[NSUserDefaults standardUserDefaults] setBool: [sender state]
                                            forKey: AEUseMouseEmulationKey];

    [view prefsUpdated];
}


/*------------------------------------------------------------------------------
 *
 */
- (IBAction)changeMenuModifier:(id)sender
{
    id cell;
    int mod;

    // find what's currently selected
    cell = [menuModifier selectedCell];

    // Set set the cell's tag to match up with entries in out modifier_table
    mod = modifier_table[[cell tag]];

    [[NSUserDefaults standardUserDefaults] setInteger: mod
                                               forKey: AEMenuModifierKey];

    [view prefsUpdated];
}


/*------------------------------------------------------------------------------
 *
 */
- (IBAction)changeAdjustModifier:(id)sender
{
    id cell;
    int mod;

    // find what's currently selected
    cell = [menuModifier selectedCell];

    // Set set the cell's tag to match up with entries in out modifier_table
    mod = modifier_table[[cell tag]];

    [[NSUserDefaults standardUserDefaults] setInteger: mod
                                               forKey: AEMenuModifierKey];

    [view prefsUpdated];
}


/*------------------------------------------------------------------------------
 *
 */
- (IBAction)changeDirText:(id)sender
{
}


/*------------------------------------------------------------------------------
 *
 */
- (void)openPanelDidEnd: (NSOpenPanel *)openPanel
             returnCode: (int)returnCode
            contextInfo: (void *)x
{
    if (returnCode == NSOKButton)
    {
        NSString *path = [openPanel filename];

        [[NSUserDefaults standardUserDefaults] setObject: path
                                                  forKey: AEMenuModifierKey];        
        
        [directoryText setStringValue: path];
    }
}


/*------------------------------------------------------------------------------
 *
 */
- (IBAction)chooseButton:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];

    [panel setCanChooseDirectories: TRUE];
    [panel setCanChooseFiles: FALSE];
    
    [panel beginSheetForDirectory: nil
                             file: [[NSUserDefaults standardUserDefaults] stringForKey:AEDirectoryKey]
                            types: nil
                   modalForWindow: [self window]
                    modalDelegate: self
                   didEndSelector: @selector(openPanelDidEnd:returnCode:contextInfo:)
                      contextInfo: nil];
}

@end
