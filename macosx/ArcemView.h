
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
    NSImage *screenImage;
    NSImage *cursorImage;

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
    float fXScale, fYScale;
    int nXScale, nYScale;

    const char *strErrorMsg;
    
    BOOL mouseEmulation;
    int adjustModifier, menuModifier;
}
- (void)setBitmapsWithScreen: (NSBitmapImageRep *)si
                  withCursor: (NSBitmapImageRep *)ci;
- (void)resizeToWidth: (int)width
             toHeight: (int)height;
- (void)toggleMouseLock;
- (void)removeMouseLock;
- (void)toggleXScale;
- (void)toggleYScale;
- (void)prefsUpdated;
- (void)setNeedsScaledDisplayInRect: (NSRect)rect;
- (void)emulatorError: (const char*)message;

@end
