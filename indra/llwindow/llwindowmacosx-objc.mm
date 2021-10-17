/**
 * @file llwindowmacosx-objc.mm
 * @brief Definition of functions shared between llwindowmacosx.cpp
 * and llwindowmacosx-objc.mm.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#import "llopenglview-objc.h"
#import "llwindowmacosx-objc.h"
#import "llappdelegate-objc.h"

#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>

/*
 * These functions are broken out into a separate file because the
 * objective-C typedef for 'BOOL' conflicts with the one in
 * llcommon/stdtypes.h.  This makes it impossible to use the standard
 * linden headers with any objective-C++ source.
 */

int createNSApp(int argc, const char *argv[])
{
	return NSApplicationMain(argc, argv);
}

void setupCocoa()
{
	static bool inited = false;
	
	if(!inited)
	{
        @autoreleasepool {
            // The following prevents the Cocoa command line parser from trying to open 'unknown' arguements as documents.
            // ie. running './secondlife -set Language fr' would cause a pop-up saying can't open document 'fr'
            // when init'ing the Cocoa App window.
            [[NSUserDefaults standardUserDefaults] setObject:@"NO" forKey:@"NSTreatUnknownArgumentsAsOpen"];
        } // @autoreleasepool

		inited = true;
	}
}

bool copyToPBoard(const unsigned short *str, unsigned int len)
{
    @autoreleasepool {
        NSPasteboard *pboard = [NSPasteboard generalPasteboard];
        [pboard clearContents];
        
        NSArray *contentsToPaste = [[NSArray alloc] initWithObjects:[NSString stringWithCharacters:str length:len], nil];
        BOOL ret = [pboard writeObjects:contentsToPaste];
        return ret;
    }
}

bool pasteBoardAvailable()
{
	NSArray *classArray = [NSArray arrayWithObject:[NSString class]];
	return [[NSPasteboard generalPasteboard] canReadObjectForClasses:classArray options:[NSDictionary dictionary]];
}

unsigned short *copyFromPBoard()
{
    @autoreleasepool {
        NSPasteboard *pboard = [NSPasteboard generalPasteboard];
        NSArray *classArray = [NSArray arrayWithObject:[NSString class]];
        NSString *str = NULL;
        BOOL ok = [pboard canReadObjectForClasses:classArray options:[NSDictionary dictionary]];
        if (ok)
        {
            NSArray *objToPaste = [pboard readObjectsForClasses:classArray options:[NSDictionary dictionary]];
            str = [objToPaste objectAtIndex:0];
        }
        NSUInteger len = [str length];
        unichar* temp = (unichar*)calloc(len+1, sizeof(unichar));
        [str getCharacters:temp range:NSMakeRange(0, len)];
        
        return temp;
    }
}

CursorRef createImageCursor(const char *fullpath, int hotspotX, int hotspotY)
{
    @autoreleasepool {
        // extra retain on the NSCursor since we want it to live for the lifetime of the app.
        NSCursor *cursor =
        [[NSCursor alloc]
          initWithImage:
          [[NSImage alloc] initWithContentsOfFile:
            [NSString stringWithFormat:@"%s", fullpath]
            ]
          hotSpot:NSMakePoint(hotspotX, hotspotY)
          ];
        
        return (CursorRef)CFBridgingRetain(cursor);
    }
}

void setArrowCursor()
{
	NSCursor *cursor = [NSCursor arrowCursor];
	[NSCursor unhide];
	[cursor set];
}

void setIBeamCursor()
{
	NSCursor *cursor = [NSCursor IBeamCursor];
	[cursor set];
}

void setPointingHandCursor()
{
	NSCursor *cursor = [NSCursor pointingHandCursor];
	[cursor set];
}

void setCopyCursor()
{
	NSCursor *cursor = [NSCursor dragCopyCursor];
	[cursor set];
}

void setCrossCursor()
{
	NSCursor *cursor = [NSCursor crosshairCursor];
	[cursor set];
}

void setNotAllowedCursor()
{
	NSCursor *cursor = [NSCursor operationNotAllowedCursor];
	[cursor set];
}

void hideNSCursor()
{
	[NSCursor hide];
}

void showNSCursor()
{
	[NSCursor unhide];
}

void hideNSCursorTillMove(bool hide)
{
	[NSCursor setHiddenUntilMouseMoves:hide];
}

// This is currently unused, since we want all our cursors to persist for the life of the app, but I've included it for completeness.
OSErr releaseImageCursor(CursorRef ref)
{
	if( ref != NULL )
	{
        @autoreleasepool {
            CFBridgingRelease(ref);
        } // @autoreleasepool
	}
	else
	{
		return paramErr;
	}
	
	return noErr;
}

