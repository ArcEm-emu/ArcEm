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
#import "macarcem.h"
#import "PreferenceController.h"
#include <ApplicationServices/ApplicationServices.h>
#import <pthread.h>


// Welcome to the wonderful world of object orientated programming, but alas
// we're merging lots of imperative C code from ArcEm, so here are a bunch
// of global variables that are used to communicate events from the interface
// back to the emulator.

// I'm using a buffer for keyboard and mouse events, and I'm hoping that I
// can avoid using a semaphore for this the golder rule is that nVirtKey
// should be the last thing to be modified, from -1 to the value, which
// acts like the lock.

// Buffer for keyboard events
#define KB_BUFFER_SIZE 	20
#define KEY_UP		1
#define KEY_DOWN	0
int nVirtKey[KB_BUFFER_SIZE];
int nKeyStat[KB_BUFFER_SIZE];
int keyF;
int nKBRead;
int nKBWrite;

// Macro used to add key events to the buffer. Just in a macro to make it
// easier to ensure the list integrity
#define KEY_EVENT(virt, stat) {\
    int oldpos = nKBWrite;\
    if (((nKBWrite + 1) % KB_BUFFER_SIZE) != nKBRead) {\
        keyF++;\
        nKeyStat[nKBWrite] = stat;\
        nKBWrite = ((nKBWrite + 1) % KB_BUFFER_SIZE);\
        nVirtKey[oldpos] = virt;\
    }\
}\

int nMouseX;
int nMouseY;
int mouseMF = 0;

int nButton[KB_BUFFER_SIZE];
int buttF;
int nMouseRead;
int nMouseWrite;

// Macro used to add mouse events to the buffer. Just in a macro to make it
// easier to ensure the list integrity
#define MOUSE_EVENT(button) {\
    int oldpos = nMouseWrite;\
        if (((nMouseWrite + 1) % KB_BUFFER_SIZE) != nMouseRead) {\
            buttF++;\
            nMouseWrite = ((nMouseWrite + 1) % KB_BUFFER_SIZE);\
            nButton[oldpos] = button;\
        }\
}\


extern int rMouseX;
extern int rMouseY;
extern int rMouseHeight;

#define MAX_SCREEN_HEIGHT 600
#define CURSOR_HEIGHT 32

@implementation ArcemView


/*------------------------------------------------------------------------------
 * initWithFrame - constructor
 */
- (id)initWithFrame:(NSRect)rect
{
    if (self = [super initWithFrame:rect])
    {
        int i;
        
        captureMouse = FALSE;
        memset(keyState, 0, 256);

        // Init keyboard and mouse buffer
        for (i = 0; i < KB_BUFFER_SIZE; i++)
        {
            nVirtKey[i] = -1;
            nKeyStat[i] = -1;
            nButton[i] = -1;
        }
        keyF = 0;
        nKBRead = 0;
        nKBWrite = 0;
        nMouseRead = 0;
        nMouseWrite = 0;
        buttF = 0;
        
        // Set the default display region
        dispFrame.origin.x = 0.0;
        dispFrame.origin.y = 0.0;
        dispFrame.size.width = 800.0;
        dispFrame.size.height = 600.0;

        nXScale = nYScale = 1;
        fXScale = fYScale = 1.0;
        bXScale = bYScale = NO;

        strErrorMsg = nil;

        // Note: we can't geet prefs yet as the controller init hasn't finished yet
    }

    return self;
}


/*------------------------------------------------------------------------------
 * setBitmapsWithScreen: withCursor - pass the bitmaps from the controller.
 */
- (void)setBitmapsWithScreen: (NSBitmapImageRep *)si
                  withCursor: (NSBitmapImageRep *)ci
{
    NSSize size1, size2;

    size1.width = SCREEN_WIDTH;
    size1.height = SCREEN_HEIGHT;
    screenImage = [[NSImage alloc] initWithSize: size1];

    size2.width = 32.0;
    size2.height = CURSOR_HEIGHT;
    cursorImage = [[NSImage alloc] initWithSize: size2];

    [screenImage addRepresentation: si];
    [cursorImage addRepresentation: ci];

    [self setNeedsDisplay: YES];

    // This should be otherwhere, but I'm not sure where yet. All I know is that initWithRect is too early
    recttag = [self addTrackingRect: [self bounds]
                              owner: self
                           userData: NULL
                       assumeInside: YES];


    [self prefsUpdated];
}


