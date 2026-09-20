/**
 * @file alscriptworkspace.h
 * @brief The scripts the viewer can reach: one way to name, load, save and compile them.
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

#include "llassettype.h"
#include "llsd.h"
#include "llsingleton.h"
#include "lluuid.h"

#include <boost/signals2.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class LLInventoryItem;

// Where a script lives: an item of the agent's inventory, or an item of an
// object's contents. The one identity for a script wherever the viewer
// meets it, so that the studio, the legacy floaters, the bridge and the
// compile queue agree about which script is which -- the floater keys and
// the bridge's subscription hash both come from it.
struct ALScriptRef
{
    // Null for the agent's inventory.
    LLUUID object;
    LLUUID item;

    ALScriptRef() = default;
    ALScriptRef(const LLUUID& object_in, const LLUUID& item_in) : object(object_in), item(item_in) {}

    bool inInventory() const { return object.isNull(); }
    bool isNull() const { return item.isNull(); }

    // The bridge's subscription id, which is also the name of the temp
    // file an external editor is given.
    std::string id() const;
    // A floater key, and back.
    LLSD               key() const;
    static ALScriptRef fromKey(const LLSD& key);

    friend bool operator==(const ALScriptRef& a, const ALScriptRef& b) { return a.object == b.object && a.item == b.item; }
    friend bool operator!=(const ALScriptRef& a, const ALScriptRef& b) { return !(a == b); }
};

// What a script is and what happens to it: its language and compile target
// decided once, its text fetched, its text uploaded and compiled with the
// result parsed into the one diagnostic convention, and the running state
// and reset of a script in an object. The legacy floaters decided each of
// these for themselves, four times over for the compile target; the studio
// and the bridge are meant to share this instead (doc/SCRIPT_STUDIO.md,
// section 3.5). Every answer arrives on the main thread.
class ALScriptWorkspace : public LLSingleton<ALScriptWorkspace>
{
    LLSINGLETON(ALScriptWorkspace);

public:
    // A script's language, and the target it compiles for: the item's
    // subtype and runtime where the item says, then what the text looks
    // like. `requested` is a target somebody asked for, which an LSL script
    // asked to run on Luau turns into the LSL-on-Luau target.
    struct Language
    {
        bool        lua = false;
        std::string compileTarget;
    };
    static Language resolve(const LLInventoryItem* item, std::string_view content, const std::string& requested = std::string());
    // No `default` state anywhere means Lua: the same guess the legacy
    // editor made.
    static bool looksLikeLua(std::string_view content);

    // The text of an asset the storage has fetched, from the cache, cut
    // at the first NUL as the legacy readers cut it.
    static bool readAsset(const LLUUID& asset_id, LLAssetType::EType type, std::string& out);

    // --- loading ---------------------------------------------------------

    struct Loaded
    {
        ALScriptRef ref;
        LLUUID      assetId;
        std::string name;
        std::string text;
        Language    language;
        // Whether the agent may see the text at all, and change it.
        bool        viewable   = false;
        bool        modifiable = false;
        // Why there is no text, where there is none.
        std::string error;
    };
    typedef std::function<void(const Loaded&)> load_callback_t;

    // The script's text, fetched if need be. A script the agent may not
    // view comes back without text and says so.
    void load(const ALScriptRef& ref, load_callback_t callback);

    // --- saving and compiling ----------------------------------------------

    // Zero-based line and column, as everything in the studio counts.
    struct Diagnostic
    {
        S32         line   = 0;
        S32         column = 0;
        std::string level;
        std::string message;
        // The server gave a column: LSL's compiler does, Luau's does not.
        bool        hasColumn = false;
    };

    struct CompileResult
    {
        ALScriptRef             ref;
        bool                    success = false;
        bool                    running = false;
        LLUUID                  newAssetId;
        std::vector<Diagnostic> diagnostics;
        // The server's lines as they came.
        std::vector<std::string> messages;
        // Why nothing was compiled, where nothing was.
        std::string error;
    };
    typedef std::function<void(const CompileResult&)> compile_callback_t;

    struct SaveOptions
    {
        std::string compileTarget;
        // Task scripts only: whether it runs after the compile, and the
        // experience it runs under.
        bool   running = true;
        LLUUID experience;
    };

    // Uploads and compiles. False, with why and nothing sent, where there
    // is no region, no capability or no such object; the result otherwise
    // reaches the callback and every listener.
    bool save(const ALScriptRef& ref, const std::string& text, const SaveOptions& options, compile_callback_t callback, std::string& error);

    // The server's error strings as diagnostics.
    static std::vector<Diagnostic> parseDiagnostics(const LLSD& errors, bool lua);

    typedef boost::signals2::signal<void(const CompileResult&)> compiled_signal_t;
    boost::signals2::connection onCompiled(const compiled_signal_t::slot_type& slot) { return mCompiled.connect(slot); }

    // --- a script in an object -----------------------------------------------

    bool setRunning(const ALScriptRef& ref, bool running);
    bool reset(const ALScriptRef& ref);

private:
    struct LoadRequest;
    static void onAssetLoaded(const LLUUID& asset_id, LLAssetType::EType type, void* user_data, S32 status, LLExtStat ext_status);
    void        deliver(const CompileResult& result, const compile_callback_t& callback);
    bool        scriptMessage(const ALScriptRef& ref, const char* message, bool running, bool with_running);

    compiled_signal_t mCompiled;
};
