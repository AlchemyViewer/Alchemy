/**
 * @file llsdlfiledialog.h
 * @brief Async SDL3 file dialog helpers shared by LLFilePicker and LLDirPicker.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

#ifndef LL_LLSDLFILEDIALOG_H
#define LL_LLSDLFILEDIALOG_H

#if LL_SDL_WINDOW

#include "SDL3/SDL.h"
#include "llwindowsdl.h"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Heap-allocated holder for an async SDL3 file dialog launch.
//
// SDL3's file dialog API is async — the callback fires from the platform UI
// thread some time after SDL_ShowFileDialogWithProperties returns. Three
// things have to outlive the launch site:
//
//   * the user's callback function pointer + opaque userdata
//   * the std::vector<SDL_DialogFileFilter> that backs the
//     SDL_PROP_FILE_DIALOG_FILTERS_POINTER property — SDL3 backends differ
//     on whether they snapshot the filter strings before
//     SDL_ShowFileDialogWithProperties returns, so a stack-local vector
//     would dangle by the time the user clicks OK.
//
// LLSDLFileDialogContext owns all three. Ownership transfers to SDL on
// LLSDLFileDialog::show; LLSDLFileDialog::trampoline deletes the holder
// when the dialog completes (success, cancel, or error).
//
// ResultT is std::string for single-result dialogs (save, folder picker)
// and std::vector<std::string> for multi-select file pickers.
template <typename ResultT>
struct LLSDLFileDialogContext
{
    using Callback = void (*)(bool, ResultT&, void*);

    LLSDLFileDialogContext(Callback cb, void* ud,
                           std::vector<SDL_DialogFileFilter> filters = {})
        : mCallback(cb), mUserdata(ud), mFilters(std::move(filters))
    {
    }

    Callback mCallback;
    void* mUserdata;
    std::vector<SDL_DialogFileFilter> mFilters;
};

namespace LLSDLFileDialog
{
    // C-style trampoline matching SDL_DialogFileCallback. Decodes the
    // filelist into ResultT, invokes the user's typed callback, and
    // frees the heap-allocated context. ResultT-specialised below for
    // multi-select (vector<string>) vs single-result (string).
    template <typename ResultT>
    inline void trampoline(void* userdata, const char* const* filelist, int /*filter*/)
    {
        // Dialog resolved; close the bracket opened in show(). Restores
        // fullscreen/mouselook and, once the last dialog closes, key focus.
        LLWindowSDL::exitDialog();

        auto* ctx = static_cast<LLSDLFileDialogContext<ResultT>*>(userdata);
        auto* cb = ctx->mCallback;
        auto* user = ctx->mUserdata;
        delete ctx; // own and free the holder regardless of outcome below

        ResultT result{};

        if (!filelist)
        {
            LL_WARNS() << "Error during SDL file picking: " << SDL_GetError() << LL_ENDL;
            cb(false, result, user);
            return;
        }
        if (!*filelist)
        {
            LL_INFOS() << "User did not select any file. Dialog likely cancelled." << LL_ENDL;
            cb(false, result, user);
            return;
        }

        if constexpr (std::is_same_v<ResultT, std::vector<std::string>>)
        {
            while (*filelist)
            {
                result.emplace_back(*filelist);
                ++filelist;
            }
        }
        else
        {
            // Single-result path (std::string): take the first entry.
            // SDL3 still passes a list with one or more entries here, but
            // single-select dialogs always deliver exactly one path.
            result = *filelist;
        }

        cb(true, result, user);
    }

    // Launch an SDL3 file dialog. Sets the standard FILTERS_POINTER /
    // NFILTERS_NUMBER (when the holder carries filters), WINDOW_POINTER
    // (always — uses the main viewer window so the dialog is parented
    // correctly even when a worker thread holds a GL context current),
    // MANY_BOOLEAN, and LOCATION_STRING when `location` is non-empty
    // (the Win32 and Cocoa backends use this to seed the starting
    // folder / suggested filename, matching the non-SDL pickers).
    //
    // Ownership of `ctx` transfers to SDL: it is read by SDL during
    // dialog display and freed by trampoline<ResultT> on completion.
    template <typename ResultT>
    inline void show(SDL_FileDialogType type, LLSDLFileDialogContext<ResultT>* ctx, bool allow_many,
                     const std::string& location = {})
    {
        SDL_PropertiesID props = SDL_CreateProperties();
        if (!ctx->mFilters.empty())
        {
            SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, ctx->mFilters.data());
            SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, ctx->mFilters.size());
        }
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, LLWindowSDL::getMainSDLWindow());
        SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, allow_many);
        if (!location.empty())
        {
            SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
        }

        // Open the dialog bracket; trampoline() closes it. Drops fullscreen and
        // mouselook for the dialog's duration, like the blocking pickers do.
        LLWindowSDL::enterDialog();

        SDL_ShowFileDialogWithProperties(type, &trampoline<ResultT>, ctx, props);

        SDL_DestroyProperties(props);
    }
}

#endif // LL_SDL_WINDOW

#endif // LL_LLSDLFILEDIALOG_H
