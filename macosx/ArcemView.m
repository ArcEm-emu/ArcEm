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
 * Name   : ArcemView.m
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/

#import "ArcemView.h"
#import "ArcemEmulator.h"
#import "PreferenceController.h"
#include <ApplicationServices/ApplicationServices.h>
#include <pthread.h>
#include "win.h"
#include "KeyTable.h"


extern int rMouseX;
extern int rMouseY;
extern int rMouseHeight;

#define CURSOR_HEIGHT 32

@implementation ArcemView
@synthesize mouseLock=captureMouse;

/*------------------------------------------------------------------------------
 * initWithFrame - constructor
 */
- (id)initWithFrame:(NSRect)rect
{
    if (self = [super initWithFrame:rect])
    {
        CGColorSpaceRef cgColorspace = CGColorSpaceCreateDeviceRGB();

        // Create the screen bitmap and image
        screenBmp = [[NSMutableData alloc] initWithLength: MonitorSize * 4];
        screenImage = CGBitmapContextCreate([screenBmp mutableBytes], MonitorWidth, MonitorHeight, 8, MonitorWidth * 4, cgColorspace, kCGImageAlphaNoneSkipFirst);

        // Create the cursor bitmap and image
        cursorBmp = [[NSMutableData alloc] initWithLength: 32 * 32 * 4];
        cursorImage = CGBitmapContextCreate([cursorBmp mutableBytes], 32, 32, 8, 32 * 4, cgColorspace, kCGImageAlphaNoneSkipFirst);

        CGColorSpaceRelease (cgColorspace);

        captureMouse = FALSE;
        memset(keyState, false, sizeof(keyState));

        // Set the default display region
        dispFrame.origin.x = 0.0;
        dispFrame.origin.y = 0.0;
        dispFrame.size.width = MonitorWidth;
        dispFrame.size.height = MonitorHeight;

        nWidth = MonitorWidth;
        nHeight = MonitorHeight;
        nXScale = nYScale = 1;
        bAspect = bUpscale = YES;

        // Note: we can't geet prefs yet as the controller init hasn't finished yet
    }

    return self;
}


- (void)setEmulator: (ArcemEmulator *)emu
{
    emuThread = emu;
}


/*------------------------------------------------------------------------------
 * getScreenBytes:
 */
- (void *)getScreenBytes
{
    return [screenBmp mutableBytes];
}


/*------------------------------------------------------------------------------
 * getCursorBytes:
 */
- (void *)getCursorBytes
{
    return [cursorBmp mutableBytes];
}


/*------------------------------------------------------------------------------
 * setNeedsScaledDisplayInRect - should be called on the view rather than
 *                               setNeedsDisplayInRect when we need to conside
 *                               the scaling.
 */
- (void)setNeedsScaledDisplayInRect: (NSRect)rect
{
    rect.origin.x *= nXScale;
    rect.origin.y *= nYScale;
    rect.size.width *= nXScale;
    rect.size.height *= nYScale;

    [self setNeedsDisplayInRect: rect];
}


/*------------------------------------------------------------------------------
 * drawRect - This redraws the display
 */
- (void)drawRect:(NSRect)rect
{
    NSRect bounds = [self bounds];
    CGRect r;
    
    NSGraphicsContext *ctx = [NSGraphicsContext currentContext];
    CGContextRef cgc = (CGContextRef) [ctx graphicsPort];

    r.size.width = nXScale * MonitorWidth;
    r.size.height = nYScale * MonitorHeight;
    r.origin.x = 0;
    r.origin.y = -(r.size.height - bounds.size.height);

    if (screenImage)
    {
        CGContextFlush (screenImage);

        CGImageRef image = CGBitmapContextCreateImage (screenImage);
        CGContextDrawImage (cgc, r, image);
        CGImageRelease(image);
    }

    r.size.width = nXScale * 32;
    r.size.height = nYScale * 32;
    r.origin.x = (rMouseX * nXScale);
    r.origin.y = -(r.size.height - bounds.size.height) - (rMouseY * nYScale);

    if (cursorImage)
    {
        CGContextFlush(cursorImage);

        CGImageRef image = CGBitmapContextCreateImage(cursorImage);
        CGContextDrawImage (cgc, r, image);
        CGImageRelease(image);
    }

    CGContextFlush (cgc);
}


