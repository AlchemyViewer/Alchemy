/**
 * @file alscriptobjectcheck.h
 * @brief Every script of an object checked, the ones no tab holds read and asked of the analyzers as a tab is.
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

#include "alluauconfig.h"
#include "alpreprocessor.h"
#include "alscriptanalysis.h"
#include "alscriptpreprocessor.h"
#include "alscriptstudiodoc.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptStudioServices;

// A Script Studio window's check of every script of an object: its prims
// all asked what they hold first, a large linkset's folded ones among them;
// the scripts a tab holds left to their tabs, whose problems the Problems
// tab lists already; each other one read, expanded as the compiler sees it
// where the preprocessor runs, and asked of the analyzers as a tab's check
// is -- a few at a time -- with what comes back read through the expansion
// to its source, the scripter's lints applied, and made into the rows the
// Problems tab lists under the script's name. What it could not reach is
// said when it is done. A check begun again lets the last one go.
class ALScriptObjectCheck
{
public:
    typedef ALScriptStudioDoc Doc;

    // One script's problems, as the Problems tab lists them.
    struct Script
    {
        ALScriptRef             ref;
        std::string             name;
        bool                    lua = false;
        std::vector<Doc::Shown> rows;
    };
    // A check done: over which object, how many scripts it checked and how
    // many it left to their tabs, and what it could not reach -- prims that
    // did not say what they hold, and scripts that could not be read.
    struct Done
    {
        LLUUID                   root;
        std::string              name;
        bool                     present = true;
        S32                      checked = 0;
        S32                      open    = 0;
        S32                      unlisted = 0;
        std::vector<std::string> unread;
    };

    // What the check asks of the window beyond its services.
    class Window
    {
    public:
        // The object's scripts, told once every prim of it has said what it
        // holds; how many prims did not; and whether it is in sight at all.
        struct Listed
        {
            struct One
            {
                ALScriptRef ref;
                std::string name;
            };
            std::vector<One> scripts;
            S32              unlisted = 0;
            bool             present  = true;
        };
        virtual void listScripts(const LLUUID& root, std::function<void(Listed)> told) = 0;
        // Whether a tab holds a script, in any of the studio's windows.
        virtual bool isOpen(const ALScriptRef& ref) = 0;
        // A script's text as its author wrote it, and what it is: nothing
        // where it could not be read.
        struct Read
        {
            std::string text;
            LLUUID      assetId;
            bool        lua      = false;
            std::string compileTarget;
            // It went up in the preprocessor's envelope, so it is read as
            // the preprocessor makes it whatever the setting.
            bool        enveloped = false;
        };
        virtual void read(const ALScriptRef& ref, std::function<void(std::optional<Read>)> told) = 0;
        // Whether the preprocessor runs; a script expanded, and the
        // analyzers asked -- for its problems alone, no colours nor hints;
        // an SLua script's `.luaurc`, where it has one in hand.
        virtual bool preprocessing() const                                                                                          = 0;
        virtual void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> expanded)     = 0;
        virtual void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) = 0;
        virtual bool luauConfig(const ALScriptPreprocessor::Request& root, ALLuauConfig& config) const                              = 0;
        // A script checked, and the check done.
        virtual void scriptChecked(const Script& script) = 0;
        virtual void objectChecked(const Done& done)     = 0;

    protected:
        ~Window() = default;
    };

    ALScriptObjectCheck(ALScriptStudioServices& services, Window& window);

    // Every script of the object with `root`, called `name`, checked.
    void check(const LLUUID& root, const std::string& name);
    // Whether one is under way.
    bool running() const { return mRunning; }

    // How many scripts are read, expanded and asked about at once, as a
    // lookup's are.
    static constexpr S32 AT_ONCE = 2;

private:
    void listed(U32 generation, Window::Listed listed);
    void feed();
    void begin(const Window::Listed::One& one);
    void checkRead(const Window::Listed::One& one, const Window::Read& read);
    // The analyzers asked about what was read, or about its expansion,
    // which is kept until the answer is read back through it.
    void ask(const Window::Listed::One& one, const Window::Read& read, std::shared_ptr<const ALPreprocessor::Result> expansion);
    void answered(const Window::Listed::One& one, bool lua, const ALScriptAnalysis::Result& result,
                  std::shared_ptr<const ALPreprocessor::Result> expansion);
    // One script done with, checked or passed over: the next begun, and the
    // check said done where it was the last.
    void passed();

    ALScriptStudioServices&         mServices;
    Window&                         mWindow;
    U32                             mGeneration = 0;
    bool                            mRunning    = false;
    Done                            mDone;
    std::vector<Window::Listed::One> mLeft;
    size_t                          mNext       = 0;
    S32                             mUnderWay   = 0;
    bool                            mFeeding    = false;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>           mAlive = std::make_shared<bool>(true);
};