/*------------------------------------------------------------------------------
 * setNeedsScaledDisplayInRect - should be called on the view rather than
 *                               setNeedsDisplayInRect when we need to conside
 *                               the scaling.
 */
- (void)setNeedsScaledDisplayInRect: (NSRect)rect
{
    rect.origin.x *= fXScale;
    rect.origin.y *= fYScale;
    rect.size.width *= fXScale;
    rect.size.height *= fYScale;

    [self setNeedsDisplayInRect: rect];
}


/*------------------------------------------------------------------------------
 * drawRect - This redraws the display
 */
- (void)drawRect:(NSRect)rect
{
    NSRect bounds = [self bounds];
    NSRect r;
    
    if (screenImage)
    {
        [screenImage drawInRect: bounds
                       fromRect: dispFrame
                      operation: NSCompositeCopy
                       fraction: 1.0];
    }

    r.size.width = fXScale * 32.0;
    r.size.height = (float)(nYScale * rMouseHeight);
    r.origin.x = bounds.origin.x + (rMouseX * nXScale) - 1;
    r.origin.y = bounds.origin.y + (bounds.size.height - ((rMouseY + rMouseHeight) * nYScale)) + 1;
    
    bounds.size.width = 32.0;
    bounds.size.height = (float)rMouseHeight;
    bounds.origin.x = 0.0;
    bounds.origin.y = (float)(CURSOR_HEIGHT - rMouseHeight);
    
    if (cursorImage)
    {
        [cursorImage drawInRect: r
                       fromRect: bounds
                      operation: NSCompositeSourceOver
                       fraction: 1.0];
    }

    if (strErrorMsg != NULL)
    {
        // Welcome to stupid race condition central. For some reason drawRect will
        // get called a second time whilst blocked on the alert panel, so I need to
        // set my test condition to handle this.
        const char* temp = strErrorMsg;
        strErrorMsg = NULL;
        NSRunCriticalAlertPanel(@"ArcEm Critical Error", @"%s", nil, nil, nil, temp);
    }
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
    captureMouse = !captureMouse;

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

        // Work out the position we want to put the cursor in (the top left of the view)
        oldMouse.x = 0.0;
        oldMouse.y = bounds.size.height;
        temp = [[self window] convertBaseToScreen: oldMouse];

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
    if (captureMouse)
        [self toggleMouseLock];
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
    
    // I prefer it if the top left corner of the window doesn't move, and the
    // cocoa coord system puts the origin at bottom left, so we need to adjust that
    frame.origin.y += frame.size.height - (height + 22);
    
    // Set the window size
    frame.size.width = (float)(width * nXScale);
    frame.size.height = (float)((height * nYScale) + 22); // bad use of constant :)
    
    // Resize the window
    [window setFrame: frame
             display: YES];

    frame.size.height -= 22.0;
    frame.origin.y = 0.0;
    frame.origin.x = 0.0;
    
    // Resize the view
    [self setFrame:frame];
    
    // Update the description as to what part of the view is visable
    dispFrame.size.width = width;
    dispFrame.size.height = height;
    dispFrame.origin.x = 0.0;

    // We need to shift the image as the screen is draw top right of the
    // bitmap, but cocoa will use the origin as bottom left
    dispFrame.origin.y = MAX_SCREEN_HEIGHT - height;
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
    
    keyState[c] = (keyState[c] == 0) ? 1 : 0;
    //NSLog(@"set %d to %d\n", c, keyState[c]);

    KEY_EVENT(c, keyState[c] == 1 ? 0 : 1);

    // Need to toggle caps lock and number lock
    if ((c == 57) || (c == 127))
    {
        KEY_EVENT(c, KEY_UP);
        keyState[c] = 0;
    }
}


/*------------------------------------------------------------------------------
 * keyDown - record a key down event.
 */
- (void)keyDown:(NSEvent *)theEvent
{
    //NSLog(@"down char %d\n", [theEvent keyCode]);
    
    KEY_EVENT([theEvent keyCode], KEY_DOWN);
}


/*------------------------------------------------------------------------------
 * keyUp - record a key release event.
 */
- (void)keyUp:(NSEvent *)theEvent
{
    //NSLog(@"up char %d\n", [theEvent keyCode]);
    
    KEY_EVENT([theEvent keyCode], KEY_UP);
}


