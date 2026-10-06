/**
 * @file alluauconfigscript.cpp
 * @brief A .config.luau run in a VM of its own, and what it returns read as a .luaurc.
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

#include "linden_common.h"

#include "alluauconfigscript.h"

#include "Luau/Common.h"
#include "Luau/Compiler.h"
#include "lua.h"
#include "lualib.h"

#include <algorithm>
#include <string_view>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

LUAU_FASTFLAG(LuauIntegerLibrary)

namespace
{
    // What a run may take, which the state's allocator and its interrupt
    // hold it to.
    struct Limits
    {
        std::chrono::steady_clock::time_point deadline;
        size_t                                bytes = 0;
        // Off once the run is over: reading what it returned raises no
        // error, there being nothing to catch one.
        bool                                  capped = true;
    };

    void* allocate(void* ud, void* ptr, size_t osize, size_t nsize)
    {
        Limits& limits = *static_cast<Limits*>(ud);
        if (nsize == 0)
        {
            free(ptr);
            limits.bytes -= osize;
            return nullptr;
        }
        // Past the most it may hold: the VM raises its own error for it.
        if (limits.capped && nsize > osize && limits.bytes + (nsize - osize) > ALLuauConfigScript::MOST_BYTES)
        {
            return nullptr;
        }
        void* out = realloc(ptr, nsize);
        if (out)
        {
            limits.bytes = limits.bytes - osize + nsize;
        }
        return out;
    }

    // At a loop's turn, a call or a return: past the deadline, an error.
    // Not at the collector's steps, which it is not raised from.
    void interrupt(lua_State* L, int gc)
    {
        if (gc < 0 && std::chrono::steady_clock::now() > static_cast<Limits*>(lua_callbacks(L)->userdata)->deadline)
        {
            luaL_errorL(L, "configuration execution timed out");
        }
    }

    // Luau's libraries as luaL_openlibs opens them, but Eris.
    void openLibraries(lua_State* L)
    {
        static const luaL_Reg LIBRARIES[] = {
            { "", luaopen_base },
            { LUA_COLIBNAME, luaopen_coroutine },
            { LUA_TABLIBNAME, luaopen_table },
            { LUA_OSLIBNAME, luaopen_os },
            { LUA_STRLIBNAME, luaopen_string },
            { LUA_MATHLIBNAME, luaopen_math },
            { LUA_DBLIBNAME, luaopen_debug },
            { LUA_UTF8LIBNAME, luaopen_utf8 },
            { LUA_BITLIBNAME, luaopen_bit32 },
            { LUA_BUFFERLIBNAME, luaopen_buffer },
            { LUA_VECLIBNAME, luaopen_vector },
            { LUA_INTLIBNAME, luaopen_integer },
        };
        for (const luaL_Reg& library : LIBRARIES)
        {
            if (library.func == luaopen_integer && !FFlag::LuauIntegerLibrary)
            {
                continue;
            }
            lua_pushcfunction(L, library.func, nullptr);
            lua_pushstring(L, library.name);
            lua_call(L, 1, 0);
        }
    }

    // A string as a `.luaurc` holds it: as it is, Luau's reading of one
    // taking what is between the quotes byte for byte, escapes and all.
    // False for one no `.luaurc` can say: one holding a quote or a control
    // character, or ending in a backslash, which would escape its closing
    // quote.
    bool quoted(std::string& out, std::string_view text)
    {
        const bool sayable = std::none_of(text.begin(), text.end(), [](char c) { return c == '"' || static_cast<unsigned char>(c) < 0x20; }) &&
                             (text.empty() || text.back() != '\\');
        if (sayable)
        {
            out += '"';
            out += text;
            out += '"';
        }
        return sayable;
    }

    constexpr const char* UNSAYABLE = "configuration strings may not hold a quote or a control character, nor end in a backslash";

    // The value at the top of the stack written as JSON, and popped: a
    // string, a number, a boolean; a table, as an array where its keys are
    // 1 to n -- `as_array` says which an empty one is -- else an object,
    // its keys strings. What Luau's own reading of the table takes; what
    // the `.luaurc` it becomes says of each value is Luau's to judge. Read
    // raw, so that no metamethod runs.
    bool write(lua_State* L, std::string& out, std::string& error, bool as_array, int depth)
    {
        const int  top  = lua_gettop(L);
        const auto fail = [&](const char* why) {
            error = why;
            lua_settop(L, top - 1);
            return false;
        };
        switch (lua_type(L, top))
        {
            case LUA_TSTRING:
            {
                size_t      size = 0;
                const char* text = lua_tolstring(L, top, &size);
                if (!quoted(out, std::string_view(text, size)))
                {
                    return fail(UNSAYABLE);
                }
                break;
            }
            case LUA_TNUMBER:
                out += llformat("%.17g", lua_tonumber(L, top));
                break;
            case LUA_TBOOLEAN:
                out += lua_toboolean(L, top) ? "true" : "false";
                break;
            case LUA_TTABLE:
            {
                if (depth > 8)
                {
                    return fail("configuration tables nest too deeply");
                }
                // Its keys first, then each value by its key: an array's in
                // order, an object's in the order of its names.
                std::vector<double>      numbered;
                std::vector<std::string> named;
                lua_pushnil(L);
                while (lua_next(L, top) != 0)
                {
                    lua_pop(L, 1);
                    if (lua_type(L, -1) == LUA_TNUMBER)
                    {
                        numbered.push_back(lua_tonumber(L, -1));
                    }
                    else if (lua_type(L, -1) == LUA_TSTRING)
                    {
                        size_t      size = 0;
                        const char* text = lua_tolstring(L, -1, &size);
                        named.emplace_back(text, size);
                    }
                    else
                    {
                        return fail("configuration table keys must be strings or numbers");
                    }
                }
                std::sort(numbered.begin(), numbered.end());
                std::sort(named.begin(), named.end());
                const bool array = named.empty() && (!numbered.empty() || as_array);
                if (!array && !numbered.empty())
                {
                    return fail("configuration table keys must be all strings or all numbers");
                }
                for (size_t i = 0; i < numbered.size(); ++i)
                {
                    if (numbered[i] != static_cast<double>(i + 1))
                    {
                        return fail("configuration array contains invalid numeric key");
                    }
                }
                out += array ? '[' : '{';
                const size_t count = array ? numbered.size() : named.size();
                for (size_t i = 0; i < count; ++i)
                {
                    out += i ? ", " : "";
                    if (array)
                    {
                        lua_pushnumber(L, numbered[i]);
                    }
                    else
                    {
                        if (!quoted(out, named[i]))
                        {
                            return fail(UNSAYABLE);
                        }
                        out += ": ";
                        lua_pushlstring(L, named[i].data(), named[i].size());
                    }
                    lua_rawget(L, top);
                    if (!write(L, out, error, false, depth + 1))
                    {
                        lua_settop(L, top - 1);
                        return false;
                    }
                }
                out += array ? ']' : '}';
                break;
            }
            default:
                return fail("configuration values must be strings, numbers, booleans, or nested tables");
        }
        lua_settop(L, top - 1);
        return true;
    }

    // The `luau` table of what a configuration returned, at the top of the
    // stack, as a `.luaurc`: its keys as a `.luaurc` names them, those
    // Luau does not read passed over as it passes them over.
    bool luaurcOf(lua_State* L, std::string& json, std::string& error)
    {
        lua_rawgetfield(L, -1, "luau");
        if (lua_isnil(L, -1))
        {
            json = "{}";
            return true;
        }
        if (!lua_istable(L, -1))
        {
            error = "configuration value for key \"luau\" must be a table";
            return false;
        }
        static const std::pair<const char*, const char*> KEYS[] = {
            { "languagemode", "languageMode" }, { "lint", "lint" },       { "linterrors", "lintErrors" },
            { "typeerrors", "typeErrors" },     { "globals", "globals" }, { "aliases", "aliases" },
        };
        json = "{";
        bool first = true;
        for (const auto& [key, named] : KEYS)
        {
            lua_rawgetfield(L, -1, key);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                continue;
            }
            json += first ? "" : ", ";
            first = false;
            quoted(json, named);
            json += ": ";
            if (!write(L, json, error, /*as_array*/ std::string_view(key) == "globals", 0))
            {
                return false;
            }
        }
        json += "}";
        return true;
    }

    bool run(const std::string& source, std::string& json, std::string& error)
    {
        Limits limits;
        limits.deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<F64>(ALLuauConfigScript::MOST_SECONDS));
        std::unique_ptr<lua_State, void (*)(lua_State*)> state(lua_newstate(allocate, &limits), lua_close);
        lua_State* L = state.get();
        if (!L)
        {
            error = "not enough memory";
            return false;
        }
        openLibraries(L);
        luaL_sandbox(L);
        const std::string bytecode = Luau::compile(source);
        if (luau_load(L, "=config", bytecode.data(), bytecode.size(), 0) != 0)
        {
            error = lua_tostring(L, -1);
            return false;
        }
        lua_callbacks(L)->userdata  = &limits;
        lua_callbacks(L)->interrupt = interrupt;
        switch (lua_resume(L, nullptr, 0))
        {
            case LUA_OK:
                break;
            case LUA_BREAK:
            case LUA_YIELD:
                error = "configuration execution cannot yield";
                return false;
            default:
                error = lua_isstring(L, -1) ? lua_tostring(L, -1) : "configuration execution failed";
                return false;
        }
        // Nothing runs from here: the table is only read, raw.
        lua_callbacks(L)->interrupt = nullptr;
        limits.capped               = false;
        if (lua_gettop(L) != 1)
        {
            error = "configuration must return exactly one value";
            return false;
        }
        if (!lua_istable(L, -1))
        {
            error = "configuration did not return a table";
            return false;
        }
        return luaurcOf(L, json, error);
    }

    // What each text came to, the latest asked first.
    struct Ran
    {
        std::string source;
        bool        ok = false;
        std::string said;
    };
    constexpr size_t RAN_KEPT = 32;
    std::mutex       sRanMutex;
    std::vector<Ran> sRan;
}

// static
bool ALLuauConfigScript::asLuaurc(const std::string& source, std::string& json, std::string& error)
{
    {
        const std::lock_guard<std::mutex> lock(sRanMutex);
        const auto found = std::find_if(sRan.begin(), sRan.end(), [&source](const Ran& ran) { return ran.source == source; });
        if (found != sRan.end())
        {
            std::rotate(sRan.begin(), found, found + 1);
            (sRan.front().ok ? json : error) = sRan.front().said;
            return sRan.front().ok;
        }
    }
    Ran ran;
    ran.source = source;
    ran.ok     = run(source, json, error);
    ran.said   = ran.ok ? json : error;
    const std::lock_guard<std::mutex> lock(sRanMutex);
    sRan.insert(sRan.begin(), std::move(ran));
    if (sRan.size() > RAN_KEPT)
    {
        sRan.pop_back();
    }
    return sRan.front().ok;
}