/*------------------------------------------------------------------------------
 * toggleMouseLock - activated by "Toggle Mouse Lock" menu option. When turning
 *                   on, we disconnect the mouse cursor from the mouse, move it
 *                   to a position where it will always be in the window
 *                   regardless of the window size (to ensure mouse clicks work),
 *                   activate mouse events, then hide the cursor. When turning
 *                   off we do the reverse :)
 */
- (void)toggleMouseLock
{
    self.mouseLock = !captureMouse;
}

- (void)setMouseLock:(BOOL)mouseLock
{
    if (mouseLock == captureMouse) {
        return;
    }
    captureMouse = mouseLock;
    
    if (captureMouse)
    {
        // Turning on mouse capture
        CGPoint cp;
        NSPoint temp;
        NSScreen *disp;
        NSDictionary *devInfo;
        NSValue *val;
        NSSize screen;
        NSRect bounds = [self bounds];

        [[self window] setAcceptsMouseMovedEvents: YES];
        CGAssociateMouseAndMouseCursorPosition(FALSE);
        [NSCursor hide];

        // Move the mouse into a safe position, so that a resize
        // will note put the cursor outside the screen (as a subsequent
        // mouse click will mess things up)

        // Work out the position we want to put the cursor in (the centre of the view)
        oldMouse.x = bounds.size.width / 2;
        oldMouse.y = bounds.size.height / 2;
        temp = [[self window] convertPointToScreen: oldMouse];

        // Make a note of the current cursor position so we can restore it
        oldMouse = [NSEvent mouseLocation];

        // Find out the screen size - I do this dynamically incase we moved displays
        disp = [NSScreen mainScreen];
        devInfo = [disp deviceDescription];
        val = [devInfo objectForKey: NSDeviceSize];
        screen = [val sizeValue];

        cp.x = temp.x;
        cp.y = screen.height - temp.y;
        CGWarpMouseCursorPosition(cp);
    }
    else
    {
        // Turn off mouse capture
        CGPoint cp;
        NSScreen *disp;
        NSDictionary *devInfo;
        NSValue *val;
        NSSize screen;

        [[self window] setAcceptsMouseMovedEvents: NO];

        // restore cursor position before the user gets to see the cursor

        // Find out the screen size - I do this dynamically incase we moved displays
        disp = [NSScreen mainScreen];
        devInfo = [disp deviceDescription];
        val = [devInfo objectForKey: NSDeviceSize];
        screen = [val sizeValue];

        cp.x = oldMouse.x;
        cp.y = screen.height - oldMouse.y;
        CGWarpMouseCursorPosition(cp);

        CGAssociateMouseAndMouseCursorPosition(TRUE);
        [NSCursor unhide];
    }    
}


/*------------------------------------------------------------------------------
 * removeMouseLock - used when the application loses focus
 */
- (void)removeMouseLock
{
    self.mouseLock = NO;
}


/*------------------------------------------------------------------------------
 * resizeToWidthToHeight - Called when the emulator changes screen size, and
 *                         adjusts the view to match
 */
- (void)resizeToWidth: (int)width
             toHeight: (int)height
{
    NSWindow* window = [self window];
    NSRect frame;

    frame = [window frame];

    nWidth = width;
    nHeight = height;

    /* Try and detect rectangular pixel modes */
    if(bAspect && (width >= height*2))
    {
        nXScale = 1;
        nYScale = 2;
    }
    else if(bAspect && (height >= width))
    {
        nXScale = 2;
        nYScale = 1;
    }
    /* Try and detect small screen resolutions */
    else if (bUpscale && (width < MinimumWidth))
    {
        nXScale = 2;
        nYScale = 2;
    }
    else
    {
        nXScale = 1;
        nYScale = 1;
    }

    // Set the window size
    frame.origin.x = 0.0;
    frame.origin.y = 0.0;
    frame.size.width = (CGFloat)(width * nXScale);
    frame.size.height = (CGFloat)(height * nYScale);
    
    // Resize the window
    [window setContentSize: frame.size];

    // Resize the view
    [self setFrame:frame];
    
    // Update the description as to what part of the view is visable
    dispFrame.size.width = width;
    dispFrame.size.height = height;
    dispFrame.origin.x = 0.0;

    // We need to shift the image as the screen is draw top right of the
    // bitmap, but cocoa will use the origin as bottom left
    dispFrame.origin.y = MonitorHeight - height;
}


