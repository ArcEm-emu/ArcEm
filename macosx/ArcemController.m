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
 * Name   : ArcemController.m
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/

#import "ArcemController.h"
#import "PreferenceController.h"
#import "ArcemView.h"
#include "ArcemConfig.h"
#include "win.h"

#import "arch/armarc.h"
#import "arch/fdc1772.h"

#include <Carbon/Carbon.h>

extern ArcemConfig hArcemConfig;
ArcemConfig hArcemConfig;

@implementation ArcemController

/*------------------------------------------------------------------------------
 * Constructor - initialises the images for use with the display
 */
- init
{
    if (self = [super init])
    {
        ArcemConfig_Result result = Result_Continue;

        if (result == Result_Continue)
            result = ArcemConfig_SetupDefaults(&hArcemConfig);

        /* Parse the config file to overrule the defaults */
        if (result == Result_Continue)
            result = ArcemConfig_ParseConfigFile(&hArcemConfig);

        if (result != Result_Continue) {
            ArcemConfig_Free(&hArcemConfig);
            exit(result == Result_Success ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        NSMutableDictionary *defaultValues;
        NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:@"arcem"];
        
        CGColorSpaceRef cgColorspace = CGColorSpaceCreateDeviceRGB();

        // Create the screen bitmap and image
        screenBmp = [[NSMutableData alloc] initWithLength: MonitorSize * 4];
        screenImg = CGBitmapContextCreate([screenBmp mutableBytes], MonitorWidth, MonitorHeight, 8, MonitorWidth * 4, cgColorspace, kCGImageAlphaNoneSkipFirst);

        // Create the cursor bitmap and image
        cursorBmp = [[NSMutableData alloc] initWithLength: 32 * 32 * 4];
        cursorImg = CGBitmapContextCreate([cursorBmp mutableBytes], 32, 32, 8, 32 * 4, cgColorspace, kCGImageAlphaNoneSkipFirst);

        CGColorSpaceRelease (cgColorspace);

        bFullScreen = NO;
        preferenceController = nil;

        // Defaults
        defaultValues = [NSMutableDictionary dictionary];

        // ...
        [defaultValues setObject: @YES
                          forKey: AEUseMouseEmulationKey];
        [defaultValues setObject: @(kVK_Option)
                          forKey: AEMenuModifierKey];
        [defaultValues setObject: @(kVK_Command)
                          forKey: AEAdjustModifierKey];
        [defaultValues setObject: [NSURL fileURLWithPath:path]
                          forKey: AEDirectoryKey];

        [[NSUserDefaults standardUserDefaults] registerDefaults: defaultValues];
    }
    
    return self;
}


/*------------------------------------------------------------------------------
 * createEmulatorThread
 */
- (void)createEmulatorThread
{
    NSArray *params;

    // Create the thread for the enumlator to run in
    emuThread = [[ArcemEmulator alloc] init];

    // List the parameters for the thread - the bitmaps, and the view to send the redraw to
    params = @[arcemView, screenBmp, cursorBmp, self];

    // Run the processing thread
    [NSThread detachNewThreadSelector: @selector(threadStart:)
                             toTarget: emuThread
                           withObject: (__bridge id)CFBridgingRetain(params)];

    // Pass the images to the view
    [arcemView setBitmapsWithScreen: screenImg
                         withCursor: cursorImg];

}


/*------------------------------------------------------------------------------
 * destroyEmulatorThread
 */
- (void)destroyEmulatorThread
{
    if (emuThread != nil)
    {
        ArcemEmulator* temp = emuThread;
        emuThread = nil;
        [temp threadKill];
    }
}

/*------------------------------------------------------------------------------
 * awakeFromNib - called when the window has been created. Now is the time to
 *                set up display related stuff. I also use this as an excuse to
 *                create the emulator thread.
 */
- (void)awakeFromNib
{
    [super awakeFromNib];
    int i;

    // Create an initial emulator thread
    [self createEmulatorThread];

    // Bring that window to the front
    [[arcemView window] makeKeyAndOrderFront: self];
    [[arcemView window] makeFirstResponder: arcemView];

    // Set the menu options

    // Build menu item arrays
    menuItemsMount[0] = menuItemMount0;
    menuItemsMount[1] = menuItemMount1;
    menuItemsMount[2] = menuItemMount2;
    menuItemsMount[3] = menuItemMount3;
    menuItemsEject[0] = menuItemEject0;
    menuItemsEject[1] = menuItemEject1;
    menuItemsEject[2] = menuItemEject2;
    menuItemsEject[3] = menuItemEject3;

    // We'll manage the menu items from now on thank you very much
    [[menuItemMount0 menu] setAutoenablesItems: NO];
    [[menuItemEject0 menu] setAutoenablesItems: NO];
    
    for (i = 0; i < 4; i++)
        [menuItemsEject[i] setEnabled: NO];
    
    // Now set up to receive notification of when we lose control (either we're hidden
    // or the user cmd-tabbed away
    [[NSNotificationCenter defaultCenter] addObserver: self
                                             selector: @selector(applicationHide:)
                                                 name: NSApplicationWillResignActiveNotification
                                               object: NSApp];
}


/*------------------------------------------------------------------------------
 * fullScreen - TODO: Use the newer windowing APIs.
 */
- (IBAction)fullScreen:(id)sender
{
    // See if there's anything to be of interest in
    if (emuThread == nil)
        return;
    
    // What we do depends if we're in full screen mode or not
    if (!bFullScreen)
    {
        // We're running the emulator in a window, so kill it first
        //[[arcemView window] close];
        arcemView = nil;
    }
    else
    {
    }

    bFullScreen = !bFullScreen;
}


/*------------------------------------------------------------------------------
 * newSim - TODO
 */
- (IBAction)newSim:(id)sender
{
}


/*------------------------------------------------------------------------------
 * lockMouse - This is the receiver of the ToggleMouseLock menu item, and we
 *             just pass it on to the view.
 */
- (IBAction)lockMouse:(id)sender
{
    [arcemView toggleMouseLock];
}


/*------------------------------------------------------------------------------
 * applicationHide - called 
 */
- (void)applicationHide:(NSNotification*)aNotification
{
    [arcemView removeMouseLock];
}


/*------------------------------------------------------------------------------
 * showPreferencePanel - TODO
 */
- (IBAction)showPreferencePanel:(id)sender
{
    if (preferenceController == nil)
    {
        preferenceController = [[PreferenceController alloc] init];
        [preferenceController setView: arcemView];
    }
    [preferenceController showWindow: self];
}


/*------------------------------------------------------------------------------
 * menuMount0 - give the user a file select dialog box to let the user
 *              mount a drive image. We don't want to let the user open a disk
 *              with a disk already loaded there, but we don't test here as
 *              the menu option should be disabled.
 */
- (IBAction)menuMount0:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];

    [panel beginSheetModalForWindow: [arcemView window] completionHandler: ^(NSModalResponse result) {
        if (result == NSModalResponseOK)
        {
            [self changeDriveImageAtIndex:0 toURL:panel.URL];
        }
    }];
}


