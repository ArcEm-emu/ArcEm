
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
 * Name   : ArcemView.h
 * Author : Michael Dales <michael@dcs.gla.ac.uk>
 * 
 ****************************************************************************/

/* ArcemView */

#import <Cocoa/Cocoa.h>

@interface ArcemView : NSView
{
    CGContextRef screenImage;
    CGContextRef cursorImage;

    NSTrackingRectTag recttag;

    BOOL captureMouse;

    NSRect dispFrame;
    
    char keyState[256];
    int nMouse;

    // Lets us return the mouse after we capture it
    NSPoint oldMouse;

    // Scaling info - over the top, but so we're not converting types all the time
    // when rendering the screen.
    BOOL bXScale, bYScale;
    CGFloat fXScale, fYScale;
    int nXScale, nYScale;

    const char *strErrorMsg;
    
    BOOL mouseEmulation;
    int adjustModifier, menuModifier;
}

@property (readonly, getter=isYScaled) BOOL yScaled;
@property (readonly, getter=isXScaled) BOOL xScaled;
@property (nonatomic, getter=isMouseLocked) BOOL mouseLock;

//! Pass the bitmaps from the controller.
- (void)setBitmapsWithScreen: (CGContextRef)si
                  withCursor: (CGContextRef)ci;
/*! Called when the emulator changes screen size, and
 * adjusts the view to match
 */
- (void)resizeToWidth: (int)width
             toHeight: (int)height;
/*! Activated by "Toggle Mouse Lock" menu option. When turning
 * on, we disconnect the mouse cursor from the mouse, move it
 * to a position where it will always be in the window
 * regardless of the window size (to ensure mouse clicks work),
 * activate mouse events, then hide the cursor. When turning
 * off we do the reverse :)
 */
- (void)toggleMouseLock;
//! Used when the application loses focus.
- (void)removeMouseLock;
- (void)toggleXScale;
- (void)toggleYScale;
- (void)prefsUpdated;
- (void)setNeedsScaledDisplayInRect: (NSRect)rect;
- (void)emulatorError: (const char*)message;

@end