/*------------------------------------------------------------------------------
 * 
 */
- (BOOL)acceptsFirstResponder
{
    return YES;
}



/*------------------------------------------------------------------------------
 *
 */
- (BOOL)resignsFirstResponder
{
    [self setNeedsDisplay: YES];
    return YES;
}


/*------------------------------------------------------------------------------
 *
 */
- (BOOL)becomeFirstResponder
{
    [self setNeedsDisplay: YES];
    return YES;
}


/*------------------------------------------------------------------------------
 * flagsChanged - this is called if a modifier key is pressed. Most keys follow
 *                the usual even for up and event for down, except for caps lock
 *                which is sticky.
 */
- (void)flagsChanged:(NSEvent *)theEvent
{
    int c = [theEvent keyCode];
    
    keyState[c] = !keyState[c];
    //NSLog(@"set %d to %d\n", c, keyState[c]);

    const mac_to_arch_key *ktak;
    for (ktak = mac_to_arch_key_map; ktak->sym >= 0; ktak++) {
      if (ktak->sym == c) {
        if (keyState[c])
          [emuThread keyDown:ktak->kid];
        else
          [emuThread keyUp:ktak->kid];

        // Need to toggle caps lock and number lock
        if ((c == kVK_CapsLock) || (c == 0x7f))
        {
            [emuThread keyUp:ktak->kid];
            keyState[c] = false;
        }
        return;
      }
    }

    //NSLog(@"flagsChanged: unknown key: keysym=%x\n", c);
}


/*------------------------------------------------------------------------------
 * keyDown - record a key down event.
 */
- (void)keyDown:(NSEvent *)theEvent
{
    int c = [theEvent keyCode];

    //NSLog(@"down char %d\n", c);

    const mac_to_arch_key *ktak;
    for (ktak = mac_to_arch_key_map; ktak->sym >= 0; ktak++) {
      if (ktak->sym == c) {
        [emuThread keyDown:ktak->kid];
        return;
      }
    }

    NSLog(@"keyDown: unknown key: keysym=%x\n", c);
}


/*------------------------------------------------------------------------------
 * keyUp - record a key release event.
 */
- (void)keyUp:(NSEvent *)theEvent
{
    int c = [theEvent keyCode];

    //NSLog(@"up char %d\n", c);

    const mac_to_arch_key *ktak;
    for (ktak = mac_to_arch_key_map; ktak->sym >= 0; ktak++) {
      if (ktak->sym == c) {
        [emuThread keyUp:ktak->kid];
        return;
      }
    }

    NSLog(@"keyUp: unknown key: keysym=%x\n", c);
}


/*------------------------------------------------------------------------------
 * mouseMoved - If this gets called then we are tracking mouse movement (else
 *              mouse movement events are not monitored due to expense).
 */
- (void)mouseMoved:(NSEvent *)theEvent
{
    int xdiff, ydiff;

    CGGetLastMouseDelta(&xdiff, &ydiff);

    [emuThread mouseMovedX:xdiff
                         Y:ydiff];
}


/*------------------------------------------------------------------------------
 * mouseDown - If we're in mouse capture mode, then send a key down event,
 * basing the button on what modifiers are pressed at the time
 */
- (void)mouseDown: (NSEvent *)theEvent
{
    int button;
    
    // Whoa! Only bother then we're in capture mode
    if (!captureMouse)
        return;

    // Work out which mouse button it should be
    if (mouseEmulation)
    {
        if (keyState[adjustModifier])
            button = ARCH_KEY_button_3;
        else if (keyState[menuModifier])
            button = ARCH_KEY_button_2;
        else
            button = ARCH_KEY_button_1;
    }
    else
    {
        button = ARCH_KEY_button_1;
    }

    // Record the button press in the mouse event queue
    [emuThread keyDown:button];

    // Note what went down for when it comes up, as we can't rely on
    // the l^Huser to still be holding the modifier key
    nMouse = button;
}


