/**
 * @file alscriptrecompile.h
 * @brief Scripts, prims and whole objects recompiled for a target from the Explorer, each result in Output and a closed script's problems in Problems.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */


#pragma once

#include "alscriptstudiodoc.h"
#include "alscriptworkspace.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ALScriptStudioServices;

// A Script Studio window's recompile from the Explorer: the scripts chosen,
// and every script of the prims chosen once each has said what it holds,
// sent up again a few at a time for one target -- or each for what it
// compiles for now -- through the workspace, as the legacy compile queue's
// window did, and said here instead:
// - each script's word in Output as it comes: compiled, how many problems,
//   or why it did not go up;
// - what the compiler said of one no tab holds, read back through the
//   expansion that went up to its source and its includes, in Problems
//   under its name and object, until a tab opens it or it compiles clean;
//   one a tab holds shows it in the tab, which hears the compile itself;
// - a count when all have.
// One with a save on its way is left alone, and said so, since the
// server's text would land over it; one with changes not saved in a tab
// goes up as the server has it, and that is said. A script known to be
// stopped stays stopped; one not known is asked of before it goes. An SLua
// script compiles as SLua whatever LSL target is asked. One begun again
// lets the last go.
class ALScriptRecompile
{
public:
    typedef ALScriptStudioDoc Doc;

    // A script to recompile: where it is, what it is called, what it is in.
    struct One
    {
        ALScriptRef ref;
        std::string name;
        std::string object;
        bool        lua = false;
    };
    // One script's recompile: compiled, or not with the compiler's rows,
    // or not sent and why; `open` where a tab holds it.
    struct Script
    {
        One                     one;
        bool                    compiled = false;
        bool                    open     = false;
        std::string             error;
        std::vector<Doc::Shown> rows;
    };
    // A recompile done: what came of each, and the prims that did not say
    // what they hold.
    struct Done
    {
        std::string target;
        S32         compiled = 0;
        S32         failed   = 0;
        S32         notSent  = 0;
        S32         skipped  = 0;
        S32         unlisted = 0;
    };

    // What the recompile asks of the window beyond its services.
    class Window
    {
    public:
        // The scripts of the prims, each prim with its object's name, told
        // once every one has said what it holds; how many did not.
        struct Listed
        {
            std::vector<One> scripts;
            S32              unlisted = 0;
        };
        virtual void listScripts(const std::vector<std::pair<LLUUID, std::string>>& prims, std::function<void(Listed)> told) = 0;
        // Whether a tab holds a script; whether a save of it is on its way;
        // whether a tab has changes of it not saved; whether it runs, where
        // that is known.
        virtual bool                isOpen(const ALScriptRef& ref)       = 0;
        virtual bool                saving(const ALScriptRef& ref)       = 0;
        virtual bool                unsaved(const ALScriptRef& ref)      = 0;
        virtual std::optional<bool> knownRunning(const ALScriptRef& ref) = 0;
        // The workspace's recompile.
        virtual void recompile(const ALScriptRef& ref, const std::string& target, std::optional<bool> running,
                               ALScriptWorkspace::compile_callback_t told) = 0;
        // A script recompiled, and the recompile done.
        virtual void scriptRecompiled(const Script& script) = 0;
        virtual void recompiled(const Done& done)           = 0;

    protected:
        ~Window() = default;
    };

    ALScriptRecompile(ALScriptStudioServices& services, Window& window);

    // The scripts and every script of the prims, for `target`: "auto" for
    // each as it compiles now, or an LSL one -- "mono", "lsl2",
    // "lsl-luau". A script also in a prim chosen goes up once.
    void recompile(std::vector<One> scripts, std::vector<std::pair<LLUUID, std::string>> prims, const std::string& target);
    bool running() const { return mRunning; }

    // How many go up at once: as many as the script upload pool takes.
    static constexpr S32 AT_ONCE = 4;

    // What the compiler said, read back through the expansion that went up
    // to the source and its includes, as the Problems tab lists it.
    // `origin` names the compiler, `generated` code the preprocessor made.
    static std::vector<Doc::Shown> rowsOf(const ALScriptWorkspace::CompileResult& result, const std::string& origin, const std::string& generated);

private:
    void listed(U32 generation, std::vector<One> chosen, Window::Listed listed);
    void feed();
    void begin(const One& one);
    void answered(const One& one, const ALScriptWorkspace::CompileResult& result);
    void passed();

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    U32                     mGeneration = 0;
    bool                    mRunning    = false;
    std::string             mTarget;
    Done                    mDone;
    std::vector<One>        mLeft;
    size_t                  mNext     = 0;
    S32                     mUnderWay = 0;
    bool                    mFeeding  = false;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};