/*------------------------------------------------------------------------------
 * mouseMoved - If this gets called then we are tracking mouse movement (else
 *              mouse movement events are not monitored due to expense).
 */
- (void)mouseMoved:(NSEvent *)theEvent
{
    CGMouseDelta x, y;

    if (mouseMF == 0)
    {
        CGGetLastMouseDelta(&x, &y);

        nMouseX = x;
        nMouseY = -y;

        mouseMF = 1;
    }
    else
    {
        CGGetLastMouseDelta(&x, &y);

        nMouseX += x;
        nMouseY -= y;
    }
    //[NSThread sleepUntilDate: [[NSDate date] addTimeInterval: 0.00000001]];
    //sched_yield();
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
            button = 0x02;
        else if (keyState[menuModifier])
            button = 0x01;
        else
            button = 0x00;
    }
    else
    {
        button = 0x00;
    }

    // Record the button press in the mouse event queue
    MOUSE_EVENT(button);

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
    MOUSE_EVENT(nMouse | 0x80);

    //NSLog(@"nMouse up = %d\n", nButton);
}


/*------------------------------------------------------------------------------
 * rightMouseDown - Should be adjust, if we're not using mouse emulation 
 */
- (void)rightMouseDown: (NSEvent *)theEvent
{
    NSLog(@"Right mouse button down\n");
    
    if (mouseEmulation)
        return;

    MOUSE_EVENT(0x2);
}


/*------------------------------------------------------------------------------
 * rightMouseUp - Inverse of rightMouseDown!
 */
- (void)rightMouseUp: (NSEvent *)theEvent
{
    if (mouseEmulation)
        return;

    MOUSE_EVENT(0x82);
}


/*------------------------------------------------------------------------------
 * otherMouseDown - Should be menu, if we're not using mouse emulation
 */
- (void)otherMouseDown: (NSEvent *)theEvent
{
    NSLog(@"Other mouse down\n");
    
    if (mouseEmulation)
        return;

    MOUSE_EVENT(0x1);
}


/*------------------------------------------------------------------------------
 * otherMouseUp - Inverse of rightMouseDown!
 */
- (void)otherMouseUp: (NSEvent *)theEvent
{
    if (mouseEmulation)
        return;

    MOUSE_EVENT(0x81);
}


/*------------------------------------------------------------------------------
 * mouseDragged - essentially the same as a mouse move in ArcEm's eyes
 */
- (void)mouseDragged: (NSEvent *)theEvent
{
    if (captureMouse)
    {
        CGMouseDelta x, y;

        if (mouseMF == 0)
        {
            CGGetLastMouseDelta(&x, &y);

            nMouseX = x;
            nMouseY = -y;

            mouseMF = 1;
        }
        else
        {
            CGGetLastMouseDelta(&x, &y);

            nMouseX += x;
            nMouseY -= y;
        }
    }
}


/*------------------------------------------------------------------------------
 *
 */
- (void)toggleXScale
{
    if (bXScale == NO)
    {
        bXScale = YES;
        nXScale = 2;
        fXScale = 2.0;
    }
    else
    {
        bXScale = NO;
        nXScale = 1;
        fXScale = 1.0;
    }

    // Force a resize
    [self resizeToWidth: dispFrame.size.width
               toHeight: dispFrame.size.height];
}


/*------------------------------------------------------------------------------
 *
 */
- (void)toggleYScale
{
    if (bYScale == NO)
    {
        bYScale = YES;
        nYScale = 2;
        fYScale = 2.0;
    }
    else
    {
        bYScale = NO;
        nYScale = 1;
        fYScale = 1.0;
    }

    // Force a resize
    [self resizeToWidth: dispFrame.size.width
               toHeight: dispFrame.size.height];
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
    adjustModifier = [defaults integerForKey: AEAdjustModifierKey];
    menuModifier = [defaults integerForKey: AEMenuModifierKey];
}


/*------------------------------------------------------------------------------
 *
 */
- (void)emulatorError: (const char*) message
{
    // No queue here - one critical error is enough :)
    if (strErrorMsg == NULL)
    {
        strErrorMsg = message;
    }
}


/*------------------------------------------------------------------------------
 * dealloc - destructor
 */
- (void)dealloc
{
    [screenImage release];
    [cursorImage release];
}

@end