/*------------------------------------------------------------------------------
 * mouseUp - Inverse of mouseDown! 
 */
- (void)mouseUp: (NSEvent *)theEvent
{
    // Only note stuff if we're in capture mode
    if (!captureMouse)
        return;

    // Record the up event
    [emuThread keyUp:nMouse];

    //NSLog(@"nMouse up = %d\n", nButton);
}


/*------------------------------------------------------------------------------
 * mouseDragged - essentially the same as a mouse move in ArcEm's eyes
 */
- (void)mouseDragged: (NSEvent *)theEvent
{
    if (!captureMouse)
        return;

    [self mouseMoved: theEvent];
}


/*------------------------------------------------------------------------------
 * rightMouseDown - Should be adjust, if we're not using mouse emulation 
 */
- (void)rightMouseDown: (NSEvent *)theEvent
{
    NSLog(@"Right mouse button down\n");
    
    if (mouseEmulation)
        return;

    [emuThread keyDown:ARCH_KEY_button_3];
}


/*------------------------------------------------------------------------------
 * rightMouseUp - Inverse of rightMouseDown!
 */
- (void)rightMouseUp: (NSEvent *)theEvent
{
    if (mouseEmulation)
        return;

    [emuThread keyUp:ARCH_KEY_button_3];
}


/*------------------------------------------------------------------------------
 * rightMouseDragged - essentially the same as a mouse move in ArcEm's eyes
 */
- (void)rightMouseDragged: (NSEvent *)theEvent
{
    if (!captureMouse)
        return;

    [self mouseMoved: theEvent];
}


/*------------------------------------------------------------------------------
 * otherMouseDown - Should be menu, if we're not using mouse emulation
 */
- (void)otherMouseDown: (NSEvent *)theEvent
{
    NSLog(@"Other mouse down\n");
    
    if (mouseEmulation)
        return;

    [emuThread keyDown:ARCH_KEY_button_2];
}


/*------------------------------------------------------------------------------
 * otherMouseUp - Inverse of rightMouseDown!
 */
- (void)otherMouseUp: (NSEvent *)theEvent
{
    if (mouseEmulation)
        return;

    [emuThread keyUp:ARCH_KEY_button_2];
}


/*------------------------------------------------------------------------------
 * otherMouseDragged - essentially the same as a mouse move in ArcEm's eyes
 */
- (void)otherMouseDragged: (NSEvent *)theEvent
{
    if (!captureMouse)
        return;

    [self mouseMoved: theEvent];
}


/*------------------------------------------------------------------------------
 *
 */
- (void)toggleAspect
{
    if (bAspect == NO)
    {
        bAspect = YES;
    }
    else
    {
        bAspect = NO;
    }

    // Force a resize
    [self resizeToWidth: nWidth
               toHeight: nHeight];
}


/*------------------------------------------------------------------------------
 *
 */
- (void)toggleUpscale
{
    if (bUpscale == NO)
    {
        bUpscale = YES;
    }
    else
    {
        bUpscale = NO;
    }

    // Force a resize
    [self resizeToWidth: nWidth
               toHeight: nHeight];
}


/*------------------------------------------------------------------------------
 * prefsUpdated - Called by the preference panel when the user updates
 *                something we should know about. Also called by the view
 *                constructor.
 */
- (void)prefsUpdated
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    
    // Get some of the prefs
    mouseEmulation = [defaults boolForKey: AEUseMouseEmulationKey];
    adjustModifier = (int)[defaults integerForKey: AEAdjustModifierKey];
    menuModifier = (int)[defaults integerForKey: AEMenuModifierKey];
}


/*------------------------------------------------------------------------------
 * destructor -
 */
- (void)dealloc
{
    if (screenImage)
        CGContextRelease(screenImage);
    if (cursorImage)
        CGContextRelease(cursorImage);
}

@end