/*------------------------------------------------------------------------------
 * menuMount1 - give the user a file select dialog box to let the user
 *              mount a drive image. We don't want to let the user open a disk
 *              with a disk already loaded there, but we don't test here as
 *              the menu option should be disabled.
 */
- (IBAction)menuMount1:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];

    [panel beginSheetModalForWindow: [arcemView window] completionHandler: ^(NSModalResponse result) {
        if (result == NSModalResponseOK)
        {
            [self changeDriveImageAtIndex:1 toURL:panel.URL];
        }
    }];
}


/*------------------------------------------------------------------------------
 * menuMount2 - give the user a file select dialog box to let the user
 *              mount a drive image. We don't want to let the user open a disk
 *              with a disk already loaded there, but we don't test here as
 *              the menu option should be disabled.
 */
- (IBAction)menuMount2:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];

    [panel beginSheetModalForWindow: [arcemView window] completionHandler: ^(NSModalResponse result) {
        if (result == NSModalResponseOK)
        {
            [self changeDriveImageAtIndex:2 toURL:panel.URL];
        }
    }];
}


/*------------------------------------------------------------------------------
 * menuMount3 - give the user a file select dialog box to let the user
 *              mount a drive image. We don't want to let the user open a disk
 *              with a disk already loaded there, but we don't test here as
 *              the menu option should be disabled.
 */
