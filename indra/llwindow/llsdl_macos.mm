/**
 * @file llsdl_macos.mm
 * @brief macOS-specific SDL helpers
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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

#ifdef LL_DARWIN

#import <Cocoa/Cocoa.h>

#include "SDL3/SDL.h"

#include "llsdl_macos.h"

void ll_sdl_macos_strip_default_close_shortcut()
{
    @autoreleasepool
    {
        // SDL3's Cocoa backend auto-creates a default menu bar (it does this
        // whenever [NSApp mainMenu] is nil at registration) whose "Window"
        // menu contains a "Close" item bound to Cmd+W via -performClose:. That
        // native item swallows Cmd+W before it can reach the viewer's own
        // in-window "Close Window" accelerator (menu_viewer.xml -> control|W ->
        // File.CloseWindow, which just closes the frontmost floater). Worse,
        // -performClose: on the sole viewer window triggers
        // SDL_EVENT_WINDOW_CLOSE_REQUESTED, which LLWindowSDL turns into a full
        // application quit. Clear the key equivalent so Cmd+W falls through to
        // LLMenuGL instead of quitting the viewer. The menu item itself stays
        // (still clickable); only its keyboard shortcut is removed.
        // SDL parks "Close" in the menu it hands to -setWindowsMenu:, but scan
        // every top-level submenu rather than assuming that location, so this
        // keeps working if SDL ever rearranges its default bar.
        NSMenu* main_menu = [NSApp mainMenu];
        if (!main_menu)
        {
            return;
        }

        for (NSMenuItem* top_item in [main_menu itemArray])
        {
            NSMenu* submenu = [top_item submenu];
            if (!submenu)
            {
                continue;
            }

            for (NSMenuItem* item in [submenu itemArray])
            {
                if ([item action] == @selector(performClose:))
                {
                    [item setKeyEquivalent:@""];
                    [item setKeyEquivalentModifierMask:0];
                }
            }
        }
    }
}

void ll_sdl_macos_make_window_key_deferred(struct SDL_Window* window)
{
    if (!window)
    {
        return;
    }

    NSWindow* ns_window = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!ns_window)
    {
        return;
    }

    // After a native dialog closes, SDL only reactivates the app, leaving no
    // key window: keystrokes hit no responder and AppKit beeps. Re-key the
    // window so it becomes key again and SDL emits FOCUS_GAINED, restoring input.
    // Defer to the next main-queue turn so this runs after SDL's own
    // ReactivateAfterDialog instead of being clobbered by it.
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp activateIgnoringOtherApps:YES];
        [ns_window makeKeyAndOrderFront:nil];
    });
}

#endif // LL_DARWIN