OSErr setImageCursor(CursorRef ref)
{
	if( ref != NULL )
	{
        NSCursor *cursor = (__bridge NSCursor*)ref;
        [cursor set];
	}
	else
	{
		return paramErr;
	}
	
	return noErr;
}

// Now for some unholy juggling between generic pointers and casting them to Obj-C objects!
// Note: things can get a bit hairy from here.  This is not for the faint of heart.

NSWindowRef createNSWindow(int x, int y, int width, int height)
{
	LLNSWindow *window = [[LLNSWindow alloc]initWithContentRect:NSMakeRect(x, y, width, height)
                                                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
	[window makeKeyAndOrderFront:nil];
	[window setAcceptsMouseMovedEvents:TRUE];
	return (NSWindowRef)CFBridgingRetain(window);
}

GLViewRef createOpenGLView(NSWindowRef window, unsigned int samples, bool vsync)
{
	LLOpenGLView *glview = [[LLOpenGLView alloc]initWithFrame:[(__bridge LLNSWindow*)window frame] withSamples:samples andVsync:vsync];
	[(__bridge LLNSWindow*)window setContentView:glview];
	return (GLViewRef)CFBridgingRetain(glview);
}

void glSwapBuffers(void* context)
{
	[(__bridge NSOpenGLContext*)context flushBuffer];
}

CGLContextObj getCGLContextObj(GLViewRef view)
{
	return [(__bridge LLOpenGLView *)view getCGLContextObj];
}

CGLPixelFormatObj* getCGLPixelFormatObj(NSWindowRef window)
{
	LLOpenGLView *glview = [(__bridge LLNSWindow*)window contentView];
	return [glview getCGLPixelFormatObj];
}

unsigned long getVramSize(GLViewRef view)
{
	return [(__bridge LLOpenGLView *)view getVramSize];
}

float getDeviceUnitSize(GLViewRef view)
{
	return [(__bridge LLOpenGLView*)view convertSizeToBacking:NSMakeSize(1, 1)].width;
}

void getContentViewBounds(NSWindowRef window, float* bounds)
{
	bounds[0] = [[(__bridge LLNSWindow*)window contentView] bounds].origin.x;
	bounds[1] = [[(__bridge LLNSWindow*)window contentView] bounds].origin.y;
	bounds[2] = [[(__bridge LLNSWindow*)window contentView] bounds].size.width;
	bounds[3] = [[(__bridge LLNSWindow*)window contentView] bounds].size.height;
}

void getScaledContentViewBounds(NSWindowRef window, GLViewRef view, float* bounds)
{
    NSRect b = [(__bridge NSOpenGLView*)view convertRectToBacking:[[(__bridge LLNSWindow*)window contentView] bounds]];
	bounds[0] = b.origin.x;
	bounds[1] = b.origin.y;
	bounds[2] = b.size.width;
	bounds[3] = b.size.height;
}

void getWindowSize(NSWindowRef window, float* size)
{
	NSRect frame = [(__bridge LLNSWindow*)window frame];
	size[0] = frame.origin.x;
	size[1] = frame.origin.y;
	size[2] = frame.size.width;
	size[3] = frame.size.height;
}

void setWindowSize(NSWindowRef window, int width, int height)
{
	NSRect frame = [(__bridge LLNSWindow*)window frame];
	frame.size.width = width;
	frame.size.height = height;
	[(__bridge LLNSWindow*)window setFrame:frame display:TRUE];
}

void setWindowPos(NSWindowRef window, float* pos)
{
	NSPoint point;
	point.x = pos[0];
	point.y = pos[1];
	[(__bridge LLNSWindow*)window setFrameOrigin:point];
}

void getCursorPos(NSWindowRef window, float* pos)
{
	NSPoint mLoc;
	mLoc = [(__bridge LLNSWindow*)window mouseLocationOutsideOfEventStream];
	pos[0] = mLoc.x;
	pos[1] = mLoc.y;
}

void makeWindowOrderFront(NSWindowRef window)
{
	[(__bridge LLNSWindow*)window makeKeyAndOrderFront:nil];
}

void convertScreenToWindow(NSWindowRef window, float *coord)
{
	NSRect point = NSMakeRect(coord[0], coord[1], 0, 0);
	point = [(__bridge LLNSWindow*)window convertRectFromScreen:point];
	coord[0] = point.origin.x;
	coord[1] = point.origin.y;
}

void convertRectToScreen(NSWindowRef window, float *coord)
{
	NSRect point = NSMakeRect(coord[0], coord[1], coord[2], coord[3]);
	point = [(__bridge LLNSWindow*)window convertRectToScreen:point];
	
	coord[0] = point.origin.x;
	coord[1] = point.origin.y;
	coord[2] = point.size.width;
	coord[3] = point.size.height;
}

void convertRectFromScreen(NSWindowRef window, float *coord)
{
	NSRect point = NSMakeRect(coord[0], coord[1], coord[2], coord[3]);
	point = [(__bridge LLNSWindow*)window convertRectFromScreen:point];
	
	coord[0] = point.origin.x;
	coord[1] = point.origin.y;
	coord[2] = point.size.width;
	coord[3] = point.size.height;
}

void convertWindowToScreen(NSWindowRef window, float *coord)
{
	LLNSWindow *nsWindow = (__bridge LLNSWindow*)window;
	NSRect rect = NSMakeRect(coord[0], coord[1], 0, 0);
	rect = [nsWindow convertRectToScreen:rect];
	NSRect screenRect = [[nsWindow screen] frame];
	NSPoint retPoint = NSMakePoint(rect.origin.x, screenRect.origin.y + screenRect.size.height - rect.origin.y);
	coord[0] = retPoint.x;
	coord[1] = retPoint.y;
}

void closeWindow(NSWindowRef windowRef)
{
    LLNSWindow *window = (LLNSWindow*)CFBridgingRelease(windowRef);
	[window close];
}

void removeGLView(GLViewRef viewRef)
{
    LLOpenGLView *view_ctx = (LLOpenGLView*)CFBridgingRelease(viewRef);
	[(LLOpenGLView*)view_ctx clearGLContext];
	[(LLOpenGLView*)view_ctx removeFromSuperview];
}

void setupInputWindow(NSWindowRef window, GLViewRef glview)
{
	[[(LLAppDelegate*)[NSApp delegate] inputView] setGLView:(__bridge LLOpenGLView*)glview];
}

void commitCurrentPreedit(GLViewRef glView)
{
	[(__bridge LLOpenGLView*)glView commitCurrentPreedit];
}

void allowDirectMarkedTextInput(bool allow, GLViewRef glView)
{
    [(__bridge LLOpenGLView*)glView allowMarkedTextInput:allow];
}

NSWindowRef getMainAppWindow()
{
	LLNSWindow *winRef = [(LLAppDelegate*)[[LLApplication sharedApplication] delegate] window];
	
	[winRef setAcceptsMouseMovedEvents:TRUE];
	return (NSWindowRef)CFBridgingRetain(winRef);
}

void makeFirstResponder(NSWindowRef window, GLViewRef view)
{
	[(__bridge LLNSWindow*)window makeFirstResponder:(__bridge LLOpenGLView*)view];
}

void requestUserAttention()
{
	[[LLApplication sharedApplication] requestUserAttention:NSInformationalRequest];
}

long showAlert(std::string text, std::string title, int type)
{
    NSModalResponse response;
    @autoreleasepool {
        NSAlert *alert = [[NSAlert alloc] init];
        
        [alert setMessageText:[NSString stringWithCString:title.c_str() encoding:[NSString defaultCStringEncoding]]];
        [alert setInformativeText:[NSString stringWithCString:text.c_str() encoding:[NSString defaultCStringEncoding]]];
        if (type == 0)
        {
            [alert addButtonWithTitle:@"Okay"];
        } else if (type == 1)
        {
            [alert addButtonWithTitle:@"Okay"];
            [alert addButtonWithTitle:@"Cancel"];
        } else if (type == 2)
        {
            [alert addButtonWithTitle:@"Yes"];
            [alert addButtonWithTitle:@"No"];
        }
        response = [alert runModal];
    }
    if (response == NSAlertFirstButtonReturn)
    {
        if (type == 1)
        {
            response = 3;
        } else if (type == 2)
        {
            response = 0;
        }
    } else if (response == NSAlertSecondButtonReturn)
    {
        if (type == 0 || type == 1)
        {
            response = 2;
        } else if (type == 2)
        {
            response = 1;
        }
    }
    
    return response;
}

unsigned int getModifiers()
{
	return [NSEvent modifierFlags];
}

void setTitle(const std::string& title)
{
    @autoreleasepool {
        LLNSWindow *winRef = [(LLAppDelegate*)[[LLApplication sharedApplication] delegate] window];
        NSString *nsTitle = [NSString stringWithUTF8String:title.c_str()];
        [winRef setTitle:nsTitle];
    }
}