- (IBAction)menuMount3:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];

    [panel beginSheetModalForWindow: [arcemView window] completionHandler: ^(NSModalResponse result) {
        if (result == NSModalResponseOK)
        {
            [self changeDriveImageAtIndex:3 toURL:panel.URL];
        }
    }];
}


/*------------------------------------------------------------------------------
 *
 */
- (void)changeDriveImageAtIndex: (int)fdNum toURL: (NSURL*)newfile
{
    // One assumes if we managed to select a file then it exists...
    
    // Force the FDC to reload that drive
    FDC_InsertFloppy(fdNum, [newfile fileSystemRepresentation]);
    
    // Now disable the insert menu option and enable the eject menu option
    [menuItemsMount[fdNum] setEnabled: NO];
    [menuItemsEject[fdNum] setEnabled: YES];
}


/*------------------------------------------------------------------------------
 * menuEject0 - called to remove a disk image
 */
- (IBAction)menuEject0:(id)sender
{
    // Update the sim
    FDC_EjectFloppy(0);
    
    // Now disable the insert menu option and enable the eject menu option
    [menuItemsMount[0] setEnabled: YES];
    [menuItemsEject[0] setEnabled: NO];
}


/*------------------------------------------------------------------------------
 * menuEject1 - called to remove a disk image
 */
- (IBAction)menuEject1:(id)sender
{
    // Update the sim
    FDC_EjectFloppy(1);

    // Now disable the insert menu option and enable the eject menu option
    [menuItemsMount[1] setEnabled: YES];
    [menuItemsEject[1] setEnabled: NO];
}



/*------------------------------------------------------------------------------
 * menuEject2 - called to remove a disk image
 */
- (IBAction)menuEject2:(id)sender
{
    // Update the sim
    FDC_EjectFloppy(2);

    // Now disable the insert menu option and enable the eject menu option
    [menuItemsMount[2] setEnabled: YES];
    [menuItemsEject[2] setEnabled: NO];
}



/*------------------------------------------------------------------------------
 * menuEject3 - called to remove a disk image
 */
- (IBAction)menuEject3:(id)sender
{
    // Update the sim
    FDC_EjectFloppy(3);

    // Now disable the insert menu option and enable the eject menu option
    [menuItemsMount[3] setEnabled: YES];
    [menuItemsEject[3] setEnabled: NO];
}


/*------------------------------------------------------------------------------
 * 
 */
- (IBAction)menuAspect:(id)sender
{
    [arcemView toggleAspect];

    if ([sender state] == NSOnState)
        [sender setState: NSOffState];
    else
        [sender setState: NSOnState];
}


/*------------------------------------------------------------------------------
 *
 */
- (IBAction)menuUpscale:(id)sender
{
    [arcemView toggleUpscale];

    if ([sender state] == NSOnState)
        [sender setState: NSOffState];
    else
        [sender setState: NSOnState];
}


/*------------------------------------------------------------------------------
 * menuReset - kill the current emulator thread and restart it.
 */
- (IBAction)menuReset:(id)sender
{
    [self destroyEmulatorThread];
    [self createEmulatorThread];
}


/*------------------------------------------------------------------------------
 * destructor - 
 */
- (void)dealloc
{
    if (screenImg)
        CGContextRelease(screenImg);
    if (cursorImg)
        CGContextRelease(cursorImg);
}

@end
