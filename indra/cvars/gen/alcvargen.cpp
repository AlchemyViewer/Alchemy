/**
 * @file alcvargen.cpp
 * @brief The settings schema's generator.
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

#include "alcvargen.h"
#include "alcvargenjson.h"

#include "alcvarconvertible.h"
#include "alcvartype.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>

static_assert(std::endian::native == std::endian::little, "slots are laid out little-endian");

namespace ALCVarGen
{
    namespace
    {
        namespace fs = std::filesystem;

        // ---- the rules' word lists --------------------------------------

        // A setting's first segment: what it is about.
        const std::set<std::string, std::less<>> AREAS = { "render", "camera", "avatar", "world",  "build",
                                                           "inventory", "chat", "im", "voice", "audio",
                                                           "media", "ui", "input", "net", "privacy",
                                                           "script", "snapshot", "rlva", "login", "update",
                                                           "perf", "debug" };

        const std::set<std::string, std::less<>> KEYWORDS = {
            "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case",
            "catch", "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval",
            "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
            "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
            "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
            "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
            "public", "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
            "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true",
            "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
            "wchar_t", "while", "xor", "xor_eq", "final", "override", "import", "module"
        };

        // Macros some platform the viewer builds for defines in lower case:
        // windows.h, macOS's AssertMacros.h, glibc, the C library.
        const std::set<std::string, std::less<>> MACROS = { "near",   "far",    "small",  "interface", "check",
                                                            "verify", "require", "major", "minor",     "min",
                                                            "max",    "errno",  "assert", "stdin",     "stdout",
                                                            "stderr", "unix",   "linux",  "pascal",    "cdecl",
                                                            "nil",    "alloca", "offsetof", "setjmp",  "environ" };

        // What the generator writes into a group's namespace itself.
        const std::set<std::string, std::less<>> GENERATED = { "read", "group", "all" };

        // A setting's fields, each with what it says, as an editor shows it.
        const std::map<std::string, std::string, std::less<>> SETTING_FIELDS = {
            { "type", "What the setting holds. Required." },
            { "default", "Its value until the user sets one, in its type. Required." },
            { "help", "What it does, as its tooltip says it. Required." },
            { "label", "Its name in the interface; the key in words where left out." },
            { "scope", "Whose value it is: the machine's, the account's, or the session's alone, never stored. "
                       "Its group's where left out." },
            { "range", "[lowest, highest]: the values it may hold; one outside is clamped." },
            { "ui_range", "[lowest, highest]: what a slider or spinner offers, within range." },
            { "ui_step", "A slider's or spinner's increment." },
            { "unit", "What its number measures. A unit changed is a step of the lock." },
            { "choices", "The values it may take, each with its label: { value = \"label\", ... }, or "
                         "[{ value = ..., label = \"...\", was = [...] }, ...], `was` naming the values that became it." },
            { "flags", "How it is shown and kept." },
            { "tags", "Generations that move whenever a setting carrying the tag changes: lower_snake names." },
            { "validate", "The registered validator every write of it runs." },
            { "alias", "An old name it was stored under: \"Name\", or { name = \"...\", inverted, encoding = \"string\", "
                       "joined = \"<separator>\", unit }, or a list of them." },
            { "origin", "Where it came from. Its group's where left out." },
            { "migrate", "What became of stored values at a step of the lock: a list, an entry a step, "
                         "[{ step = <n>, map = { old = new, ... }, else = \"keep\" | \"default\" | <value> }], scale and offset, "
                         "reset = true, fn = \"<hook>\", or join = { \"<decl>\" = { ... } } to end a fork." },
            { "backup", "Whether a backup takes it. Its group's where left out." },
            { "preset", "The kind of preset that takes it. Its group's where left out." },
            { "asset", "The kind of file a string names: its value is a bare file name." },
            { "refers", "What a UUID refers to. Required for a UUID." },
            { "from", "Settings it takes its first value from where it has none of its own: "
                      "[{ name = \"<setting>\", fn = \"<hook>\" }, ...]." },
            { "reuse", "Declares again a name renamed away or retired, as a history of its own that its old values "
                       "never meet." },
        };

        const std::map<std::string, std::string, std::less<>> GROUP_FIELDS = {
            { "label", "The group's name in the interface; the key in words where left out." },
            { "help", "What the group's settings are for." },
            { "scope", "Whose values its settings are, where a setting does not say." },
            { "backup", "Whether a backup takes its settings, where a setting does not say." },
            { "preset", "The kind of preset that takes its settings, where a setting does not say." },
            { "origin", "Where its settings came from, where a setting does not say." },
        };

        // Where a setting came from is its origin, not part of its name.
        const std::set<std::string, std::less<>> ORIGIN_WORDS = { "alchemy", "al", "fs", "ll", "firestorm",
                                                                  "linden" };

        // The first word of a boolean that says what false means.
        const std::set<std::string, std::less<>> NEGATIVE_WORDS = { "disable", "disabled", "hide", "no",
                                                                    "skip",    "dont",     "not" };

        // Units, which belong in the unit field, not at the end of a name.
        const std::set<std::string, std::less<>> UNIT_WORDS = {
            "ms",  "msec",    "sec",    "secs",   "seconds", "minutes", "hours",   "metres", "meters", "km",
            "px",  "pixels",  "pct",    "percent", "deg",    "degrees", "rad",     "radians", "kb",    "mb",
            "gb",  "bytes",   "hz"
        };

        // One word for one thing: the word a name uses, for the words it
        // might have used instead.
        const std::map<std::string, std::string, std::less<>> GLOSSARY = {
            { "enable", "enabled" },        { "seperator", "separator" }, { "colour", "color" },
            { "centre", "center" },         { "initialise", "initialize" }
        };

        const std::set<std::string, std::less<>> ABBREVIATIONS = {
            "aa", "ao", "cas", "cvd", "dof", "ev", "fov", "fps", "gpu", "hdr", "hud", "http", "im", "lod", "lut",
            "ssao", "ssr", "ui", "url", "uuid", "vram", "lsl", "ssl", "tls", "udp", "tcp", "ip", "id", "xml",
            "json", "smaa", "fxaa", "pbr", "gltf", "rlva", "leap", "cpu", "rgb", "srgb", "uv", "ik"
        };

        const std::set<std::string, std::less<>> FILE_EXTENSIONS = { "cube", "png", "jpg", "jpeg", "tga", "bmp",
                                                                     "webp", "xml", "txt", "lsl", "luau", "wav",
                                                                     "ogg", "mp3", "dae", "gltf", "glb", "json" };

        constexpr size_t MAX_SEGMENT = 32;
        constexpr size_t MAX_NAME    = 64;

        // ---- the model --------------------------------------------------

        // A place in a file: its line and column, from 1. A column of 0 is
        // not known, and a line of 0 is the whole file.
        struct Where
        {
            U32 line   = 0;
            U32 column = 0;

            Where() = default;
            Where(U32 at, U32 col = 0) : line(at), column(col) {}

            bool operator==(const Where&) const = default;
            bool operator<(const Where& other) const
            {
                return line != other.line ? line < other.line : column < other.column;
            }
        };

        struct Choice
        {
            S64                      number = 0;
            std::string              text;
            std::string              label;
            std::vector<S64>         wasNumbers;
            std::vector<std::string> wasTexts;
        };

        struct Alias
        {
            std::string name;
            bool        inverted      = false;
            bool        stringEncoded = false;
            std::string joined;
            ALCVarUnit  unit = ALCVarUnit::None;
            std::string file;
            Where       line = 0;
        };

        // A map's entry: its key as written, and as a number where it reads
        // as one; and its value, a number or a string.
        struct MapEntry
        {
            std::string        from;
            std::optional<F64> fromNumber;
            std::optional<F64> toNumber;
            std::string        toText;
        };

        // What a step's migrate does to a value of the step before it.
        struct Answer
        {
            ALCVarMigrateKind                 kind      = ALCVarMigrateKind::None;
            std::vector<MapEntry>             map;
            ALCVarMapElse                     otherwise = ALCVarMapElse::Keep;
            bool                              hasElse   = false;
            F64                               elseValue = 0.0;
            // A string's else, which a number's is not.
            std::optional<std::string>        elseText;
            F64                               scale     = 1.0;
            F64                               offset    = 0.0;
            std::string                       hook;
        };

        struct Migrate : Answer
        {
            // The step it begins.
            S32                                step = 0;
            // A join's answers, each to a step of the other branch.
            std::vector<std::pair<U64, Answer>> join;
            Where                              line = 0;
        };

        // A setting another takes its first value from.
        struct From
        {
            std::string name;
            std::string hook;
            Where       line = 0;
        };

        // A choice as a step records it: its value, and the values that
        // became it.
        struct LockChoice
        {
            S64                      number = 0;
            std::string              text;
            std::vector<S64>         wasNumbers;
            std::vector<std::string> wasTexts;
        };

        // A line of the lock, or one the schema adds to it: a step of a
        // setting's history, what its stored values meant from it on, or
        // the line that ends a name's chain.
        struct Step
        {
            enum class Kind : U8
            {
                Step,
                Renamed,
                Retired,
            };
            Kind                    kind   = Kind::Step;
            std::string             name;
            S32                     number = 0;
            U64                     decl   = 0;
            // The fingerprint of the step it follows, "" for none.
            std::string             parent;
            ALCVarType              type   = ALCVarType::Bool;
            ALCVarUnit              unit   = ALCVarUnit::None;
            std::vector<LockChoice> choices;
            // The hash of its migrate, "" for none.
            std::string             migrate;
            std::string             renamedTo;
            // The old name of the setting it was written under, by its
            // place among the setting's aliases; -1 for its own.
            S16                     alias  = -1;
            // The line as the lock holds it, kept byte for byte, and where;
            // empty for a line the lock does not hold yet.
            std::string             text;
            U32                     line = 0;
        };

        // A step of a fork's other branch, and what the join makes of it.
        struct Joined
        {
            U64        decl = 0;
            ALCVarType type = ALCVarType::Bool;
            ALCVarUnit unit = ALCVarUnit::None;
            Answer     answer;
        };

        struct Setting;

        // A source of `from`, with the steps its rows are brought through,
        // and its declaration where it has one, whose migrates take them.
        struct Source
        {
            From              from;
            std::vector<Step> steps;
            const Setting*    declared = nullptr;
        };

        struct Group;

        struct Setting
        {
            std::string  name;
            std::string  key;
            Group*       group = nullptr;
            std::string  file;
            Where        line  = 0;
            ALCVarType   type  = ALCVarType::Bool;
            ALCVarScope  scope = ALCVarScope::Global;
            ALCVarUnit   unit  = ALCVarUnit::None;
            ALCVarBackup backup = ALCVarBackup::Yes;
            ALCVarPreset preset = ALCVarPreset::None;
            ALCVarRefers refers = ALCVarRefers::None;
            ALCVarOrigin origin = ALCVarOrigin::Alchemy;
            bool         hasRefers = false;
            bool         hasBackup = false;
            bool         hasPreset = false;
            bool         hasScope  = false;
            bool         hasOrigin = false;
            U16          flags = 0;
            std::string  label;
            std::string  help;
            U64          words[3] = { 0, 0, 0 };
            // A string's default; a list's or LLSD's as JSON.
            std::string  text;
            // The default as cvars.json gives it.
            std::string  json;
            bool         ranged   = false;
            bool         uiRanged = false;
            F64          rangeMin = 0, rangeMax = 0, uiMin = 0, uiMax = 0, uiStep = 0;
            std::vector<Choice>      choices;
            std::vector<Alias>       aliases;
            std::vector<std::string> tags;
            std::string  validate;
            std::string  asset;
            // By step.
            std::vector<Migrate> migrates;
            std::vector<From>    from;
            // A name renamed away or retired, declared again with a chain
            // of its own.
            bool                 reuse = false;
            // Its history: the lock's steps, its old names' before them,
            // and the one the declaration is now if the lock does not record
            // it yet.
            std::vector<Step>    steps;
            // The steps of the setting a reused name had before.
            std::vector<U64>     superseded;
            // The steps of a fork's other branch a join answers, by the
            // step the join begins.
            std::map<S32, std::vector<Joined>> joined;
            std::vector<Source>  sources;
            U16          keyIndex = 0;
            U16          slot     = 0;
        };

        struct Group
        {
            std::string           name;
            std::string           key;
            std::string           parentName;
            Group*                parent = nullptr;
            std::string           file;
            Where                 line = 0;
            std::string           label;
            std::string           help;
            std::optional<ALCVarScope>  scope;
            std::optional<ALCVarBackup> backup;
            std::optional<ALCVarPreset> preset;
            std::optional<ALCVarOrigin> origin;
            std::vector<Setting*> settings;
            std::vector<Group*>   children;
        };

        struct Schema
        {
            std::vector<std::unique_ptr<Group>>   groups;
            std::vector<std::unique_ptr<Setting>> settings;
            std::map<std::string, Group*, std::less<>>   groupByName;
            std::map<std::string, Setting*, std::less<>> settingByName;
            std::map<std::string, std::string, std::less<>> areaFile;
            std::set<std::string, std::less<>> words;
        };

        // What the run says of a place in a file: of a setting or a group
        // where it names one.
        struct Message
        {
            std::string file;
            Where       at;
            std::string name;
            std::string text;
        };

        struct Diagnostics
        {
            std::vector<Message> errors;

            void error(const std::string& file, Where at, std::string_view name, const std::string& what)
            {
                errors.push_back({ file, at, std::string(name), what });
            }
        };

        // A message as a compiler's, which an IDE lists and opens at its
        // place: "file:line:col: error: name: text", or Visual Studio's
        // "file(line,col): error CV0001: name: text". The path is absolute,
        // as a build's is not run where its files are; a file's whole is
        // its path alone, and the generator's own say has none.
        std::string said(const Message& message, std::string_view severity, bool msvc)
        {
            std::string at;
            if (!message.file.empty())
            {
                fs::path            path = fs::absolute(fs::path(message.file)).lexically_normal();
                const std::u8string text = msvc ? path.make_preferred().u8string() : path.generic_u8string();
                at                       = std::string(text.begin(), text.end());
                const U32 column = message.at.column ? message.at.column : 1;
                if (message.at.line && msvc)
                {
                    at += "(" + std::to_string(message.at.line) + "," + std::to_string(column) + ")";
                }
                else if (message.at.line)
                {
                    at += ":" + std::to_string(message.at.line) + ":" + std::to_string(column);
                }
            }
            else
            {
                at = "alcvargen";
            }
            std::string text = at + (msvc && !message.at.line ? " : " : ": ") + std::string(severity);
            if (msvc && severity != "note")
            {
                text += " CV0001";
            }
            text += ": ";
            if (!message.name.empty())
            {
                text += message.name + ": ";
            }
            return text + message.text;
        }

        std::vector<std::string> said(const std::vector<Message>& messages, std::string_view severity, bool msvc)
        {
            std::vector<std::string> out;
            for (const Message& message : messages)
            {
                out.push_back(said(message, severity, msvc));
            }
            return out;
        }

        // The word of `words` that `word` most nearly is, as a misspelling
        // is: within a letter or two, a transposition one, case ignored.
        // "" where none is near.
        template <typename Words>
        std::string nearest(std::string_view word, const Words& words)
        {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; };
            auto distance = [&](std::string_view a, std::string_view b) {
                std::vector<std::vector<size_t>> d(a.size() + 1, std::vector<size_t>(b.size() + 1));
                for (size_t i = 0; i <= a.size(); ++i)
                {
                    d[i][0] = i;
                }
                for (size_t j = 0; j <= b.size(); ++j)
                {
                    d[0][j] = j;
                }
                for (size_t i = 1; i <= a.size(); ++i)
                {
                    for (size_t j = 1; j <= b.size(); ++j)
                    {
                        const size_t cost = lower(a[i - 1]) == lower(b[j - 1]) ? 0 : 1;
                        d[i][j]           = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
                        if (i > 1 && j > 1 && lower(a[i - 1]) == lower(b[j - 2]) && lower(a[i - 2]) == lower(b[j - 1]))
                        {
                            d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
                        }
                    }
                }
                return d[a.size()][b.size()];
            };
            const size_t most = std::max<size_t>(1, std::min<size_t>(2, word.size() / 3));
            std::string  best;
            size_t       bestDistance = most + 1;
            for (const auto& candidate : words)
            {
                const std::string_view text = candidate;
                const size_t           d    = distance(word, text);
                if (d < bestDistance && text != word)
                {
                    best         = std::string(text);
                    bestDistance = d;
                }
            }
            return best;
        }

        // "; did you mean 'x'?", or "".
        template <typename Words>
        std::string guess(std::string_view word, const Words& words)
        {
            const std::string close = nearest(word, words);
            return close.empty() ? std::string() : "; did you mean '" + close + "'?";
        }

        template <typename Map>
        std::vector<std::string> keysOf(const Map& map)
        {
            std::vector<std::string> keys;
            for (const auto& [key, value] : map)
            {
                keys.push_back(key);
            }
            return keys;
        }

        // ---- small helpers ----------------------------------------------

        std::vector<std::string> split(std::string_view text, char separator)
        {
            std::vector<std::string> out;
            size_t                   start = 0;
            for (;;)
            {
                const size_t at = text.find(separator, start);
                out.emplace_back(text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start));
                if (at == std::string_view::npos)
                {
                    return out;
                }
                start = at + 1;
            }
        }

        std::string join(const std::vector<std::string>& parts, std::string_view separator)
        {
            std::string out;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (i)
                {
                    out += separator;
                }
                out += parts[i];
            }
            return out;
        }

        U64 fnv1a(std::string_view text)
        {
            U64 hash = 0xcbf29ce484222325ull;
            for (const char c : text)
            {
                hash ^= static_cast<unsigned char>(c);
                hash *= 0x100000001b3ull;
            }
            return hash;
        }

        std::string number(F64 value)
        {
            char text[40];
            std::snprintf(text, sizeof(text), "%.17g", value);
            std::string out = text;
            if (out.find_first_of(".en") == std::string::npos)
            {
                out += ".0";
            }
            return out;
        }

        // A number of a setting's type, as a message gives it: an integer's
        // as an integer.
        std::string valueText(ALCVarType type, F64 value)
        {
            if (ALCVarTypes::isInteger(type) && value == std::floor(value) && std::fabs(value) < 9.0e18)
            {
                return std::to_string(static_cast<long long>(value));
            }
            return number(value);
        }

        // A number as a person writes it: a whole one with no point.
        std::string compact(F64 value)
        {
            return value == std::floor(value) && std::fabs(value) < 9.0e18 ? std::to_string(static_cast<long long>(value))
                                                                            : number(value);
        }

        std::string numberF32(F32 value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.9g", double(value));
            return text;
        }

        std::string hex64(U64 value)
        {
            char text[24];
            std::snprintf(text, sizeof(text), "0x%016llxull", static_cast<unsigned long long>(value));
            return text;
        }

        // A C++ string literal, every byte outside printable ASCII escaped in
        // octal, which never runs into the characters after it.
        std::string cppString(std::string_view text)
        {
            std::string out = "\"";
            for (const char c : text)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (c == '"' || c == '\\')
                {
                    out += '\\';
                    out += c;
                }
                else if (u < 0x20 || u >= 0x7f || c == '?')
                {
                    char escape[8];
                    std::snprintf(escape, sizeof(escape), "\\%03o", unsigned(u));
                    out += escape;
                }
                else
                {
                    out += c;
                }
            }
            out += '"';
            return out;
        }

        std::string xmlText(std::string_view text)
        {
            std::string out;
            for (const char c : text)
            {
                switch (c)
                {
                    case '<': out += "&lt;"; break;
                    case '>': out += "&gt;"; break;
                    case '&': out += "&amp;"; break;
                    case '"': out += "&quot;"; break;
                    default:  out += c;
                }
            }
            return out;
        }

        // A setting's name as one C++ identifier, the letter after each dot
        // made upper case: a segment starts with a lower-case letter and holds
        // no upper-case one, so render.shadow.map_size and
        // render.shadow_map.size stay apart.
        std::string identifier(std::string_view name)
        {
            std::string out;
            bool        dot = false;
            for (const char c : name)
            {
                if (c == '.')
                {
                    dot = true;
                    continue;
                }
                out += dot && c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
                dot = false;
            }
            return out;
        }

        std::string labelFor(std::string_view key)
        {
            std::string out(key);
            std::replace(out.begin(), out.end(), '_', ' ');
            if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
            {
                out[0] = char(out[0] - 'a' + 'A');
            }
            return out;
        }

        std::string namespaceOf(const Options& options, std::string_view groupName)
        {
            std::string out = options.root;
            for (const auto& segment : split(groupName, '.'))
            {
                out += "::" + segment;
            }
            return out;
        }

        // The header a group's handles are in, as included.
        std::string headerOf(const Options& options, std::string_view groupName)
        {
            std::string out = options.includeDir;
            for (const auto& segment : split(groupName, '.'))
            {
                out += "/" + segment;
            }
            return out + ".h";
        }

        std::string cppType(ALCVarType type)
        {
            switch (type)
            {
                case ALCVarType::Bool:       return "bool";
                case ALCVarType::S32:        return "S32";
                case ALCVarType::U32:        return "U32";
                case ALCVarType::F32:        return "F32";
                case ALCVarType::S64:        return "S64";
                case ALCVarType::F64:        return "F64";
                case ALCVarType::String:     return "std::string";
                case ALCVarType::UUID:       return "LLUUID";
                case ALCVarType::Vec2:       return "LLVector2";
                case ALCVarType::Vec3:       return "LLVector3";
                case ALCVarType::Vec3d:      return "LLVector3d";
                case ALCVarType::Quat:       return "LLQuaternion";
                case ALCVarType::Rect:       return "LLRect";
                case ALCVarType::Color3:     return "LLColor3";
                case ALCVarType::Color4:     return "LLColor4";
                case ALCVarType::UUIDList:   return "std::vector<LLUUID>";
                case ALCVarType::StringList: return "std::vector<std::string>";
                case ALCVarType::Vec2List:   return "std::vector<LLVector2>";
                case ALCVarType::LLSD:       return "LLSD";
            }
            return "";
        }

        // ---- enums by their schema spelling -----------------------------

        template <typename E, size_t N>
        std::optional<E> lookup(const std::array<std::pair<std::string_view, E>, N>& table, std::string_view name)
        {
            for (const auto& [text, value] : table)
            {
                if (text == name)
                {
                    return value;
                }
            }
            return std::nullopt;
        }

        template <typename E, size_t N>
        std::string_view spell(const std::array<std::pair<std::string_view, E>, N>& table, E value)
        {
            for (const auto& [text, entry] : table)
            {
                if (entry == value)
                {
                    return text;
                }
            }
            return "";
        }

        template <typename E, size_t N>
        std::string choicesOf(const std::array<std::pair<std::string_view, E>, N>& table)
        {
            std::vector<std::string> names;
            for (const auto& [text, value] : table)
            {
                names.emplace_back(text);
            }
            return join(names, ", ");
        }

        constexpr std::array<std::pair<std::string_view, ALCVarScope>, 3> SCOPES = {
            { { "global", ALCVarScope::Global }, { "account", ALCVarScope::Account }, { "runtime", ALCVarScope::Runtime } }
        };
        constexpr std::array<std::pair<std::string_view, ALCVarBackup>, 4> BACKUPS = {
            { { "yes", ALCVarBackup::Yes },
              { "machine", ALCVarBackup::Machine },
              { "ask", ALCVarBackup::Ask },
              { "never", ALCVarBackup::Never } }
        };
        constexpr std::array<std::pair<std::string_view, ALCVarPreset>, 4> PRESETS = {
            { { "none", ALCVarPreset::None },
              { "look", ALCVarPreset::Look },
              { "scene", ALCVarPreset::Scene },
              { "camera", ALCVarPreset::Camera } }
        };
        constexpr std::array<std::pair<std::string_view, ALCVarRefers>, 8> REFERS = {
            { { "none", ALCVarRefers::None },
              { "sound", ALCVarRefers::Sound },
              { "texture", ALCVarRefers::Texture },
              { "material", ALCVarRefers::Material },
              { "inventory_folder", ALCVarRefers::InventoryFolder },
              { "inventory_item", ALCVarRefers::InventoryItem },
              { "object", ALCVarRefers::Object },
              { "experience", ALCVarRefers::Experience } }
        };
        constexpr std::array<std::pair<std::string_view, ALCVarOrigin>, 4> ORIGINS = {
            { { "alchemy", ALCVarOrigin::Alchemy },
              { "linden", ALCVarOrigin::Linden },
              { "firestorm", ALCVarOrigin::Firestorm },
              { "rlva", ALCVarOrigin::RLVa } }
        };
        constexpr std::array<std::pair<std::string_view, ALCVarFlag>, 6> FLAGS = {
            { { "hidden", ALCVAR_HIDDEN },
              { "restart", ALCVAR_RESTART },
              { "developer", ALCVAR_DEVELOPER },
              { "sensitive", ALCVAR_SENSITIVE },
              { "dangerous", ALCVAR_DANGEROUS },
              { "account_override", ALCVAR_ACCOUNT_OVERRIDE } }
        };

        std::string_view scopeName(ALCVarScope v) { return spell(SCOPES, v); }
        std::string_view backupName(ALCVarBackup v) { return spell(BACKUPS, v); }
        std::string_view presetName(ALCVarPreset v) { return spell(PRESETS, v); }
        std::string_view refersName(ALCVarRefers v) { return spell(REFERS, v); }
        std::string_view originName(ALCVarOrigin v) { return spell(ORIGINS, v); }

        std::string enumScope(ALCVarScope v)
        {
            switch (v)
            {
                case ALCVarScope::Global:  return "ALCVarScope::Global";
                case ALCVarScope::Account: return "ALCVarScope::Account";
                case ALCVarScope::Runtime: return "ALCVarScope::Runtime";
            }
            return "";
        }
        std::string enumBackup(ALCVarBackup v)
        {
            switch (v)
            {
                case ALCVarBackup::Yes:     return "ALCVarBackup::Yes";
                case ALCVarBackup::Machine: return "ALCVarBackup::Machine";
                case ALCVarBackup::Ask:     return "ALCVarBackup::Ask";
                case ALCVarBackup::Never:   return "ALCVarBackup::Never";
            }
            return "";
        }
        std::string enumPreset(ALCVarPreset v)
        {
            switch (v)
            {
                case ALCVarPreset::None:   return "ALCVarPreset::None";
                case ALCVarPreset::Look:   return "ALCVarPreset::Look";
                case ALCVarPreset::Scene:  return "ALCVarPreset::Scene";
                case ALCVarPreset::Camera: return "ALCVarPreset::Camera";
            }
            return "";
        }
        std::string enumRefers(ALCVarRefers v)
        {
            switch (v)
            {
                case ALCVarRefers::None:            return "ALCVarRefers::None";
                case ALCVarRefers::Sound:           return "ALCVarRefers::Sound";
                case ALCVarRefers::Texture:         return "ALCVarRefers::Texture";
                case ALCVarRefers::Material:        return "ALCVarRefers::Material";
                case ALCVarRefers::InventoryFolder: return "ALCVarRefers::InventoryFolder";
                case ALCVarRefers::InventoryItem:   return "ALCVarRefers::InventoryItem";
                case ALCVarRefers::Object:          return "ALCVarRefers::Object";
                case ALCVarRefers::Experience:      return "ALCVarRefers::Experience";
            }
            return "";
        }
        std::string enumOrigin(ALCVarOrigin v)
        {
            switch (v)
            {
                case ALCVarOrigin::Alchemy:   return "ALCVarOrigin::Alchemy";
                case ALCVarOrigin::Linden:    return "ALCVarOrigin::Linden";
                case ALCVarOrigin::Firestorm: return "ALCVarOrigin::Firestorm";
                case ALCVarOrigin::RLVa:      return "ALCVarOrigin::RLVa";
            }
            return "";
        }
        std::string enumType(ALCVarType type)
        {
            switch (type)
            {
                case ALCVarType::Bool:       return "ALCVarType::Bool";
                case ALCVarType::S32:        return "ALCVarType::S32";
                case ALCVarType::U32:        return "ALCVarType::U32";
                case ALCVarType::F32:        return "ALCVarType::F32";
                case ALCVarType::S64:        return "ALCVarType::S64";
                case ALCVarType::F64:        return "ALCVarType::F64";
                case ALCVarType::String:     return "ALCVarType::String";
                case ALCVarType::UUID:       return "ALCVarType::UUID";
                case ALCVarType::Vec2:       return "ALCVarType::Vec2";
                case ALCVarType::Vec3:       return "ALCVarType::Vec3";
                case ALCVarType::Vec3d:      return "ALCVarType::Vec3d";
                case ALCVarType::Quat:       return "ALCVarType::Quat";
                case ALCVarType::Rect:       return "ALCVarType::Rect";
                case ALCVarType::Color3:     return "ALCVarType::Color3";
                case ALCVarType::Color4:     return "ALCVarType::Color4";
                case ALCVarType::UUIDList:   return "ALCVarType::UUIDList";
                case ALCVarType::StringList: return "ALCVarType::StringList";
                case ALCVarType::Vec2List:   return "ALCVarType::Vec2List";
                case ALCVarType::LLSD:       return "ALCVarType::LLSD";
            }
            return "";
        }
        // Each unit's enumerator, beside the unit it names: a unit added to
        // the enum or moved in it, and not here, fails the build rather than
        // writing every later unit as the one before it.
        struct UnitEnumerator
        {
            ALCVarUnit       unit;
            std::string_view name;
        };
        constexpr UnitEnumerator UNIT_ENUMERATORS[] = {
            { ALCVarUnit::None, "None" },
            { ALCVarUnit::Metres, "Metres" },
            { ALCVarUnit::Kilometres, "Kilometres" },
            { ALCVarUnit::Centimetres, "Centimetres" },
            { ALCVarUnit::Seconds, "Seconds" },
            { ALCVarUnit::Milliseconds, "Milliseconds" },
            { ALCVarUnit::Microseconds, "Microseconds" },
            { ALCVarUnit::Minutes, "Minutes" },
            { ALCVarUnit::Hours, "Hours" },
            { ALCVarUnit::Days, "Days" },
            { ALCVarUnit::Degrees, "Degrees" },
            { ALCVarUnit::Radians, "Radians" },
            { ALCVarUnit::Percent, "Percent" },
            { ALCVarUnit::Ratio, "Ratio" },
            { ALCVarUnit::Bytes, "Bytes" },
            { ALCVarUnit::Kilobytes, "Kilobytes" },
            { ALCVarUnit::Megabytes, "Megabytes" },
            { ALCVarUnit::Gigabytes, "Gigabytes" },
            { ALCVarUnit::Hertz, "Hertz" },
            { ALCVarUnit::Pixels, "Pixels" },
            { ALCVarUnit::Points, "Points" },
            { ALCVarUnit::EV, "EV" },
            { ALCVarUnit::Kelvin, "Kelvin" },
            { ALCVarUnit::FPS, "FPS" },
            { ALCVarUnit::Decibels, "Decibels" },
        };
        static_assert(std::size(UNIT_ENUMERATORS) == size_t(ALCVarUnit::Count), "a unit has no enumerator name");
        static_assert(
            [] {
                for (size_t i = 0; i < std::size(UNIT_ENUMERATORS); ++i)
                {
                    if (UNIT_ENUMERATORS[i].unit != ALCVarUnit(i))
                    {
                        return false;
                    }
                }
                return true;
            }(),
            "the unit enumerators are not in the enum's order");

        std::string enumUnit(ALCVarUnit unit)
        {
            return "ALCVarUnit::" + std::string(UNIT_ENUMERATORS[size_t(unit)].name);
        }

        // ---- reading the TOML -------------------------------------------

        Where whereOf(const toml::node& node) { return { U32(node.source().begin.line), U32(node.source().begin.column) }; }
        Where whereOf(const toml::key& key) { return { U32(key.source().begin.line), U32(key.source().begin.column) }; }

        bool isUUID(std::string_view text)
        {
            if (text.size() != 36)
            {
                return false;
            }
            for (size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (i == 8 || i == 13 || i == 18 || i == 23)
                {
                    if (c != '-')
                    {
                        return false;
                    }
                }
                else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                {
                    return false;
                }
            }
            return true;
        }

        std::string lowered(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = char(c - 'A' + 'a');
                }
            }
            return out;
        }

        std::array<U8, 16> uuidBytes(std::string_view text)
        {
            std::array<U8, 16> bytes{};
            size_t             at = 0;
            auto               hex = [](char c) {
                return U8(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
            };
            for (size_t i = 0; i < text.size() && at < 16; ++i)
            {
                if (text[i] == '-')
                {
                    continue;
                }
                bytes[at++] = U8((hex(text[i]) << 4) | hex(text[i + 1]));
                ++i;
            }
            return bytes;
        }

        bool finite(F64 value)
        {
            const U64 bits = std::bit_cast<U64>(value);
            return (bits & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
        }

        // A number the schema gives: TOML's inf and nan are none, as no
        // range, step, scale or map can hold one and C++ has no literal for
        // it.
        std::optional<F64> numberOf(const toml::node& node)
        {
            if (const auto* i = node.as_integer())
            {
                return F64(i->get());
            }
            if (const auto* f = node.as_floating_point(); f && finite(f->get()))
            {
                return f->get();
            }
            return std::nullopt;
        }

        class Loader
        {
        public:
            Loader(const Options& options, Schema& schema, Diagnostics& diagnostics)
            :   mOptions(options),
                mSchema(schema),
                mDiag(diagnostics)
            {
            }

            void load(const std::string& path)
            {
                mFile = fs::path(path).generic_string();
                std::ifstream in(fs::path(path), std::ios::binary);
                if (!in)
                {
                    mDiag.error(mFile, 0, "", "cannot be read");
                    return;
                }
                std::stringstream text;
                text << in.rdbuf();
                const std::string source = text.str();
                scanHeaders(source);

                toml::table root;
                try
                {
                    root = toml::parse(source, std::string_view(mFile));
                }
                catch (const toml::parse_error& error)
                {
                    mDiag.error(mFile, { U32(error.source().begin.line), U32(error.source().begin.column) }, "",
                                std::string(error.description()));
                    return;
                }

                std::vector<std::pair<Where, std::string>> areas;
                for (auto&& [key, node] : root)
                {
                    const std::string name(key.str());
                    if (!node.is_table())
                    {
                        mDiag.error(mFile, whereOf(key), name, "a value outside any group");
                        continue;
                    }
                    areas.emplace_back(whereOf(key), name);
                }
                std::sort(areas.begin(), areas.end());
                for (const auto& [line, name] : areas)
                {
                    walk(*root[name].as_table(), name, nullptr, line);
                }
            }

        private:
            // The [name] headers the file has, and where: what the file
            // declares, as against the tables TOML makes implicitly for a
            // header's parents.
            void scanHeaders(const std::string& source)
            {
                mHeaders.clear();
                U32         line = 0;
                std::string row;
                std::istringstream lines(source);
                while (std::getline(lines, row))
                {
                    ++line;
                    size_t at = row.find_first_not_of(" \t");
                    if (at == std::string::npos || row[at] != '[' || (at + 1 < row.size() && row[at + 1] == '['))
                    {
                        continue;
                    }
                    const size_t close = row.find(']', at);
                    if (close == std::string::npos)
                    {
                        continue;
                    }
                    std::string name;
                    for (size_t i = at + 1; i < close; ++i)
                    {
                        const char c = row[i];
                        if (c != ' ' && c != '\t' && c != '"' && c != '\'')
                        {
                            name += c;
                        }
                    }
                    mHeaders.emplace(name, Where(line, U32(at + 1)));
                }
            }

            std::optional<Where> declared(const std::string& name) const
            {
                const auto found = mHeaders.find(name);
                return found == mHeaders.end() ? std::nullopt : std::optional<Where>(found->second);
            }

            static bool isSetting(const toml::table& table)
            {
                const toml::node* type = table.get("type");
                return type && type->is_string();
            }

            void walk(const toml::table& table, const std::string& name, Group* parent, Where line)
            {
                const auto header = declared(name);
                if (!parent)
                {
                    if (const auto area = mSchema.areaFile.find(name); area != mSchema.areaFile.end() && area->second != mFile)
                    {
                        Where first = line;
                        for (const auto& [declaredName, declaredLine] : mHeaders)
                        {
                            if (declaredName == name || declaredName.starts_with(name + "."))
                            {
                                first = std::min(first == line ? declaredLine : first, declaredLine);
                            }
                        }
                        mDiag.error(mFile, first, name, "the area " + name + " is declared in " + area->second +
                                                            "; one file an area");
                        return;
                    }
                }
                if (parent && header && *header < parent->line)
                {
                    mDiag.error(mFile, *header, name, "declared before its group " + parent->name);
                }
                if (isSetting(table))
                {
                    if (!parent)
                    {
                        mDiag.error(mFile, line, name, "a setting outside any group: declare its area first");
                        return;
                    }
                    if (!header)
                    {
                        mDiag.error(mFile, line, name, "declare each setting under a [" + name + "] header of its own");
                        return;
                    }
                    readSetting(table, name, *parent, *header);
                    return;
                }
                if (!header)
                {
                    mDiag.error(mFile, line, name,
                                "no group declares this: add a [" + name + "] table, with its label, before its settings");
                    return;
                }
                Group* group = readGroup(table, name, parent, *header);
                if (!group)
                {
                    return;
                }

                std::vector<std::pair<Where, std::string>> children;
                for (auto&& [key, node] : table)
                {
                    const std::string field(key.str());
                    if (const toml::table* child = node.as_table(); child && !child->is_inline())
                    {
                        const std::string childName = name + "." + field;
                        const auto        childHeader = declared(childName);
                        children.emplace_back(childHeader ? *childHeader : whereOf(key), field);
                    }
                }
                std::sort(children.begin(), children.end());
                for (const auto& [childLine, field] : children)
                {
                    const std::string childName = name + "." + field;
                    walk(*table[field].as_table(), childName, group, childLine);
                }
            }

            bool claim(const std::string& name, Where line)
            {
                const std::string area = split(name, '.')[0];
                const auto [at, added] = mSchema.areaFile.emplace(area, mFile);
                if (!added && at->second != mFile)
                {
                    mDiag.error(mFile, line, name, "the area " + area + " is declared in " + at->second +
                                                       "; one file an area");
                    return false;
                }
                if (const auto g = mSchema.groupByName.find(name); g != mSchema.groupByName.end())
                {
                    mDiag.error(mFile, line, name,
                                "already declared at " + g->second->file + ":" + std::to_string(g->second->line.line));
                    return false;
                }
                if (const auto s = mSchema.settingByName.find(name); s != mSchema.settingByName.end())
                {
                    mDiag.error(mFile, line, name,
                                "already declared at " + s->second->file + ":" + std::to_string(s->second->line.line));
                    return false;
                }
                return true;
            }

            template <typename E, size_t N>
            std::optional<E> readEnum(const toml::node& node, const std::array<std::pair<std::string_view, E>, N>& table,
                                      const std::string& name, std::string_view field, Where line)
            {
                const auto* text = node.as_string();
                if (text)
                {
                    if (auto value = lookup(table, text->get()))
                    {
                        return value;
                    }
                }
                std::vector<std::string_view> names;
                for (const auto& [spelling, value] : table)
                {
                    names.push_back(spelling);
                }
                mDiag.error(mFile, line, name,
                            std::string(field) + " is one of " + choicesOf(table) + (text ? guess(text->get(), names) : ""));
                return std::nullopt;
            }

            Group* readGroup(const toml::table& table, const std::string& name, Group* parent, Where line)
            {
                if (!claim(name, line))
                {
                    return nullptr;
                }
                auto group        = std::make_unique<Group>();
                group->name       = name;
                group->key        = split(name, '.').back();
                group->parent     = parent;
                group->parentName = parent ? parent->name : "";
                group->file       = mFile;
                group->line       = line;
                checkName(name, line, false, ALCVarType::Bool);

                for (auto&& [key, node] : table)
                {
                    const std::string field(key.str());
                    const Where       at = whereOf(key);
                    if (const toml::table* child = node.as_table(); child && !child->is_inline())
                    {
                        continue;
                    }
                    if (!GROUP_FIELDS.contains(field))
                    {
                        mDiag.error(mFile, at, name, "a group has no field '" + field + "'" + guess(field, keysOf(GROUP_FIELDS)));
                        continue;
                    }
                    if (field == "label" || field == "help")
                    {
                        const auto* text = node.as_string();
                        if (!text)
                        {
                            mDiag.error(mFile, at, name, field + " is a string");
                            continue;
                        }
                        (field == "label" ? group->label : group->help) = text->get();
                    }
                    else if (field == "scope")
                    {
                        group->scope = readEnum(node, SCOPES, name, field, at);
                    }
                    else if (field == "backup")
                    {
                        group->backup = readEnum(node, BACKUPS, name, field, at);
                    }
                    else if (field == "preset")
                    {
                        group->preset = readEnum(node, PRESETS, name, field, at);
                    }
                    else if (field == "origin")
                    {
                        group->origin = readEnum(node, ORIGINS, name, field, at);
                    }
                }
                if (group->label.empty())
                {
                    group->label = labelFor(group->key);
                }
                Group* raw = group.get();
                if (parent)
                {
                    parent->children.push_back(raw);
                }
                mSchema.groupByName.emplace(name, raw);
                mSchema.groups.push_back(std::move(group));
                return raw;
            }

            // ---- names --------------------------------------------------

            void checkName(const std::string& name, Where line, bool setting, ALCVarType type)
            {
                const auto segments = split(name, '.');
                if (name.size() > MAX_NAME)
                {
                    mDiag.error(mFile, line, name, "longer than " + std::to_string(MAX_NAME) + " characters");
                }
                if (!AREAS.contains(segments[0]))
                {
                    std::vector<std::string> areas(AREAS.begin(), AREAS.end());
                    mDiag.error(mFile, line, name,
                                "'" + segments[0] + "' is not an area: one of " + join(areas, ", ") + guess(segments[0], areas));
                }
                if (setting && segments.size() < 2)
                {
                    mDiag.error(mFile, line, name, "a setting's name is its group's and a key");
                }
                for (const auto& segment : segments)
                {
                    checkSegment(name, segment, line);
                }
                if (setting && type == ALCVarType::Bool)
                {
                    const auto words = split(segments.back(), '_');
                    if (NEGATIVE_WORDS.contains(words.front()))
                    {
                        mDiag.error(mFile, line, name,
                                    "booleans name what true means: '" + words.front() + "' says what false does");
                    }
                    if (segments.back() == "on" || segments.back() == "use" || segments.back() == "enable")
                    {
                        mDiag.error(mFile, line, name, "a switch is 'enabled'");
                    }
                }
            }

            void checkSegment(const std::string& name, const std::string& segment, Where line)
            {
                bool shaped = !segment.empty() && segment.size() <= MAX_SEGMENT && segment[0] >= 'a' && segment[0] <= 'z' &&
                              segment.back() != '_' && segment.find("__") == std::string::npos;
                for (const char c : segment)
                {
                    shaped = shaped && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
                }
                if (!shaped)
                {
                    mDiag.error(mFile, line, name,
                                "'" + segment + "' is not lower_snake: a letter, then letters, digits and single "
                                "underscores, at most " + std::to_string(MAX_SEGMENT) + " characters");
                    return;
                }
                if (KEYWORDS.contains(segment))
                {
                    mDiag.error(mFile, line, name, "'" + segment + "' is a C++ keyword");
                }
                if (MACROS.contains(segment))
                {
                    mDiag.error(mFile, line, name, "'" + segment + "' is a macro on a platform the viewer builds for");
                }
                if (GENERATED.contains(segment))
                {
                    mDiag.error(mFile, line, name, "'" + segment + "' is a name the generator writes itself");
                }
                if (SETTING_FIELDS.contains(segment))
                {
                    mDiag.error(mFile, line, name, "'" + segment + "' is a field's name");
                }
                for (const auto& word : split(segment, '_'))
                {
                    if (ORIGIN_WORDS.contains(word))
                    {
                        mDiag.error(mFile, line, name,
                                    "'" + word + "' says where a setting came from: that is its origin field");
                    }
                    if (const auto better = GLOSSARY.find(word); better != GLOSSARY.end())
                    {
                        mDiag.error(mFile, line, name, "'" + word + "' is '" + better->second + "' in a name");
                    }
                    if (!mSchema.words.empty() && !mSchema.words.contains(word) && !ABBREVIATIONS.contains(word) &&
                        !AREAS.contains(word) && !(word[0] >= '0' && word[0] <= '9'))
                    {
                        mDiag.error(mFile, line, name, "'" + word + "' is not a word the schema knows");
                    }
                }
            }

            // ---- settings -----------------------------------------------

            void readSetting(const toml::table& table, const std::string& name, Group& group, Where line)
            {
                if (!claim(name, line))
                {
                    return;
                }
                auto setting   = std::make_unique<Setting>();
                Setting& s     = *setting;
                s.name         = name;
                s.key          = split(name, '.').back();
                s.group        = &group;
                s.file         = mFile;
                s.line         = line;

                const auto typeName = table["type"].value<std::string>();
                const auto type     = ALCVarTypes::fromName(*typeName);
                if (!type)
                {
                    std::vector<std::string> names;
                    for (U32 i = 0; i < ALCVAR_TYPE_COUNT; ++i)
                    {
                        names.emplace_back(ALCVarTypes::nameOf(ALCVarType(i)));
                    }
                    mDiag.error(mFile, whereOf(*table.get("type")), name,
                                "'" + *typeName + "' is not a type: one of " + join(names, ", ") + guess(*typeName, names));
                    return;
                }
                s.type = *type;
                checkName(name, line, true, s.type);

                for (auto&& [key, node] : table)
                {
                    const std::string field(key.str());
                    if (!SETTING_FIELDS.contains(field))
                    {
                        if (const toml::table* child = node.as_table(); child && !child->is_inline())
                        {
                            mDiag.error(mFile, whereOf(key), name + "." + field, "a setting has no settings under it");
                        }
                        else if (field == "step")
                        {
                            mDiag.error(mFile, whereOf(key), name,
                                        "a setting has no field 'step': a slider's increment is ui_step, and a step is the "
                                        "lock's");
                        }
                        else
                        {
                            mDiag.error(mFile, whereOf(key), name,
                                        "a setting has no field '" + field + "'" + guess(field, keysOf(SETTING_FIELDS)));
                        }
                    }
                }

                const toml::node* defaultNode = table.get("default");
                if (!defaultNode)
                {
                    mDiag.error(mFile, line, name, "no default");
                }
                const toml::node* help = table.get("help");
                if (!help || !help->is_string() || help->as_string()->get().empty())
                {
                    mDiag.error(mFile, help ? whereOf(*help) : line, name, "no help: say what it does");
                }
                else
                {
                    s.help = help->as_string()->get();
                }
                if (const toml::node* label = table.get("label"))
                {
                    if (const auto* text = label->as_string())
                    {
                        s.label = text->get();
                    }
                    else
                    {
                        mDiag.error(mFile, whereOf(*label), name, "label is a string");
                    }
                }
                if (s.label.empty())
                {
                    s.label = labelFor(s.key);
                }

                readEnums(table, s);
                readUnit(table, s);
                readFlags(table, s);
                readRange(table, s, "range");
                readRange(table, s, "ui_range");
                if (const toml::node* step = table.get("ui_step"))
                {
                    const auto value = numberOf(*step);
                    if (!value || *value <= 0.0 || !ALCVarTypes::isNumber(s.type))
                    {
                        mDiag.error(mFile, whereOf(*step), name, "ui_step is a positive number, for a number");
                    }
                    else
                    {
                        s.uiStep = *value;
                    }
                }
                readChoices(table, s);
                checkWas(s);
                readTags(table, s);
                readStrings(table, s);
                readAliases(table, s);
                readMigrate(table, s);
                readFrom(table, s);
                if (defaultNode)
                {
                    readDefault(*defaultNode, s);
                }
                checkSetting(table, s);

                Setting* raw = setting.get();
                group.settings.push_back(raw);
                mSchema.settingByName.emplace(name, raw);
                mSchema.settings.push_back(std::move(setting));
            }

            void readEnums(const toml::table& table, Setting& s)
            {
                if (const toml::node* node = table.get("scope"))
                {
                    if (auto v = readEnum(*node, SCOPES, s.name, "scope", whereOf(*node)))
                    {
                        s.scope    = *v;
                        s.hasScope = true;
                    }
                }
                if (const toml::node* node = table.get("backup"))
                {
                    if (auto v = readEnum(*node, BACKUPS, s.name, "backup", whereOf(*node)))
                    {
                        s.backup    = *v;
                        s.hasBackup = true;
                    }
                }
                if (const toml::node* node = table.get("preset"))
                {
                    if (auto v = readEnum(*node, PRESETS, s.name, "preset", whereOf(*node)))
                    {
                        s.preset    = *v;
                        s.hasPreset = true;
                    }
                }
                if (const toml::node* node = table.get("refers"))
                {
                    if (auto v = readEnum(*node, REFERS, s.name, "refers", whereOf(*node)))
                    {
                        s.refers    = *v;
                        s.hasRefers = true;
                    }
                }
                if (const toml::node* node = table.get("origin"))
                {
                    if (auto v = readEnum(*node, ORIGINS, s.name, "origin", whereOf(*node)))
                    {
                        s.origin    = *v;
                        s.hasOrigin = true;
                    }
                }
            }

            std::optional<ALCVarUnit> unitNamed(const toml::node& node, const std::string& name)
            {
                const auto* text = node.as_string();
                if (text && !text->get().empty())
                {
                    if (auto unit = ALCVarUnits::fromName(text->get()))
                    {
                        return unit;
                    }
                }
                std::vector<std::string> names;
                for (U32 i = 1; i < U32(ALCVarUnit::Count); ++i)
                {
                    names.emplace_back(ALCVarUnits::nameOf(ALCVarUnit(i)));
                }
                if (text)
                {
                    mDiag.error(mFile, whereOf(node), name,
                                "'" + text->get() + "' is not a unit: one of " + join(names, ", ") + guess(text->get(), names));
                }
                else
                {
                    mDiag.error(mFile, whereOf(node), name, "unit is one of " + join(names, ", "));
                }
                return std::nullopt;
            }

            void readUnit(const toml::table& table, Setting& s)
            {
                if (const toml::node* node = table.get("unit"))
                {
                    if (auto unit = unitNamed(*node, s.name))
                    {
                        s.unit = *unit;
                    }
                    if (!ALCVarTypes::isNumber(s.type) && ALCVarTypes::componentsOf(s.type) == 0)
                    {
                        mDiag.error(mFile, whereOf(*node), s.name, "a unit is for a number");
                    }
                }
            }

            void readFlags(const toml::table& table, Setting& s)
            {
                const toml::node* node = table.get("flags");
                if (!node)
                {
                    return;
                }
                const auto* list = node->as_array();
                if (!list)
                {
                    mDiag.error(mFile, whereOf(*node), s.name, "flags is a list: [\"hidden\", ...]");
                    return;
                }
                for (const auto& item : *list)
                {
                    const auto* text = item.as_string();
                    const auto  flag = text ? lookup(FLAGS, text->get()) : std::nullopt;
                    if (!flag)
                    {
                        std::vector<std::string_view> names;
                        for (const auto& [spelling, value] : FLAGS)
                        {
                            names.push_back(spelling);
                        }
                        mDiag.error(mFile, whereOf(item), s.name,
                                    "a flag is one of " + choicesOf(FLAGS) + (text ? guess(text->get(), names) : ""));
                        continue;
                    }
                    s.flags = U16(s.flags | *flag);
                }
            }

            void readRange(const toml::table& table, Setting& s, std::string_view field)
            {
                const toml::node* node = table.get(field);
                if (!node)
                {
                    return;
                }
                const auto* pair = node->as_array();
                std::optional<F64> lo, hi;
                if (pair && pair->size() == 2)
                {
                    lo = numberOf(*pair->get(0));
                    hi = numberOf(*pair->get(1));
                }
                const bool numeric = ALCVarTypes::isNumber(s.type) || ALCVarTypes::componentsOf(s.type) > 0;
                if (!lo || !hi || *lo > *hi || !numeric)
                {
                    mDiag.error(mFile, whereOf(*node), s.name,
                                std::string(field) + " is [lowest, highest], for a number or a vector's components");
                    return;
                }
                if (field == "range")
                {
                    s.ranged   = true;
                    s.rangeMin = *lo;
                    s.rangeMax = *hi;
                }
                else
                {
                    s.uiRanged = true;
                    s.uiMin    = *lo;
                    s.uiMax    = *hi;
                }
            }

            void readChoices(const toml::table& table, Setting& s)
            {
                const toml::node* node = table.get("choices");
                if (!node)
                {
                    return;
                }
                const bool integer = ALCVarTypes::isInteger(s.type);
                if (!integer && s.type != ALCVarType::String)
                {
                    mDiag.error(mFile, whereOf(*node), s.name, "choices are for an integer or a string");
                    return;
                }
                auto valueOf = [&](const toml::node& value, Choice& choice) {
                    if (integer)
                    {
                        if (const auto* i = value.as_integer())
                        {
                            choice.number = i->get();
                            return true;
                        }
                        return false;
                    }
                    if (const auto* text = value.as_string())
                    {
                        choice.text = text->get();
                        return true;
                    }
                    return false;
                };
                if (const auto* inline_table = node->as_table())
                {
                    std::vector<std::pair<Where, Choice>> ordered;
                    for (auto&& [key, label] : *inline_table)
                    {
                        Choice choice;
                        const std::string text(key.str());
                        if (integer)
                        {
                            char* end     = nullptr;
                            choice.number = std::strtoll(text.c_str(), &end, 10);
                            if (text.empty() || *end != '\0')
                            {
                                mDiag.error(mFile, whereOf(key), s.name, "'" + text + "' is not an integer choice");
                                continue;
                            }
                        }
                        else
                        {
                            choice.text = text;
                        }
                        if (const auto* l = label.as_string())
                        {
                            choice.label = l->get();
                        }
                        else
                        {
                            mDiag.error(mFile, whereOf(key), s.name, "a choice's label is a string");
                        }
                        ordered.emplace_back(whereOf(key), std::move(choice));
                    }
                    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                    for (auto& [at, choice] : ordered)
                    {
                        s.choices.push_back(std::move(choice));
                    }
                    return;
                }
                const auto* list = node->as_array();
                if (!list)
                {
                    mDiag.error(mFile, whereOf(*node), s.name, "choices are { value = \"label\", ... } or a list of tables");
                    return;
                }
                for (const auto& item : *list)
                {
                    const auto* entry = item.as_table();
                    Choice      choice;
                    if (!entry || !entry->get("value") || !valueOf(*entry->get("value"), choice))
                    {
                        mDiag.error(mFile, whereOf(item), s.name, "a choice is { value = ..., label = \"...\" }");
                        continue;
                    }
                    choice.label = (*entry)["label"].value_or(std::string());
                    if (const auto* was = (*entry)["was"].as_array())
                    {
                        for (const auto& old : *was)
                        {
                            if (const auto* i = old.as_integer())
                            {
                                choice.wasNumbers.push_back(i->get());
                            }
                            else if (const auto* text = old.as_string())
                            {
                                choice.wasTexts.emplace_back(text->get());
                            }
                            else
                            {
                                mDiag.error(mFile, whereOf(old), s.name, "a choice was a number or a string");
                            }
                        }
                    }
                    s.choices.push_back(std::move(choice));
                }
            }

            void readTags(const toml::table& table, Setting& s)
            {
                const toml::node* node = table.get("tags");
                if (!node)
                {
                    return;
                }
                const auto* list = node->as_array();
                if (!list)
                {
                    mDiag.error(mFile, whereOf(*node), s.name, "tags is a list of names");
                    return;
                }
                for (const auto& item : *list)
                {
                    // A tag is a handle's name in the schema's header: a C++
                    // name no platform's macro takes.
                    const auto* text = item.as_string();
                    bool        ok   = text && !text->get().empty() && text->get()[0] >= 'a' && text->get()[0] <= 'z';
                    for (const char c : ok ? text->get() : std::string())
                    {
                        ok = ok && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
                    }
                    if (!ok)
                    {
                        mDiag.error(mFile, whereOf(item), s.name, "a tag is a lower_snake name, a letter first");
                        continue;
                    }
                    if (KEYWORDS.contains(text->get()))
                    {
                        mDiag.error(mFile, whereOf(item), s.name, "the tag '" + text->get() + "' is a C++ keyword");
                        continue;
                    }
                    if (MACROS.contains(text->get()))
                    {
                        mDiag.error(mFile, whereOf(item), s.name,
                                    "the tag '" + text->get() + "' is a macro on a platform the viewer builds for");
                        continue;
                    }
                    s.tags.push_back(text->get());
                }
            }

            void readStrings(const toml::table& table, Setting& s)
            {
                for (const std::string_view field : { std::string_view("validate"), std::string_view("asset") })
                {
                    const toml::node* node = table.get(field);
                    if (!node)
                    {
                        continue;
                    }
                    const auto* text = node->as_string();
                    if (!text || text->get().empty())
                    {
                        mDiag.error(mFile, whereOf(*node), s.name, std::string(field) + " names a hook: a string");
                        continue;
                    }
                    (field == "validate" ? s.validate : s.asset) = text->get();
                }
            }

            void readAlias(const toml::node& node, Setting& s)
            {
                Alias alias;
                alias.file = mFile;
                alias.line = whereOf(node);
                if (const auto* text = node.as_string())
                {
                    alias.name = text->get();
                }
                else if (const auto* entry = node.as_table())
                {
                    alias.name = (*entry)["name"].value_or(std::string());
                    for (auto&& [key, value] : *entry)
                    {
                        const std::string field(key.str());
                        if (field == "name")
                        {
                            continue;
                        }
                        if (field == "inverted")
                        {
                            alias.inverted = value.value_or(false);
                            if (s.type != ALCVarType::Bool)
                            {
                                mDiag.error(mFile, whereOf(key), s.name, "only a boolean is inverted");
                            }
                        }
                        else if (field == "encoding")
                        {
                            if (value.value_or(std::string()) != "string")
                            {
                                mDiag.error(mFile, whereOf(key), s.name, "an alias's encoding is \"string\"");
                            }
                            alias.stringEncoded = true;
                            if (s.type != ALCVarType::UUID && !ALCVarTypes::isList(s.type))
                            {
                                mDiag.error(mFile, whereOf(key), s.name,
                                            "only a uuid or a list was kept in a string");
                            }
                        }
                        else if (field == "joined")
                        {
                            alias.joined = value.value_or(std::string());
                            if (alias.joined.empty() || !ALCVarTypes::isList(s.type))
                            {
                                mDiag.error(mFile, whereOf(key), s.name,
                                            "joined is the separator a list was kept joined by");
                            }
                        }
                        else if (field == "unit")
                        {
                            if (auto unit = unitNamed(value, s.name))
                            {
                                alias.unit = *unit;
                            }
                        }
                        else
                        {
                            mDiag.error(mFile, whereOf(key), s.name,
                                        "an alias has no field '" + field + "'" +
                                            guess(field, std::array<std::string_view, 5>{ "name", "inverted", "encoding", "joined", "unit" }));
                        }
                    }
                }
                if (alias.name.empty())
                {
                    mDiag.error(mFile, alias.line, s.name, "an alias is an old name, or { name = \"...\", ... }");
                    return;
                }
                s.aliases.push_back(std::move(alias));
            }

            void readAliases(const toml::table& table, Setting& s)
            {
                const toml::node* node = table.get("alias");
                if (!node)
                {
                    return;
                }
                if (const auto* list = node->as_array())
                {
                    for (const auto& item : *list)
                    {
                        readAlias(item, s);
                    }
                }
                else
                {
                    readAlias(*node, s);
                }
            }

            // A value a choice was is no longer offered: one still a choice
            // would mean two things.
            void checkWas(const Setting& s)
            {
                const bool integer = ALCVarTypes::isInteger(s.type);
                for (const Choice& choice : s.choices)
                {
                    const std::string value = integer ? std::to_string(choice.number) : choice.text;
                    for (const Choice& other : s.choices)
                    {
                        const bool was =
                            integer ? std::find(choice.wasNumbers.begin(), choice.wasNumbers.end(), other.number) != choice.wasNumbers.end()
                                    : std::find(choice.wasTexts.begin(), choice.wasTexts.end(), other.text) != choice.wasTexts.end();
                        if (was)
                        {
                            mDiag.error(mFile, s.line, s.name,
                                        (integer ? std::to_string(other.number) : other.text) +
                                            " is still a choice, and cannot be what " + value + " was");
                        }
                    }
                }
            }

            // One answer of a migrate: map and its else, scale and offset,
            // reset or fn. Whether a map or a scale has a number to take is
            // the history's to say (History). False where it says
            // nothing it can do.
            bool readAnswer(const toml::table& entry, Answer& m, Setting& s, Where line, bool joins)
            {
                int kinds = 0;
                for (auto&& [key, value] : entry)
                {
                    const std::string field(key.str());
                    if (field == "step" || (joins && field == "join"))
                    {
                        continue;
                    }
                    if (field == "map")
                    {
                        ++kinds;
                        m.kind            = ALCVarMigrateKind::Map;
                        const auto* pairs = value.as_table();
                        if (!pairs)
                        {
                            mDiag.error(mFile, whereOf(key), s.name, "map is { old = new, ... }");
                            continue;
                        }
                        // Whether a key is a number or a string is the step
                        // before's to say (History).
                        for (auto&& [from, to] : *pairs)
                        {
                            MapEntry entry;
                            entry.from      = std::string(from.str());
                            char*     end   = nullptr;
                            const F64 old   = std::strtod(entry.from.c_str(), &end);
                            if (!entry.from.empty() && *end == '\0' && finite(old))
                            {
                                entry.fromNumber = old;
                            }
                            if (const auto now = numberOf(to))
                            {
                                entry.toNumber = now;
                            }
                            else if (const auto* text = to.as_string())
                            {
                                entry.toText = text->get();
                            }
                            else
                            {
                                mDiag.error(mFile, whereOf(from), s.name, "a map's value is a number or a string");
                                continue;
                            }
                            m.map.push_back(std::move(entry));
                        }
                        std::sort(m.map.begin(), m.map.end(), [](const MapEntry& a, const MapEntry& b) {
                            if (a.fromNumber.has_value() != b.fromNumber.has_value())
                            {
                                return a.fromNumber.has_value();
                            }
                            if (a.fromNumber && *a.fromNumber != *b.fromNumber)
                            {
                                return *a.fromNumber < *b.fromNumber;
                            }
                            return a.from < b.from;
                        });
                    }
                    else if (field == "else")
                    {
                        m.hasElse = true;
                        if (const auto* text = value.as_string(); text && (text->get() == "keep" || text->get() == "default"))
                        {
                            m.otherwise = text->get() == "keep" ? ALCVarMapElse::Keep : ALCVarMapElse::Default;
                        }
                        else if (const auto number = numberOf(value))
                        {
                            m.otherwise = ALCVarMapElse::Value;
                            m.elseValue = *number;
                        }
                        else if (text)
                        {
                            m.otherwise = ALCVarMapElse::Value;
                            m.elseText  = text->get();
                        }
                        else
                        {
                            mDiag.error(mFile, whereOf(key), s.name, "else is \"keep\", \"default\" or a value");
                        }
                    }
                    else if (field == "scale" || field == "offset")
                    {
                        const auto number = numberOf(value);
                        if (!number)
                        {
                            mDiag.error(mFile, whereOf(key), s.name, field + " is a number");
                            continue;
                        }
                        if (m.kind != ALCVarMigrateKind::Scale)
                        {
                            ++kinds;
                        }
                        m.kind                                  = ALCVarMigrateKind::Scale;
                        (field == "scale" ? m.scale : m.offset) = *number;
                    }
                    else if (field == "reset")
                    {
                        ++kinds;
                        m.kind = ALCVarMigrateKind::Reset;
                        if (!value.value_or(false))
                        {
                            mDiag.error(mFile, whereOf(key), s.name, "reset is true");
                        }
                    }
                    else if (field == "fn")
                    {
                        ++kinds;
                        m.kind = ALCVarMigrateKind::Hook;
                        m.hook = value.value_or(std::string());
                        if (m.hook.empty())
                        {
                            mDiag.error(mFile, whereOf(key), s.name, "fn names a registered migration");
                        }
                    }
                    else
                    {
                        mDiag.error(mFile, whereOf(key), s.name,
                                    "migrate has no field '" + field + "'" +
                                        guess(field, std::array<std::string_view, 8>{ "step", "map", "else", "scale", "offset",
                                                                                      "reset", "fn", "join" }));
                    }
                }
                if (m.kind == ALCVarMigrateKind::Map && !m.hasElse)
                {
                    mDiag.error(mFile, line, s.name,
                                "a map says what becomes of a value it does not list: else = \"keep\", \"default\" or "
                                "a value");
                }
                else if (m.hasElse && m.kind != ALCVarMigrateKind::Map)
                {
                    mDiag.error(mFile, line, s.name, "else belongs to a map");
                }
                return kinds == 1;
            }

            // A list, an entry a step: what each step's change did to the
            // values before it, or the join that ends a fork.
            void readMigrate(const toml::table& table, Setting& s)
            {
                const toml::node* node = table.get("migrate");
                if (!node)
                {
                    return;
                }
                const auto* list = node->as_array();
                if (!list)
                {
                    mDiag.error(mFile, whereOf(*node), s.name,
                                "migrate is a list, an entry a step: [{ step = <n>, map | scale | offset | reset | fn | "
                                "join }]");
                    return;
                }
                for (const auto& item : *list)
                {
                    const auto* entry = item.as_table();
                    const Where line  = whereOf(item);
                    const auto  step  = entry ? (*entry)["step"].value<S64>() : std::nullopt;
                    if (!step)
                    {
                        mDiag.error(mFile, line, s.name, "a migrate is { step = <n>, map | scale | offset | reset | fn | join }");
                        continue;
                    }
                    Migrate m;
                    m.step = S32(std::clamp<S64>(*step, 0, 1 << 30));
                    m.line = line;
                    if (const toml::node* join = entry->get("join"))
                    {
                        const auto* answers = join->as_table();
                        if (!answers || entry->size() != 2)
                        {
                            mDiag.error(mFile, line, s.name,
                                        "a join is { step = <n>, join = { \"<decl>\" = { map | scale | offset | reset | fn }, "
                                        "... } }");
                            continue;
                        }
                        m.kind = ALCVarMigrateKind::Join;
                        for (auto&& [key, value] : *answers)
                        {
                            const std::string decl(key.str());
                            const auto*       answer = value.as_table();
                            Answer            a;
                            if (decl.size() != 16 || decl.find_first_not_of("0123456789abcdef") != std::string::npos ||
                                !answer || !readAnswer(*answer, a, s, whereOf(key), false))
                            {
                                mDiag.error(mFile, whereOf(key), s.name,
                                            "a join answers a step by its fingerprint: \"<16 hex digits>\" = { map | scale | "
                                            "offset | reset | fn }");
                                continue;
                            }
                            m.join.emplace_back(std::strtoull(decl.c_str(), nullptr, 16), std::move(a));
                        }
                        std::sort(m.join.begin(), m.join.end(),
                                  [](const auto& a, const auto& b) { return a.first < b.first; });
                    }
                    else if (!readAnswer(*entry, m, s, line, true))
                    {
                        mDiag.error(mFile, line, s.name, "migrate says one thing: map, scale and offset, reset, fn or join");
                    }
                    for (const Migrate& other : s.migrates)
                    {
                        if (other.step == m.step)
                        {
                            mDiag.error(mFile, line, s.name, "step " + std::to_string(m.step) + " has a migrate already");
                        }
                    }
                    s.migrates.push_back(std::move(m));
                }
                std::stable_sort(s.migrates.begin(), s.migrates.end(),
                                 [](const Migrate& a, const Migrate& b) { return a.step < b.step; });
            }

            // The settings it takes its first value from, each with the
            // hook that works it out; and whether it reuses a name.
            void readFrom(const toml::table& table, Setting& s)
            {
                if (const toml::node* node = table.get("reuse"))
                {
                    if (!node->is_boolean())
                    {
                        mDiag.error(mFile, whereOf(*node), s.name, "reuse is true or false");
                    }
                    s.reuse = node->value_or(false);
                }
                const toml::node* node = table.get("from");
                if (!node)
                {
                    return;
                }
                const auto* list = node->as_array();
                if (!list)
                {
                    mDiag.error(mFile, whereOf(*node), s.name, "from is a list: [{ name = \"...\", fn = \"...\" }]");
                    return;
                }
                for (const auto& item : *list)
                {
                    const auto* entry = item.as_table();
                    From        f;
                    f.line = whereOf(item);
                    if (entry)
                    {
                        f.name = (*entry)["name"].value_or(std::string());
                        f.hook = (*entry)["fn"].value_or(std::string());
                    }
                    if (!entry || entry->size() != 2 || f.name.empty() || f.hook.empty())
                    {
                        mDiag.error(mFile, f.line, s.name, "a source is { name = \"...\", fn = \"...\" }");
                        continue;
                    }
                    s.from.push_back(std::move(f));
                }
            }

            // ---- defaults -----------------------------------------------

            void badDefault(const Setting& s, Where line, const std::string& what)
            {
                mDiag.error(mFile, line, s.name, "the default " + what);
            }

            std::optional<std::vector<F64>> components(const toml::node& node, size_t count)
            {
                const auto* list = node.as_array();
                if (!list || list->size() != count)
                {
                    return std::nullopt;
                }
                std::vector<F64> out;
                for (const auto& item : *list)
                {
                    const auto value = numberOf(item);
                    if (!value || !finite(*value))
                    {
                        return std::nullopt;
                    }
                    out.push_back(*value);
                }
                return out;
            }

            static U64 packF32(F32 lo, F32 hi)
            {
                return U64(std::bit_cast<U32>(lo)) | (U64(std::bit_cast<U32>(hi)) << 32);
            }

            void readDefault(const toml::node& node, Setting& s)
            {
                const Where line = whereOf(node);
                switch (s.type)
                {
                    case ALCVarType::Bool:
                    {
                        const auto* b = node.as_boolean();
                        if (!b)
                        {
                            return badDefault(s, line, "is true or false");
                        }
                        s.words[0] = b->get() ? 1 : 0;
                        s.json     = b->get() ? "true" : "false";
                        return;
                    }
                    case ALCVarType::S32:
                    case ALCVarType::U32:
                    case ALCVarType::S64:
                    {
                        const auto* i = node.as_integer();
                        if (!i)
                        {
                            return badDefault(s, line, "is an integer");
                        }
                        const S64 v  = i->get();
                        const S64 lo = s.type == ALCVarType::S32 ? S64(INT32_MIN) : s.type == ALCVarType::U32 ? 0 : INT64_MIN;
                        const S64 hi = s.type == ALCVarType::S32 ? S64(INT32_MAX) : s.type == ALCVarType::U32 ? S64(UINT32_MAX) : INT64_MAX;
                        if (v < lo || v > hi)
                        {
                            return badDefault(s, line, "is past what " + std::string(ALCVarTypes::nameOf(s.type)) + " holds");
                        }
                        s.words[0] = s.type == ALCVarType::S64 ? U64(v) : U64(U32(S32(v)));
                        if (s.type == ALCVarType::U32)
                        {
                            s.words[0] = U64(U32(v));
                        }
                        s.json = std::to_string(v);
                        checkInRange(s, F64(v), line);
                        return;
                    }
                    case ALCVarType::F32:
                    case ALCVarType::F64:
                    {
                        const auto v = numberOf(node);
                        if (!v || !finite(*v))
                        {
                            return badDefault(s, line, "is a finite number");
                        }
                        s.words[0] = s.type == ALCVarType::F32 ? U64(std::bit_cast<U32>(F32(*v))) : std::bit_cast<U64>(*v);
                        s.json     = number(*v);
                        checkInRange(s, *v, line);
                        return;
                    }
                    case ALCVarType::String:
                    {
                        const auto* text = node.as_string();
                        if (!text)
                        {
                            return badDefault(s, line, "is a string");
                        }
                        s.text = text->get();
                        s.json = jsonString(s.text);
                        return;
                    }
                    case ALCVarType::UUID:
                    {
                        const auto* text = node.as_string();
                        if (!text || !isUUID(text->get()))
                        {
                            return badDefault(s, line, "is a UUID: \"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx\"");
                        }
                        const auto bytes = uuidBytes(text->get());
                        std::memcpy(s.words, bytes.data(), 16);
                        s.json = jsonString(lowered(text->get()));
                        return;
                    }
                    case ALCVarType::Vec2:
                    case ALCVarType::Vec3:
                    case ALCVarType::Quat:
                    case ALCVarType::Color3:
                    case ALCVarType::Color4:
                    case ALCVarType::Vec3d:
                    {
                        const size_t n = ALCVarTypes::componentsOf(s.type);
                        const auto   v = components(node, n);
                        if (!v)
                        {
                            return badDefault(s, line, "is " + std::to_string(n) + " finite numbers: [x, y, ...]");
                        }
                        std::vector<std::string> parts;
                        for (const F64 c : *v)
                        {
                            parts.push_back(s.type == ALCVarType::Vec3d ? number(c) : numberF32(F32(c)));
                            checkInRange(s, c, line);
                        }
                        s.json = "[" + join(parts, ", ") + "]";
                        if (s.type == ALCVarType::Vec3d)
                        {
                            for (size_t i = 0; i < 3; ++i)
                            {
                                s.words[i] = std::bit_cast<U64>((*v)[i]);
                            }
                        }
                        else
                        {
                            const F32 x = F32((*v)[0]);
                            const F32 y = F32((*v)[1]);
                            const F32 z = n > 2 ? F32((*v)[2]) : 0.f;
                            const F32 w = n > 3 ? F32((*v)[3]) : 0.f;
                            s.words[0]  = packF32(x, y);
                            if (n > 2)
                            {
                                s.words[1] = n > 3 ? packF32(z, w) : U64(std::bit_cast<U32>(z));
                            }
                        }
                        return;
                    }
                    case ALCVarType::Rect:
                    {
                        const auto* list = node.as_array();
                        std::array<S64, 4> v{};
                        bool ok = list && list->size() == 4;
                        for (size_t i = 0; ok && i < 4; ++i)
                        {
                            const auto* c = list->get(i)->as_integer();
                            ok            = c && c->get() >= INT32_MIN && c->get() <= INT32_MAX;
                            v[i]          = ok ? c->get() : 0;
                        }
                        if (!ok)
                        {
                            return badDefault(s, line, "is four integers: [left, top, right, bottom]");
                        }
                        s.words[0] = U64(U32(S32(v[0]))) | (U64(U32(S32(v[1]))) << 32);
                        s.words[1] = U64(U32(S32(v[2]))) | (U64(U32(S32(v[3]))) << 32);
                        s.json     = "[" + std::to_string(v[0]) + ", " + std::to_string(v[1]) + ", " +
                                 std::to_string(v[2]) + ", " + std::to_string(v[3]) + "]";
                        return;
                    }
                    case ALCVarType::UUIDList:
                    case ALCVarType::StringList:
                    case ALCVarType::Vec2List:
                    {
                        const auto* list = node.as_array();
                        if (!list)
                        {
                            return badDefault(s, line, "is a list");
                        }
                        std::vector<std::string> parts;
                        for (const auto& item : *list)
                        {
                            if (s.type == ALCVarType::Vec2List)
                            {
                                const auto v = components(item, 2);
                                if (!v)
                                {
                                    return badDefault(s, whereOf(item), "is a list of [x, y]");
                                }
                                parts.push_back("[" + numberF32(F32((*v)[0])) + ", " + numberF32(F32((*v)[1])) + "]");
                                continue;
                            }
                            const auto* text = item.as_string();
                            if (!text || (s.type == ALCVarType::UUIDList && !isUUID(text->get())))
                            {
                                return badDefault(s, whereOf(item), s.type == ALCVarType::UUIDList ? "is a list of UUIDs" : "is a list of strings");
                            }
                            parts.push_back(jsonString(s.type == ALCVarType::UUIDList ? lowered(text->get()) : text->get()));
                        }
                        s.text = "[" + join(parts, ",") + "]";
                        s.json = s.text;
                        return;
                    }
                    case ALCVarType::LLSD:
                    {
                        const auto* text = node.as_string();
                        Json        parsed;
                        std::string error;
                        if (!text || !parseJson(text->get(), parsed, error))
                        {
                            return badDefault(s, line, "is LLSD as JSON text" + (error.empty() ? std::string() : ": " + error));
                        }
                        s.text = text->get();
                        s.json = s.text;
                        return;
                    }
                }
            }

            void checkInRange(const Setting& s, F64 value, Where line)
            {
                if (s.ranged && (value < s.rangeMin || value > s.rangeMax))
                {
                    mDiag.error(mFile, line, s.name,
                                "the default " + valueText(s.type, value) + " is outside the range [" +
                                    valueText(s.type, s.rangeMin) + ", " + valueText(s.type, s.rangeMax) + "]");
                }
            }

            void checkSetting(const toml::table& table, Setting& s)
            {
                const Where line = s.line;
                if ((s.type == ALCVarType::UUID || s.type == ALCVarType::UUIDList) && !s.hasRefers)
                {
                    mDiag.error(mFile, line, s.name, "a UUID says what it refers to: refers = \"sound\", ... or \"none\"");
                }
                if (s.hasRefers && s.type != ALCVarType::UUID && s.type != ALCVarType::UUIDList)
                {
                    mDiag.error(mFile, line, s.name, "refers is for a UUID");
                }
                if (!s.asset.empty() && s.type != ALCVarType::String)
                {
                    mDiag.error(mFile, line, s.name, "asset is for a string that names a file");
                }
                if (s.type == ALCVarType::String && s.asset.empty())
                {
                    const size_t dot = s.text.rfind('.');
                    if (dot != std::string::npos && s.text.find_first_of("/\\: ") == std::string::npos &&
                        FILE_EXTENSIONS.contains(lowered(s.text.substr(dot + 1))))
                    {
                        mDiag.error(mFile, line, s.name, "names a file: say which kind with asset = \"...\"");
                    }
                }
                if (s.uiRanged && s.ranged && (s.uiMin < s.rangeMin || s.uiMax > s.rangeMax))
                {
                    mDiag.error(mFile, line, s.name, "ui_range reaches past range");
                }
                if (!s.choices.empty() && table.get("default"))
                {
                    bool found = false;
                    for (const auto& choice : s.choices)
                    {
                        found = found || (ALCVarTypes::isInteger(s.type) ? choice.number == S64(s.type == ALCVarType::S32 ? S64(S32(U32(s.words[0]))) : S64(s.words[0]))
                                                                          : choice.text == s.text);
                    }
                    if (!found)
                    {
                        mDiag.error(mFile, line, s.name, "the default is not among the choices");
                    }
                }
            }

            const Options& mOptions;
            Schema&        mSchema;
            Diagnostics&   mDiag;
            std::string    mFile;
            std::map<std::string, Where, std::less<>> mHeaders;
        };

        // ---- what a group gives its settings ----------------------------

        template <typename T>
        std::optional<T> inherited(const Group* group, std::optional<T> Group::*field)
        {
            for (; group; group = group->parent)
            {
                if (group->*field)
                {
                    return group->*field;
                }
            }
            return std::nullopt;
        }

        void inherit(Schema& schema, Diagnostics& diag)
        {
            for (auto& setting : schema.settings)
            {
                Setting& s = *setting;
                if (!s.hasScope)
                {
                    s.scope = inherited(s.group, &Group::scope).value_or(ALCVarScope::Global);
                }
                if (!s.hasBackup)
                {
                    s.backup = inherited(s.group, &Group::backup).value_or(ALCVarBackup::Yes);
                    if (s.scope == ALCVarScope::Runtime)
                    {
                        s.backup = ALCVarBackup::Never;
                    }
                }
                if (!s.hasPreset)
                {
                    s.preset = inherited(s.group, &Group::preset).value_or(ALCVarPreset::None);
                }
                if (!s.hasOrigin)
                {
                    s.origin = inherited(s.group, &Group::origin).value_or(ALCVarOrigin::Alchemy);
                }
                if ((s.flags & ALCVAR_SENSITIVE) && s.backup != ALCVarBackup::Ask && s.backup != ALCVarBackup::Never)
                {
                    diag.error(s.file, s.line, s.name, "a sensitive setting is backed up only when asked: backup = \"ask\" or \"never\"");
                }
                if ((s.flags & ALCVAR_DANGEROUS) && s.preset != ALCVarPreset::None)
                {
                    diag.error(s.file, s.line, s.name, "a dangerous setting is in no preset: preset = \"none\"");
                }
                if ((s.flags & ALCVAR_ACCOUNT_OVERRIDE) && s.scope != ALCVarScope::Global)
                {
                    diag.error(s.file, s.line, s.name, "account_override is for a global setting");
                }
                if (s.type == ALCVarType::LLSD && s.preset != ALCVarPreset::None)
                {
                    diag.error(s.file, s.line, s.name, "LLSD is in no preset: preset = \"none\"");
                }
            }
        }

        // Unit suffixes are allowed only where two settings of one quantity
        // differ by their unit alone.
        void checkUnitSuffixes(const Schema& schema, Diagnostics& diag)
        {
            for (const auto& setting : schema.settings)
            {
                const auto words = split(setting->key, '_');
                if (words.size() < 2 || !UNIT_WORDS.contains(words.back()))
                {
                    continue;
                }
                const std::string stem = setting->key.substr(0, setting->key.size() - words.back().size());
                bool              paired = false;
                for (const Setting* sibling : setting->group->settings)
                {
                    paired = paired || (sibling != setting.get() && sibling->key.starts_with(stem) &&
                                        UNIT_WORDS.contains(sibling->key.substr(stem.size())));
                }
                if (!paired)
                {
                    diag.error(setting->file, setting->line, setting->name,
                               "'" + words.back() + "' is a unit: that is the unit field, not the name");
                }
            }
        }

        void checkAliases(const Schema& schema, Diagnostics& diag)
        {
            std::map<std::string, const Setting*, std::less<>> taken;
            for (const auto& setting : schema.settings)
            {
                for (const auto& alias : setting->aliases)
                {
                    if (schema.settingByName.contains(alias.name) || schema.groupByName.contains(alias.name))
                    {
                        diag.error(alias.file, alias.line, setting->name, "the alias " + alias.name + " is a current name");
                        continue;
                    }
                    const auto [at, added] = taken.emplace(alias.name, setting.get());
                    if (!added)
                    {
                        diag.error(alias.file, alias.line, setting->name,
                                   "the alias " + alias.name + " is already " + at->second->name + "'s");
                    }
                    if (alias.unit != ALCVarUnit::None && !ALCVarUnits::scale(alias.unit, setting->unit))
                    {
                        diag.error(alias.file, alias.line, setting->name,
                                   "the alias's unit does not scale into " + std::string(ALCVarUnits::nameOf(setting->unit)));
                    }
                }
            }
        }

        // ---- the lock ---------------------------------------------------

        std::string hex16(U64 value)
        {
            char text[24];
            std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
            return text;
        }

        std::string unitName(ALCVarUnit unit)
        {
            return unit == ALCVarUnit::None ? std::string("no unit") : std::string(ALCVarUnits::nameOf(unit));
        }

        // What a migrate does, as its hash takes it: nothing of where it is
        // written.
        std::string answerText(const Answer& a)
        {
            switch (a.kind)
            {
                case ALCVarMigrateKind::Map:
                {
                    // A map of numbers to numbers by its numbers; one with a
                    // string in it by its keys as written.
                    const bool numbers = !a.elseText && std::all_of(a.map.begin(), a.map.end(), [](const MapEntry& e) {
                        return e.fromNumber && e.toNumber;
                    });
                    std::vector<std::string> pairs;
                    if (numbers)
                    {
                        std::vector<std::pair<F64, F64>> sorted;
                        for (const MapEntry& e : a.map)
                        {
                            sorted.emplace_back(*e.fromNumber, *e.toNumber);
                        }
                        std::sort(sorted.begin(), sorted.end());
                        for (const auto& [from, to] : sorted)
                        {
                            pairs.push_back(number(from) + ":" + number(to));
                        }
                    }
                    else
                    {
                        std::vector<const MapEntry*> sorted;
                        for (const MapEntry& e : a.map)
                        {
                            sorted.push_back(&e);
                        }
                        std::sort(sorted.begin(), sorted.end(), [](const MapEntry* x, const MapEntry* y) { return x->from < y->from; });
                        for (const MapEntry* e : sorted)
                        {
                            pairs.push_back(jsonString(e->from) + ":" + (e->toNumber ? number(*e->toNumber) : jsonString(e->toText)));
                        }
                    }
                    return "map=" + join(pairs, ",") + ";else=" +
                           (a.otherwise == ALCVarMapElse::Keep      ? std::string("keep")
                            : a.otherwise == ALCVarMapElse::Default ? std::string("default")
                            : a.elseText                            ? jsonString(*a.elseText)
                                                                    : number(a.elseValue));
                }
                case ALCVarMigrateKind::Scale:
                    return "scale=" + number(a.scale) + ";offset=" + number(a.offset);
                case ALCVarMigrateKind::Reset:
                    return "reset";
                case ALCVarMigrateKind::Hook:
                    return "fn=" + jsonString(a.hook);
                default:
                    return "";
            }
        }

        std::string migrateHash(const Migrate& m)
        {
            if (m.kind != ALCVarMigrateKind::Join)
            {
                return hex16(fnv1a(answerText(m)));
            }
            std::string text = "join=";
            for (const auto& [decl, answer] : m.join)
            {
                text += hex16(decl) + "{" + answerText(answer) + "}";
            }
            return hex16(fnv1a(text));
        }

        // A rename's migrate: the old name's codec.
        std::string renameHash(const Alias& a)
        {
            return hex16(fnv1a("rename=" + jsonString(a.name) + ";inverted=" + (a.inverted ? "1" : "0") +
                               ";encoded=" + (a.stringEncoded ? "1" : "0") + ";joined=" + jsonString(a.joined) +
                               ";unit=" + jsonString(ALCVarUnits::nameOf(a.unit))));
        }

        // The step a declaration is: its type, unit and choices, unnumbered.
        Step stepOf(const Setting& s)
        {
            Step step;
            step.name = s.name;
            step.type = s.type;
            step.unit = s.unit;
            for (const Choice& choice : s.choices)
            {
                step.choices.push_back({ choice.number, choice.text, choice.wasNumbers, choice.wasTexts });
            }
            return step;
        }

        std::string choiceValue(const Step& step, const LockChoice& choice)
        {
            return ALCVarTypes::isInteger(step.type) ? std::to_string(choice.number) : choice.text;
        }

        // A step's choices as its line gives them: each value with the
        // values that became it, numbers before texts.
        std::string choicesJson(const Step& step)
        {
            const bool  integer = ALCVarTypes::isInteger(step.type);
            std::string out     = "[";
            for (size_t c = 0; c < step.choices.size(); ++c)
            {
                const LockChoice& choice = step.choices[c];
                out += std::string(c ? ", " : "") + "{\"value\": " +
                       (integer ? std::to_string(choice.number) : jsonString(choice.text));
                if (!choice.wasNumbers.empty() || !choice.wasTexts.empty())
                {
                    std::vector<std::string> was;
                    for (const S64 n : choice.wasNumbers)
                    {
                        was.push_back(std::to_string(n));
                    }
                    for (const auto& text : choice.wasTexts)
                    {
                        was.push_back(jsonString(text));
                    }
                    out += ", \"was\": [" + join(was, ", ") + "]";
                }
                out += "}";
            }
            return out + "]";
        }

        // What a row written at the step carries: a hash of the step it
        // follows, its type, its unit, its choices with the values that
        // became them, and its migrate, so that a step is known by its
        // history -- two branches' different steps of one setting have two,
        // and a migrate edited after it was recorded makes another. Help,
        // labels, defaults, ranges, the number and the name are not in it.
        U64 fingerprintOf(const Step& step)
        {
            if (step.kind == Step::Kind::Retired)
            {
                return fnv1a("parent=" + step.parent + ";retired");
            }
            if (step.kind == Step::Kind::Renamed)
            {
                return fnv1a("parent=" + step.parent + ";renamed_to=" + jsonString(step.renamedTo));
            }
            return fnv1a("parent=" + step.parent + ";type=" + jsonString(ALCVarTypes::nameOf(step.type)) + ";unit=" +
                         jsonString(ALCVarUnits::nameOf(step.unit)) + ";choices=" + choicesJson(step) +
                         ";migrate=" + step.migrate);
        }

        bool sameShape(const Step& a, const Step& b)
        {
            return a.type == b.type && a.unit == b.unit && choicesJson(a) == choicesJson(b);
        }

        std::string lockLine(const Step& step)
        {
            std::string head = "{\"name\": " + jsonString(step.name);
            if (step.kind == Step::Kind::Step)
            {
                head += ", \"step\": " + std::to_string(step.number);
            }
            head += ", \"decl\": \"" + hex16(step.decl) + "\", \"parent\": \"" + step.parent + "\"";
            if (step.kind == Step::Kind::Retired)
            {
                return head + ", \"retired\": true}";
            }
            if (step.kind == Step::Kind::Renamed)
            {
                return head + ", \"renamed_to\": " + jsonString(step.renamedTo) + "}";
            }
            return head + ", \"type\": " + jsonString(ALCVarTypes::nameOf(step.type)) +
                   ", \"unit\": " + jsonString(ALCVarUnits::nameOf(step.unit)) + ", \"choices\": " + choicesJson(step) +
                   ", \"migrate\": \"" + step.migrate + "\"}";
        }

        struct Lock
        {
            std::string path;
            // Every line, in the order the file holds them, which a union
            // merge leaves as it will; a line merged in twice once.
            std::vector<Step> lines;
            // Each name's lines.
            std::map<std::string, std::vector<const Step*>, std::less<>> byName;

            const Step* find(std::string_view name, const std::string& decl) const
            {
                const auto lines = byName.find(name);
                if (lines != byName.end())
                {
                    for (const Step* step : lines->second)
                    {
                        if (hex16(step->decl) == decl)
                        {
                            return step;
                        }
                    }
                }
                return nullptr;
            }

            // The lines that follow `decl` under `name`: steps and the line
            // that ends the chain.
            std::vector<const Step*> children(std::string_view name, const std::string& decl) const
            {
                std::vector<const Step*> out;
                const auto               lines = byName.find(name);
                if (lines != byName.end())
                {
                    for (const Step* step : lines->second)
                    {
                        if (step->parent == decl)
                        {
                            out.push_back(step);
                        }
                    }
                }
                return out;
            }

            // The line that ends a name's chain: the last one, as a reused
            // name may have been retired before.
            const Step* end(std::string_view name) const
            {
                const Step* last  = nullptr;
                S32         after = -1;
                const auto  lines = byName.find(name);
                for (const Step* step : lines != byName.end() ? lines->second : std::vector<const Step*>())
                {
                    if (step->kind == Step::Kind::Step)
                    {
                        continue;
                    }
                    const Step* parent = find(name, step->parent);
                    const S32   at     = parent ? parent->number : 0;
                    if (at > after)
                    {
                        after = at;
                        last  = step;
                    }
                }
                return last;
            }
        };

        std::optional<S64> integerOf(const Json& json)
        {
            if (json.kind != Json::Kind::Number || json.text.empty())
            {
                return std::nullopt;
            }
            char*     end   = nullptr;
            const S64 value = std::strtoll(json.text.c_str(), &end, 10);
            return *end == '\0' ? std::optional<S64>(value) : std::nullopt;
        }

        bool isHex16(const Json* json)
        {
            return json && json->kind == Json::Kind::String && json->text.size() == 16 &&
                   json->text.find_first_not_of("0123456789abcdef") == std::string::npos;
        }

        // One line of the lock, or nothing, with what is wrong with it.
        std::optional<Step> readStep(const std::string& text, std::string& wrong)
        {
            Json json;
            if (!parseJson(text, json, wrong) || json.kind != Json::Kind::Object)
            {
                return std::nullopt;
            }
            wrong = "a line is a step, {\"name\", \"step\", \"decl\", \"parent\", \"type\", \"unit\", \"choices\", "
                    "\"migrate\"}, or the end of a name's chain, {\"name\", \"decl\", \"parent\", \"retired\" | "
                    "\"renamed_to\"}";
            Step        step;
            const Json* decl   = json.find("decl");
            const Json* parent = json.find("parent");
            step.name          = std::string(json.string("name"));
            if (step.name.empty() || !isHex16(decl) || !parent || parent->kind != Json::Kind::String ||
                (!parent->text.empty() && !isHex16(parent)))
            {
                return std::nullopt;
            }
            step.decl   = std::strtoull(decl->text.c_str(), nullptr, 16);
            step.parent = parent->text;
            if (const Json* retired = json.find("retired"))
            {
                step.kind = Step::Kind::Retired;
                return retired->kind == Json::Kind::Bool && retired->boolean ? std::optional<Step>(step) : std::nullopt;
            }
            if (const Json* renamed = json.find("renamed_to"))
            {
                step.kind      = Step::Kind::Renamed;
                step.renamedTo = renamed->text;
                return renamed->kind == Json::Kind::String && !renamed->text.empty() ? std::optional<Step>(step)
                                                                                    : std::nullopt;
            }
            const Json* number  = json.find("step");
            const Json* migrate = json.find("migrate");
            const Json* choices = json.find("choices");
            const auto  type    = ALCVarTypes::fromName(json.string("type"));
            const auto  unit    = ALCVarUnits::fromName(json.string("unit"));
            const auto  n       = number ? integerOf(*number) : std::nullopt;
            if (!n || *n < 1 || *n > (1 << 30) || !type || !unit || !json.find("unit") || !migrate ||
                migrate->kind != Json::Kind::String || (!migrate->text.empty() && !isHex16(migrate)) || !choices ||
                choices->kind != Json::Kind::Array)
            {
                return std::nullopt;
            }
            step.number  = S32(*n);
            step.type    = *type;
            step.unit    = *unit;
            step.migrate = migrate->text;
            for (const Json& item : choices->items)
            {
                const Json* value = item.find("value");
                LockChoice  choice;
                if (!value)
                {
                    return std::nullopt;
                }
                if (const auto i = integerOf(*value); i && ALCVarTypes::isInteger(step.type))
                {
                    choice.number = *i;
                }
                else if (value->kind == Json::Kind::String && !ALCVarTypes::isInteger(step.type))
                {
                    choice.text = value->text;
                }
                else
                {
                    return std::nullopt;
                }
                if (const Json* was = item.find("was"))
                {
                    for (const Json& old : was->items)
                    {
                        if (const auto i = integerOf(old))
                        {
                            choice.wasNumbers.push_back(*i);
                        }
                        else if (old.kind == Json::Kind::String)
                        {
                            choice.wasTexts.push_back(old.text);
                        }
                        else
                        {
                            return std::nullopt;
                        }
                    }
                }
                step.choices.push_back(std::move(choice));
            }
            return step;
        }

        // Reads a lock, what is wrong with it into `problems`. None is an
        // empty lock. Lines are taken in any order, a line merged in twice
        // once; each line's decl is its own, and each follows a line the
        // lock holds.
        void readLock(const std::string& path, Lock& lock, Diagnostics& problems)
        {
            lock.path = path;
            std::ifstream in(fs::path(path), std::ios::binary);
            U32           number = 0;
            for (std::string text; std::getline(in, text);)
            {
                ++number;
                if (!text.empty() && text.back() == '\r')
                {
                    text.pop_back();
                }
                if (text.find_first_not_of(" \t") == std::string::npos)
                {
                    continue;
                }
                std::string wrong;
                auto        step = readStep(text, wrong);
                if (!step)
                {
                    problems.error(path, number, "", "not a line of the lock: " + wrong);
                    continue;
                }
                step->text = text;
                step->line = number;
                if (fingerprintOf(*step) != step->decl)
                {
                    problems.error(path, number, step->name,
                                   (step->kind == Step::Kind::Step ? "step " + std::to_string(step->number) + "'s"
                                                                   : std::string("the line that ends its chain's")) +
                                       " decl is not its own: the lock is written by its target, not by hand");
                    continue;
                }
                const auto same = std::find_if(lock.lines.begin(), lock.lines.end(), [&](const Step& line) {
                    return line.name == step->name && line.decl == step->decl;
                });
                if (same == lock.lines.end())
                {
                    lock.lines.push_back(std::move(*step));
                }
                else if (same->text != step->text)
                {
                    problems.error(path, number, step->name, "two lines of " + hex16(step->decl));
                }
            }
            for (const Step& step : lock.lines)
            {
                lock.byName[step.name].push_back(&step);
            }
            for (const Step& step : lock.lines)
            {
                bool held = step.parent.empty();
                for (const Step& other : lock.lines)
                {
                    held = held || hex16(other.decl) == step.parent;
                }
                if (!held)
                {
                    problems.error(path, step.line, step.name,
                                   (step.kind == Step::Kind::Step ? "step " + std::to_string(step.number)
                                                                  : std::string("the line that ends its chain")) +
                                       " follows a step the lock does not hold: a line, once committed, stays");
                }
            }
        }

        // The sort a lock's lines take: by name, then step, a chain's end
        // after the step it follows.
        double orderOf(const Lock& lock, const std::vector<Step>& added, const Step& step)
        {
            if (step.kind == Step::Kind::Step)
            {
                return step.number;
            }
            const Step* parent = lock.find(step.name, step.parent);
            for (const Step& other : added)
            {
                parent = parent ? parent : (other.name == step.name && hex16(other.decl) == step.parent ? &other : nullptr);
            }
            return (parent ? parent->number : 0) + 0.5;
        }

        // What becomes of the stored values of a choice a step takes away:
        // the default, or what its migrate gives them.
        std::string fateOf(const std::string& value, const Step& prev, const Migrate* migrate)
        {
            const std::string become = "of it become ";
            if (!migrate || migrate->kind == ALCVarMigrateKind::Reset)
            {
                return "of it take the default";
            }
            if (migrate->kind != ALCVarMigrateKind::Map)
            {
                return "of it migrate as the entry says";
            }
            for (const MapEntry& e : migrate->map)
            {
                const bool listed = prev.type == ALCVarType::String ? e.from == value
                                                                     : e.fromNumber && compact(*e.fromNumber) == value;
                if (listed)
                {
                    return become + (e.toNumber ? compact(*e.toNumber) : e.toText);
                }
            }
            switch (migrate->otherwise)
            {
                case ALCVarMapElse::Value:   return become + (migrate->elseText ? *migrate->elseText : compact(migrate->elseValue));
                case ALCVarMapElse::Default:
                case ALCVarMapElse::Keep:    break;
            }
            return "of it take the default";
        }

        // What each step since the lock does to stored values, a note a
        // change.
        void noteStep(const Setting& s, const Step& prev, const Step& cur, const Migrate* migrate,
                      std::vector<Message>& notes)
        {
            const std::string at       = " (step " + std::to_string(cur.number) + "); stored values ";
            const std::string answered = "migrate as the entry says";
            if (prev.name != cur.name)
            {
                notes.push_back({ s.file, s.line, "", prev.name + " -> " + cur.name + at + "follow the rename" });
            }
            bool said = prev.name != cur.name;
            if (prev.type != cur.type)
            {
                const ALCVarConvertible how = ALCVarConvert::convertible(prev.type, cur.type);
                notes.push_back({ s.file, s.line, s.name,
                                  std::string(ALCVarTypes::nameOf(prev.type)) + " -> " +
                                      std::string(ALCVarTypes::nameOf(cur.type)) + at +
                                      (migrate                           ? answered
                                       : how == ALCVarConvertible::Exact ? std::string("convert exactly")
                                                                         : std::string("convert where they fit, clamped or rounded")) });
                said = true;
            }
            if (prev.unit != cur.unit)
            {
                const auto  scale = ALCVarUnits::scale(prev.unit, cur.unit);
                std::string what  = prev.unit == ALCVarUnit::None || cur.unit == ALCVarUnit::None
                                        ? std::string("are kept as they are")
                                    : migrate ? answered
                                    : scale   ? "scale by " + number(*scale)
                                              : answered;
                notes.push_back({ s.file, s.line, s.name, unitName(prev.unit) + " -> " + unitName(cur.unit) + at + what });
                said = true;
            }
            for (const LockChoice& old : prev.choices)
            {
                const std::string value = choiceValue(prev, old);
                bool              kept  = false;
                for (const LockChoice& choice : cur.choices)
                {
                    kept = kept || choiceValue(cur, choice) == value ||
                           std::find(choice.wasTexts.begin(), choice.wasTexts.end(), value) != choice.wasTexts.end();
                    for (const S64 n : choice.wasNumbers)
                    {
                        kept = kept || std::to_string(n) == value;
                    }
                }
                if (!kept && !cur.choices.empty())
                {
                    notes.push_back({ s.file, s.line, s.name, "the choice " + value + " is gone" + at + fateOf(value, prev, migrate) });
                    said = true;
                }
            }
            if (!said)
            {
                notes.push_back({ s.file, s.line, s.name,
                                  std::string(migrate && migrate->kind == ALCVarMigrateKind::Join ? "joined" : "migrated") + at +
                                      answered });
            }
        }

        // Where each value of a setting's choices has gone by its last
        // step, through every step's `was`: a value to the choice that
        // holds it now.
        std::map<std::string, std::string> foldChoices(const std::vector<Step>& steps)
        {
            std::map<std::string, std::string> to;
            for (const Step& step : steps)
            {
                if (step.kind != Step::Kind::Step)
                {
                    continue;
                }
                std::map<std::string, std::string> next;
                for (const LockChoice& choice : step.choices)
                {
                    const std::string value = choiceValue(step, choice);
                    std::vector<std::string> was;
                    for (const S64 n : choice.wasNumbers)
                    {
                        was.push_back(std::to_string(n));
                    }
                    was.insert(was.end(), choice.wasTexts.begin(), choice.wasTexts.end());
                    for (const std::string& old : was)
                    {
                        next[old] = value;
                        for (const auto& [from, now] : to)
                        {
                            if (now == old)
                            {
                                next[from] = value;
                            }
                        }
                    }
                    next.emplace(value, value);
                }
                // What still holds where it was, unless this step offers it
                // no more.
                for (const auto& [from, now] : to)
                {
                    if (!next.contains(from) && next.contains(now) && next[now] == now)
                    {
                        next[from] = now;
                    }
                }
                to = std::move(next);
            }
            return to;
        }

        // Whether a declaration changes what stored values of its last step
        // mean: its type, its unit, or a choice taken away or given to
        // another. One added changes nothing stored.
        bool movesFrom(const Step& last, const Step& now, const std::vector<Step>& history)
        {
            if (last.type != now.type || last.unit != now.unit)
            {
                return true;
            }
            std::set<std::string> live;
            for (const LockChoice& choice : now.choices)
            {
                live.insert(choiceValue(now, choice));
            }
            for (const LockChoice& choice : last.choices)
            {
                if (!live.contains(choiceValue(last, choice)))
                {
                    return true;
                }
            }
            const auto before = foldChoices(history);
            for (const auto& [from, to] : foldChoices({ now }))
            {
                const auto was = before.find(from);
                if (from != to && (was == before.end() || was->second != to))
                {
                    return true;
                }
            }
            return false;
        }

        const Migrate* migrateOf(const Setting& s, S32 step)
        {
            const auto found =
                std::find_if(s.migrates.begin(), s.migrates.end(), [&](const Migrate& m) { return m.step == step; });
            return found != s.migrates.end() ? &*found : nullptr;
        }

        S16 aliasIndex(const Setting& s, std::string_view name)
        {
            for (size_t i = 0; i < s.aliases.size(); ++i)
            {
                if (s.aliases[i].name == name)
                {
                    return S16(i);
                }
            }
            return -1;
        }

        size_t renamesIn(const Setting& s)
        {
            size_t renames = 0;
            for (size_t k = 1; k < s.steps.size(); ++k)
            {
                renames += s.steps[k - 1].name != s.steps[k].name ? 1 : 0;
            }
            return renames;
        }

        // A declaration's choices with every value they were at any step,
        // through each step's `was`: a choice renamed twice still takes its
        // first name's values. A value offered again now is its own.
        void foldWas(Setting& s)
        {
            const auto folded = foldChoices(s.steps);
            for (Choice& choice : s.choices)
            {
                const bool  integer = ALCVarTypes::isInteger(s.type);
                const std::string value = integer ? std::to_string(choice.number) : choice.text;
                for (const auto& [from, to] : folded)
                {
                    const bool live = std::any_of(s.choices.begin(), s.choices.end(), [&](const Choice& other) {
                        return (integer ? std::to_string(other.number) : other.text) == from;
                    });
                    if (to != value || from == value || live)
                    {
                        continue;
                    }
                    if (integer && std::find(choice.wasNumbers.begin(), choice.wasNumbers.end(), std::strtoll(from.c_str(), nullptr, 10)) ==
                                       choice.wasNumbers.end())
                    {
                        choice.wasNumbers.push_back(std::strtoll(from.c_str(), nullptr, 10));
                    }
                    else if (!integer && std::find(choice.wasTexts.begin(), choice.wasTexts.end(), from) == choice.wasTexts.end())
                    {
                        choice.wasTexts.push_back(from);
                    }
                }
            }
        }

        // Each declaration's history worked out from its entry and checked
        // against the lock, and every line of the lock accounted for.
        class History
        {
        public:
            History(Schema& schema, const Lock& lock, bool haveLock, Diagnostics& diag, std::vector<Message>& notes)
            :   mSchema(schema),
                mLock(lock),
                mHave(haveLock),
                mDiag(diag),
                mNotes(notes)
            {
            }

            // The lines the lock does not hold yet.
            std::vector<Step> added;

            void run(bool retire)
            {
                for (const auto& setting : mSchema.settings)
                {
                    settle(*setting);
                    if (setting->steps.empty())
                    {
                        Step step   = stepOf(*setting);
                        step.number = 1;
                        step.decl   = fingerprintOf(step);
                        setting->steps.push_back(step);
                    }
                }
                for (const auto& setting : mSchema.settings)
                {
                    sources(*setting);
                }
                account(retire);
            }

            // Every line of a base branch's lock still in this one.
            void checkBase(const Lock& base)
            {
                for (const Step& line : base.lines)
                {
                    if (!mLock.find(line.name, hex16(line.decl)))
                    {
                        mDiag.error(base.path, line.line, line.name,
                                    (line.kind == Step::Kind::Step
                                         ? "step " + std::to_string(line.number) + " (" + hex16(line.decl) + ")"
                                         : std::string("the line that ends its chain")) +
                                        " is missing from the lock: a line, once committed, stays, and a step after it "
                                        "puts right what was wrong with it");
                    }
                }
            }

        private:
            Schema&                   mSchema;
            const Lock&               mLock;
            bool                      mHave;
            Diagnostics&              mDiag;
            std::vector<Message>&     mNotes;
            // The names a declaration's history takes in.
            std::set<std::string, std::less<>> mTaken;

            void grow(Setting& s, Step step, const std::string& parent, S32 number, std::string migrate)
            {
                step.kind    = Step::Kind::Step;
                step.name    = s.name;
                step.text.clear();
                step.line    = 0;
                step.alias   = -1;
                step.parent  = parent;
                step.number  = number;
                step.migrate = std::move(migrate);
                step.decl    = fingerprintOf(step);
                s.steps.push_back(step);
                added.push_back(step);
            }

            // A name's steps from the one after `parent`, the entry's own
            // branch taken at a fork: the branch its join does not answer.
            // False at a fork no join resolves.
            bool walk(Setting& s, std::string_view name, std::string parent, S16 alias)
            {
                for (;;)
                {
                    std::vector<const Step*> next;
                    for (const Step* child : mLock.children(name, parent))
                    {
                        if (child->kind == Step::Kind::Step)
                        {
                            next.push_back(child);
                        }
                    }
                    if (next.empty())
                    {
                        return true;
                    }
                    if (next.size() > 1)
                    {
                        std::vector<const Step*> ours;
                        for (const Step* child : next)
                        {
                            if (!answers(s, name, child))
                            {
                                ours.push_back(child);
                            }
                        }
                        if (ours.size() != 1)
                        {
                            std::vector<std::string> branches;
                            for (const Step* child : next)
                            {
                                branches.push_back(hex16(child->decl) + " (" + std::string(ALCVarTypes::nameOf(child->type)) +
                                                   ", " + unitName(child->unit) + ")");
                            }
                            const S32 after = next.front()->number - 1;
                            mDiag.error(s.file, s.line, s.name,
                                        "two steps follow step " + std::to_string(after) + ": " + join(branches, " and ") +
                                            "; keep one branch's migrate list and end it with a join answering the other's "
                                            "steps: migrate = [..., { step = <n>, join = { \"<decl>\" = { ... } } }]");
                            return false;
                        }
                        next = ours;
                    }
                    Step step  = *next.front();
                    step.alias = alias;
                    s.steps.push_back(step);
                    parent = hex16(step.decl);
                }
            }

            // Whether every step from `step` on, in its branch, is answered
            // by one of the entry's joins.
            bool answers(const Setting& s, std::string_view name, const Step* step)
            {
                bool answered = false;
                for (const Migrate& m : s.migrates)
                {
                    for (const auto& [decl, answer] : m.join)
                    {
                        answered = answered || decl == step->decl;
                    }
                }
                for (const Step* child : mLock.children(name, hex16(step->decl)))
                {
                    answered = answered && (child->kind != Step::Kind::Step || answers(s, name, child));
                }
                return answered;
            }

            // The joins' answers placed by the steps they answer.
            void placeJoins(Setting& s)
            {
                for (const Migrate& m : s.migrates)
                {
                    for (const auto& [decl, answer] : m.join)
                    {
                        const Step* step = nullptr;
                        for (const auto& [name, lines] : mLock.byName)
                        {
                            for (const Step* line : lines)
                            {
                                step = step ? step : (line->decl == decl && line->kind == Step::Kind::Step ? line : nullptr);
                            }
                        }
                        const bool ours = std::any_of(s.steps.begin(), s.steps.end(),
                                                      [&](const Step& own) { return own.decl == decl; });
                        if (!step || ours)
                        {
                            mDiag.error(s.file, m.line, s.name,
                                        "the join's " + hex16(decl) + " is no step of another branch of its history");
                            continue;
                        }
                        s.joined[m.step].push_back({ decl, step->type, step->unit, answer });
                    }
                }
            }

            // Where a declaration's history begins: the chain of its old
            // name if it continues one, and the parent of its own first step.
            void settle(Setting& s)
            {
                s.steps.clear();
                mTaken.insert(s.name);
                const size_t mark  = mNotes.size();
                const Step*  ended = mLock.end(s.name);
                std::string  root;
                bool         rooted = false;
                const Alias* renamed = nullptr;

                if (ended && !s.reuse)
                {
                    mDiag.error(s.file, s.line, s.name,
                                (ended->kind == Step::Kind::Retired ? std::string("retired")
                                                                    : "renamed to " + ended->renamedTo) +
                                    "; declaring the name again needs reuse = true, which begins a history its old values "
                                    "never meet");
                }
                if (s.reuse && !ended)
                {
                    mDiag.error(s.file, s.line, s.name, "reuse = true, but the name was never renamed away or retired");
                }
                if (s.reuse && ended)
                {
                    root   = hex16(ended->decl);
                    rooted = true;
                }
                else if (mLock.byName.contains(s.name))
                {
                    // Its own chain, or one it continues: the parent of its
                    // own first step names it.
                    const auto& lines = mLock.byName.find(s.name)->second;
                    for (const Step* line : lines)
                    {
                        if (line->kind == Step::Kind::Step && !mLock.find(s.name, line->parent))
                        {
                            root   = line->parent;
                            rooted = true;
                        }
                    }
                    if (!root.empty() && !before(s, root))
                    {
                        mDiag.error(s.file, s.line, s.name,
                                    "its history begins after a step of no old name it declares: keep the alias of the "
                                    "name it was renamed from");
                        return;
                    }
                }
                else
                {
                    // Not in the lock: new, or the new name of one that is.
                    for (const Alias& alias : s.aliases)
                    {
                        const Step* aliasEnd = mLock.end(alias.name);
                        if (!mLock.byName.contains(alias.name) ||
                            (aliasEnd && aliasEnd->kind == Step::Kind::Renamed && aliasEnd->renamedTo != s.name))
                        {
                            continue;
                        }
                        if (aliasEnd && aliasEnd->kind == Step::Kind::Retired)
                        {
                            mDiag.error(alias.file, alias.line, s.name, "the old name " + alias.name + " was retired");
                            continue;
                        }
                        if (renamed)
                        {
                            mDiag.error(alias.file, alias.line, s.name,
                                        "a rename continues one old name's history: " + renamed->name + " and " +
                                            alias.name + " both have one");
                            continue;
                        }
                        renamed = &alias;
                    }
                    if (renamed && !continueFrom(s, renamed->name))
                    {
                        return;
                    }
                }
                if (!walk(s, s.name, root, -1))
                {
                    return;
                }
                placeJoins(s);
                const size_t locked = s.steps.size();
                verify(s);

                // What the declaration is now, past what the lock records.
                const Step now  = stepOf(s);
                S32        next = s.steps.empty() ? 1 : s.steps.back().number + 1;
                if (s.steps.empty() && s.reuse && ended)
                {
                    const Step* last = mLock.find(s.name, ended->parent);
                    next             = last ? last->number + 1 : 1;
                }
                if (renamed)
                {
                    const Step& last = s.steps.back();
                    Step        end;
                    end.kind      = Step::Kind::Renamed;
                    end.name      = last.name;
                    end.parent    = hex16(last.decl);
                    end.renamedTo = s.name;
                    end.decl      = fingerprintOf(end);
                    added.push_back(end);
                    grow(s, now, hex16(last.decl), next++, renameHash(*renamed));
                }
                else if (s.steps.empty() || (rooted && s.steps.back().name != s.name))
                {
                    if (const Migrate* m = migrateOf(s, next))
                    {
                        mDiag.error(s.file, m->line, s.name,
                                    "a migrate begins a step after the first, which is the setting as declared");
                    }
                    grow(s, now, root, next++, "");
                }
                else if (movesFrom(s.steps.back(), now, s.steps) || migrateOf(s, next))
                {
                    const Migrate* m = migrateOf(s, next);
                    grow(s, now, hex16(s.steps.back().decl), next++, m ? migrateHash(*m) : "");
                }
                // A new setting declared with its first migrate at once.
                if (locked == 0 && !renamed && migrateOf(s, next))
                {
                    grow(s, now, hex16(s.steps.back().decl), next, migrateHash(*migrateOf(s, next)));
                    ++next;
                }
                for (const Migrate& m : s.migrates)
                {
                    const bool held = std::any_of(s.steps.begin(), s.steps.end(),
                                                  [&](const Step& step) { return step.number == m.step; });
                    if (held && m.step == s.steps.front().number)
                    {
                        mDiag.error(s.file, m.line, s.name,
                                    "a migrate begins a step after the first, which is the setting as declared");
                    }
                    else if (!held)
                    {
                        mDiag.error(s.file, m.line, s.name,
                                    "one change is one step: a migrate here begins step " + std::to_string(next) +
                                        " at most, and names a step its history holds");
                    }
                }
                check(s, locked);
                if (mHave && locked == 0)
                {
                    mNotes.insert(mNotes.begin() + S64(mark),
                                  Message{ s.file, s.line, s.name, "declared (step " + std::to_string(s.steps.front().number) + ")" });
                }
                if (!s.reuse || !ended)
                {
                    return;
                }
                // The setting the name was before: its rows take the default.
                for (const Step* line : mLock.byName.find(s.name)->second)
                {
                    const bool ours = std::any_of(s.steps.begin(), s.steps.end(),
                                                  [&](const Step& step) { return step.decl == line->decl; });
                    if (!ours && line->kind == Step::Kind::Step)
                    {
                        s.superseded.push_back(line->decl);
                    }
                }
            }

            // Whether `root`, the parent of the setting's own first step, is
            // the last step of an old name it declares, whose history it then
            // takes in first.
            bool before(Setting& s, const std::string& root)
            {
                for (const Alias& alias : s.aliases)
                {
                    const Step* last = mLock.find(alias.name, root);
                    const Step* end  = mLock.end(alias.name);
                    if (last && end && end->kind == Step::Kind::Renamed && end->renamedTo == s.name)
                    {
                        return continueFrom(s, alias.name);
                    }
                }
                return false;
            }

            // The history of an old name, which may be an older one's new.
            bool continueFrom(Setting& s, const std::string& name)
            {
                mTaken.insert(name);
                std::string root;
                for (const Step* line : mLock.byName.find(name)->second)
                {
                    if (line->kind == Step::Kind::Step && !mLock.find(name, line->parent))
                    {
                        root = line->parent;
                    }
                }
                if (!root.empty())
                {
                    bool found = false;
                    for (const Alias& alias : s.aliases)
                    {
                        const Step* end = mLock.end(alias.name);
                        if (alias.name != name && mLock.find(alias.name, root) && end &&
                            end->kind == Step::Kind::Renamed && end->renamedTo == name)
                        {
                            found = continueFrom(s, alias.name);
                        }
                    }
                    if (!found)
                    {
                        mDiag.error(s.file, s.line, s.name,
                                    name + "'s history begins after a step of a name it does not declare as an alias");
                        return false;
                    }
                }
                return walk(s, name, root, aliasIndex(s, name));
            }

            // Each step the lock records, worked out again from the entry: a
            // migrate edited or deleted, or a rename's codec changed, gives
            // another fingerprint.
            void verify(const Setting& s)
            {
                for (size_t k = 0; k < s.steps.size(); ++k)
                {
                    Step        step = s.steps[k];
                    const bool  rename = k > 0 && s.steps[k - 1].name != step.name;
                    std::string migrate;
                    if (rename)
                    {
                        const S16 alias = aliasIndex(s, s.steps[k - 1].name);
                        migrate         = alias >= 0 ? renameHash(s.aliases[size_t(alias)]) : "";
                        if (const Migrate* m = migrateOf(s, step.number))
                        {
                            mDiag.error(s.file, m->line, s.name,
                                        "step " + std::to_string(step.number) +
                                            " is the rename: its migrate is the old name's codec, not one of its own");
                        }
                    }
                    else if (const Migrate* m = migrateOf(s, step.number); m && k > 0)
                    {
                        migrate = migrateHash(*m);
                    }
                    step.migrate = migrate;
                    if (fingerprintOf(step) != s.steps[k].decl)
                    {
                        mDiag.error(s.file, s.line, s.name,
                                    "step " + std::to_string(step.number) + " is not the step the lock records (" +
                                        hex16(s.steps[k].decl) + ")" +
                                        (rename ? ": the old name's codec was changed"
                                                : ": its migrate was edited, added or deleted") +
                                        "; a recorded step stays, and a step after it puts right what was wrong with it");
                    }
                }
            }

            // Every step from the one before: by the conversions, or by a
            // migrate of its own.
            // A map's keys values the step before could hold, as it held
            // them: numbers of a number's step, strings of a string's, and
            // among its choices where it had them; and its values, its else's
            // too, values of its own step.
            void checkMap(const Setting& s, const Answer& a, const Step& prev, const Step& cur, Where line,
                          const std::string& whose)
            {
                const std::string p      = std::to_string(prev.number);
                const std::string n      = std::to_string(cur.number);
                const bool        textIn  = prev.type == ALCVarType::String;
                const bool        textOut = cur.type == ALCVarType::String;
                if ((!textIn && !ALCVarTypes::isNumber(prev.type)) || (!textOut && !ALCVarTypes::isNumber(cur.type)))
                {
                    mDiag.error(s.file, line, s.name,
                                whose + "a map takes a number or a string to a number or a string, and step " + n + " is " +
                                    std::string(ALCVarTypes::nameOf(prev.type)) + " -> " +
                                    std::string(ALCVarTypes::nameOf(cur.type)));
                    return;
                }
                auto chosen = [&](const Step& step, const std::optional<F64>& numeric, const std::string& text) {
                    return step.choices.empty() ||
                           std::any_of(step.choices.begin(), step.choices.end(), [&](const LockChoice& c) {
                               return step.type == ALCVarType::String ? c.text == text : numeric && F64(c.number) == *numeric;
                           });
                };
                auto given = [&](const std::string& what, const std::optional<F64>& numeric, const std::string& text) {
                    const std::string value = numeric ? compact(*numeric) : text;
                    if (textOut == numeric.has_value())
                    {
                        mDiag.error(s.file, line, s.name,
                                    whose + what + " " + value + ", and step " + n + " holds " +
                                        (textOut ? "a string" : "a number"));
                    }
                    else if (!chosen(cur, numeric, text))
                    {
                        mDiag.error(s.file, line, s.name, whose + what + " " + value + ", which is not among the choices");
                    }
                };
                for (const MapEntry& e : a.map)
                {
                    if (!textIn && !e.fromNumber)
                    {
                        mDiag.error(s.file, line, s.name,
                                    whose + "the map's key " + e.from + " is not a number, as step " + p + " holds");
                    }
                    else if (!chosen(prev, e.fromNumber, e.from))
                    {
                        mDiag.error(s.file, line, s.name,
                                    whose + "the map's key " + e.from + " is not among step " + p + "'s choices");
                    }
                    given("the map gives", e.toNumber, e.toText);
                }
                if (a.otherwise == ALCVarMapElse::Value)
                {
                    given("the map's else gives", a.elseText ? std::nullopt : std::optional<F64>(a.elseValue),
                          a.elseText.value_or(""));
                }
            }

            void check(const Setting& s, size_t locked)
            {
                for (size_t k = 1; k < s.steps.size(); ++k)
                {
                    const size_t      failed = mDiag.errors.size();
                    const Step&       prev   = s.steps[k - 1];
                    const Step&       cur    = s.steps[k];
                    const bool        rename = prev.name != cur.name;
                    const Migrate*    m      = rename ? nullptr : migrateOf(s, cur.number);
                    const std::string n      = std::to_string(cur.number);
                    const std::string types  = std::string(ALCVarTypes::nameOf(prev.type)) + " -> " +
                                              std::string(ALCVarTypes::nameOf(cur.type)) + " (step " + n + ")";
                    const std::string units = unitName(prev.unit) + " -> " + unitName(cur.unit) + " (step " + n + ")";
                    const bool        typed = ALCVarConvert::convertible(prev.type, cur.type) != ALCVarConvertible::No;
                    const bool        scaled = prev.unit == cur.unit || prev.unit == ALCVarUnit::None ||
                                        cur.unit == ALCVarUnit::None || ALCVarUnits::scale(prev.unit, cur.unit);
                    const bool        joins  = m && m->kind == ALCVarMigrateKind::Join;
                    if (!m && !rename && sameShape(prev, cur))
                    {
                        mDiag.error(s.file, s.line, s.name,
                                    "step " + n + " is step " + std::to_string(prev.number) +
                                        " again with no migrate of its own: a migrate stays in its entry while the lock "
                                        "holds its step");
                    }
                    // A value of a type nothing converts is never a number a
                    // map or a scale could take: fn or reset answer it. A
                    // join answers the other branch, not this one.
                    if (!typed && !rename && (!m || joins || m->kind == ALCVarMigrateKind::Map || m->kind == ALCVarMigrateKind::Scale))
                    {
                        mDiag.error(s.file, s.line, s.name,
                                    types + " has no automatic conversion; add migrate = [{ step = " + n +
                                        ", fn = \"...\" }], or [{ step = " + n + ", reset = true }] to take the default");
                    }
                    else if (!scaled && (!m || joins))
                    {
                        mDiag.error(s.file, s.line, s.name,
                                    units + " do not scale into one another; add migrate = [{ step = " + n +
                                        ", scale = ... }], or [{ step = " + n + ", reset = true }]");
                    }
                    if (m && m->kind == ALCVarMigrateKind::Scale &&
                        (!ALCVarTypes::isNumber(prev.type) || !ALCVarTypes::isNumber(cur.type)))
                    {
                        mDiag.error(s.file, m->line, s.name,
                                    "scale and offset take a number to a number, and step " + n + " is " +
                                        std::string(ALCVarTypes::nameOf(prev.type)) + " -> " +
                                        std::string(ALCVarTypes::nameOf(cur.type)));
                    }
                    if (m && m->kind == ALCVarMigrateKind::Map)
                    {
                        checkMap(s, *m, prev, cur, m->line, "");
                    }
                    const auto joined = s.joined.find(cur.number);
                    if (joins && joined != s.joined.end())
                    {
                        for (const Joined& other : joined->second)
                        {
                            if (other.answer.kind == ALCVarMigrateKind::Scale &&
                                (!ALCVarTypes::isNumber(other.type) || !ALCVarTypes::isNumber(cur.type)))
                            {
                                mDiag.error(s.file, m->line, s.name,
                                            "the join's answer to " + hex16(other.decl) +
                                                " takes a number, and that step is " +
                                                std::string(ALCVarTypes::nameOf(other.type)));
                            }
                            if (other.answer.kind == ALCVarMigrateKind::Map)
                            {
                                Step branch;
                                branch.type   = other.type;
                                branch.unit   = other.unit;
                                branch.number = cur.number - 1;
                                checkMap(s, other.answer, branch, cur, m->line, "the join's answer to " + hex16(other.decl) + ": ");
                            }
                        }
                    }
                    // A step that fails says so, and nothing says what it does.
                    if (mHave && k >= locked && mDiag.errors.size() == failed)
                    {
                        noteStep(s, prev, cur, m, mNotes);
                    }
                }
            }

            // A source's history: the steps its rows are brought through.
            void sources(Setting& s)
            {
                for (const From& from : s.from)
                {
                    Source source{ from, {} };
                    if (const auto declared = mSchema.settingByName.find(from.name); declared != mSchema.settingByName.end())
                    {
                        source.steps    = declared->second->steps;
                        source.declared = declared->second;
                    }
                    else if (mLock.byName.contains(from.name))
                    {
                        std::string parent;
                        for (const Step* line : mLock.byName.find(from.name)->second)
                        {
                            if (line->kind == Step::Kind::Step && !mLock.find(from.name, line->parent))
                            {
                                parent = line->parent;
                            }
                        }
                        for (std::vector<const Step*> next; !(next = mLock.children(from.name, parent)).empty();)
                        {
                            const auto step = std::find_if(next.begin(), next.end(),
                                                           [](const Step* line) { return line->kind == Step::Kind::Step; });
                            if (step == next.end())
                            {
                                break;
                            }
                            source.steps.push_back(**step);
                            parent = hex16((*step)->decl);
                        }
                    }
                    else if (mHave)
                    {
                        mDiag.error(s.file, from.line, s.name,
                                    from.name +
                                        " is not in the lock: a setting takes its first value from one declared, renamed "
                                        "or retired");
                    }
                    s.sources.push_back(std::move(source));
                }
            }

            // Every name in the lock declared, an old name of one, renamed or
            // retired: a name gone without a word drops every user's value.
            void account(bool retire)
            {
                for (const auto& [name, lines] : mLock.byName)
                {
                    const Step* end = mLock.end(name);
                    if (mTaken.contains(name) || end)
                    {
                        continue;
                    }
                    bool aliased = false;
                    for (const auto& setting : mSchema.settings)
                    {
                        aliased = aliased || aliasIndex(*setting, name) >= 0;
                    }
                    if (aliased)
                    {
                        continue;
                    }
                    std::string parent;
                    const Step* last = nullptr;
                    for (std::vector<const Step*> next; !(next = mLock.children(name, parent)).empty();)
                    {
                        last   = next.front();
                        parent = hex16(last->decl);
                    }
                    if (!last)
                    {
                        continue;
                    }
                    if (retire)
                    {
                        Step retired;
                        retired.kind   = Step::Kind::Retired;
                        retired.name   = name;
                        retired.parent = parent;
                        retired.decl   = fingerprintOf(retired);
                        added.push_back(retired);
                        mNotes.push_back({ mLock.path, lines.front()->line, name, "retired; its stored values are kept and ignored" });
                        continue;
                    }
                    std::string renamed;
                    for (const auto& setting : mSchema.settings)
                    {
                        const Setting& s = *setting;
                        if (renamed.empty() && !mLock.byName.contains(s.name) && sameShape(*last, stepOf(s)))
                        {
                            renamed = "; if it is " + s.name + " renamed, add alias = \"" + name + "\" to " + s.name;
                        }
                    }
                    mDiag.error(mLock.path, lines.front()->line, name,
                                "in the lock but declared nowhere: a setting deleted is retired by recording the lock" +
                                    renamed);
                }
            }
        };

        // The lock with the lines added: every line it held as it held it,
        // and each new one, sorted by name and step.
        std::string lockText(const Lock& lock, const std::vector<Step>& added)
        {
            std::vector<const Step*> all;
            for (const Step& step : lock.lines)
            {
                all.push_back(&step);
            }
            for (const Step& step : added)
            {
                all.push_back(&step);
            }
            std::stable_sort(all.begin(), all.end(), [&](const Step* a, const Step* b) {
                return a->name != b->name ? a->name < b->name : orderOf(lock, added, *a) < orderOf(lock, added, *b);
            });
            std::string text;
            for (const Step* step : all)
            {
                text += (step->text.empty() ? lockLine(*step) : step->text) + "\n";
            }
            return text;
        }

        // The lines of `lock` that `committed` holds too, as `lock` holds
        // them.
        Lock heldIn(const Lock& lock, const Lock& committed)
        {
            Lock held;
            held.path = lock.path;
            for (const Step& line : lock.lines)
            {
                if (committed.find(line.name, hex16(line.decl)))
                {
                    held.lines.push_back(line);
                }
            }
            for (const Step& line : held.lines)
            {
                held.byName[line.name].push_back(&line);
            }
            return held;
        }

        // Writes `text` to `path` where it holds something else.
        bool writeText(const std::string& path, const std::string& text)
        {
            {
                std::ifstream     in(fs::path(path), std::ios::binary);
                std::stringstream existing;
                existing << in.rdbuf();
                if (in && existing.str() == text)
                {
                    return true;
                }
            }
            std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
            out << text;
            return bool(out);
        }

        // ---- writing ----------------------------------------------------

        class Writer
        {
        public:
            Writer(const Options& options, Result& result) : mOptions(options), mResult(result) {}

            void write(const std::string& relative, const std::string& content)
            {
                const fs::path path = fs::path(mOptions.outDir) / fs::path(relative);
                mResult.outputs.push_back(relative);
                std::error_code ec;
                fs::create_directories(path.parent_path(), ec);
                {
                    std::ifstream in(path, std::ios::binary);
                    if (in)
                    {
                        std::stringstream existing;
                        existing << in.rdbuf();
                        if (existing.str() == content)
                        {
                            return;
                        }
                    }
                }
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << content;
                if (!out)
                {
                    mResult.errors.push_back(said(Message{ path.generic_string(), {}, "", "cannot be written" }, "error", mOptions.msvc));
                    return;
                }
                mResult.written.push_back(relative);
            }

        private:
            const Options& mOptions;
            Result&        mResult;
        };

        const char* const GENERATED_NOTE = "// Written by alcvargen from the settings schema: edit the declaration, not this.\n";

        std::string groupHeader(const Options& options, const Group& g)
        {
            std::ostringstream out;
            out << "// " << headerOf(options, g.name) << ": the settings of the group " << g.name << ".\n"
                << GENERATED_NOTE << "\n#pragma once\n\n#include \"alcvar.h\"\n\n"
                << "namespace " << namespaceOf(options, g.name) << "\n{\n";

            std::array<U16, ALCVAR_STORAGE_COUNT> counts{};
            for (const Setting* s : g.settings)
            {
                ++counts[size_t(ALCVarTypes::storageOf(s->type))];
            }
            out << "    // The group's values, in arrays by size, and its descriptors.\n"
                << "    struct ALCVAR_GROUP_STORAGE Group_\n    {\n"
                << "        static constexpr U16 count = " << g.settings.size() << ";\n";
            if (!g.settings.empty())
            {
                out << "        static constexpr U16 slot[count] = { ";
                for (size_t i = 0; i < g.settings.size(); ++i)
                {
                    out << (i ? ", " : "") << g.settings[i]->slot;
                }
                out << " };\n";
            }
            out << "        static const ALCVarGroupDesc info;\n";
            if (!g.settings.empty())
            {
                out << "        static const ALCVarDesc desc[count];\n";
            }
            out << "        static ALCVarGroupState state;\n";
            const std::array<std::pair<ALCVarStorage, const char*>, ALCVAR_STORAGE_COUNT> arrays = {
                { { ALCVarStorage::Byte, "U8 bytes" },
                  { ALCVarStorage::Word, "U32 words" },
                  { ALCVarStorage::DWord, "U64 dwords" },
                  { ALCVarStorage::Pair, "ALCVarSeqSlot<2> pairs" },
                  { ALCVarStorage::Triple, "ALCVarSeqSlot<3> triples" },
                  { ALCVarStorage::Shared, "ALCVarSharedSlot shared" },
                  { ALCVarStorage::Boxed, "ALCVarBoxedSlot boxed" } }
            };
            for (const auto& [storage, decl] : arrays)
            {
                if (counts[size_t(storage)])
                {
                    out << "        static " << decl << "[" << counts[size_t(storage)] << "];\n";
                }
            }
            out << "    };\n\n";

            for (const Setting* s : g.settings)
            {
                out << "    // [" << s->name << "]\n"
                    << "    inline constexpr ALCVar<" << cppType(s->type) << ", Group_, " << s->keyIndex << "> "
                    << s->key << "{};\n";
            }
            out << (g.settings.empty() ? "" : "\n") << "    inline constexpr ALCVarGroup<Group_> group{};\n";

            std::vector<const Setting*> readable;
            for (const Setting* s : g.settings)
            {
                const ALCVarStorage storage = ALCVarTypes::storageOf(s->type);
                if (storage != ALCVarStorage::Shared && storage != ALCVarStorage::Boxed)
                {
                    readable.push_back(s);
                }
            }
            if (!readable.empty())
            {
                out << "\n    // The group's numbers and small values, as they were at one moment.\n"
                    << "    struct Values\n    {\n";
                for (const Setting* s : readable)
                {
                    out << "        " << cppType(s->type) << " " << s->key << ";\n";
                }
                out << "    };\n\n"
                    << "    inline Values read() noexcept\n    {\n"
                    << "        Values                    values;\n"
                    << "        ALCVarGroupReader<Group_> reader;\n"
                    << "        do\n        {\n            reader.begin();\n";
                for (const Setting* s : readable)
                {
                    out << "            values." << s->key << " = " << s->key << ".inGroup();\n";
                }
                out << "        } while (!reader.end());\n        return values;\n    }\n";
            }
            out << "}\n";
            return out.str();
        }

        std::string spanOf(const std::string& array, size_t count)
        {
            return count ? array : "{}";
        }

        std::string areaSource(const Options& options, const std::string& area, const std::vector<const Group*>& groups)
        {
            std::ostringstream out;
            out << "// The settings of the area " << area << ": their compiled defaults and descriptors.\n"
                << GENERATED_NOTE << "\n#include \"linden_common.h\"\n\n";
            // The area's groups, and those of the declared settings its
            // settings take their first values from.
            std::set<std::string> headers;
            for (const Group* g : groups)
            {
                headers.insert(headerOf(options, g->name));
                for (const Setting* s : g->settings)
                {
                    for (const Source& source : s->sources)
                    {
                        if (source.declared)
                        {
                            headers.insert(headerOf(options, source.declared->group->name));
                        }
                    }
                }
            }
            for (const auto& header : headers)
            {
                out << "#include \"" << header << "\"\n";
            }
            out << "\nnamespace\n{\n";

            // The descriptors' lists, each beside its setting's name.
            for (const Group* g : groups)
            {
                for (const Setting* s : g->settings)
                {
                    const std::string id = identifier(s->name);
                    for (size_t c = 0; c < s->choices.size(); ++c)
                    {
                        const Choice& choice = s->choices[c];
                        if (!choice.wasNumbers.empty())
                        {
                            out << "    constexpr S64 " << id << "_was" << c << "[] = { ";
                            for (size_t i = 0; i < choice.wasNumbers.size(); ++i)
                            {
                                out << (i ? ", " : "") << choice.wasNumbers[i] << "ll";
                            }
                            out << " };\n";
                        }
                        if (!choice.wasTexts.empty())
                        {
                            out << "    constexpr std::string_view " << id << "_wastext" << c << "[] = { ";
                            for (size_t i = 0; i < choice.wasTexts.size(); ++i)
                            {
                                out << (i ? ", " : "") << cppString(choice.wasTexts[i]);
                            }
                            out << " };\n";
                        }
                    }
                    if (!s->choices.empty())
                    {
                        out << "    constexpr ALCVarChoice " << id << "_choices[] = {\n";
                        for (size_t c = 0; c < s->choices.size(); ++c)
                        {
                            const Choice& choice = s->choices[c];
                            out << "        { .number = " << choice.number << "ll, .text = " << cppString(choice.text)
                                << ", .label = " << cppString(choice.label);
                            if (!choice.wasNumbers.empty())
                            {
                                out << ", .wasNumbers = " << id << "_was" << c;
                            }
                            if (!choice.wasTexts.empty())
                            {
                                out << ", .wasTexts = " << id << "_wastext" << c;
                            }
                            out << " },\n";
                        }
                        out << "    };\n";
                    }
                    if (!s->aliases.empty())
                    {
                        out << "    constexpr ALCVarAlias " << id << "_aliases[] = {\n";
                        for (const Alias& alias : s->aliases)
                        {
                            out << "        { .name = " << cppString(alias.name)
                                << ", .inverted = " << (alias.inverted ? "true" : "false")
                                << ", .stringEncoded = " << (alias.stringEncoded ? "true" : "false")
                                << ", .joined = " << cppString(alias.joined) << ", .unit = " << enumUnit(alias.unit)
                                << " },\n";
                        }
                        out << "    };\n";
                    }
                    if (!s->tags.empty())
                    {
                        out << "    constexpr std::string_view " << id << "_tags[] = { ";
                        for (size_t i = 0; i < s->tags.size(); ++i)
                        {
                            out << (i ? ", " : "") << cppString(s->tags[i]);
                        }
                        out << " };\n";
                    }
                    out << "    constexpr ALCVarStep " << id << "_steps[] = {\n";
                    for (const Step& step : s->steps)
                    {
                        out << "        { .decl = " << hex64(step.decl) << ", .type = " << enumType(step.type)
                            << ", .unit = " << enumUnit(step.unit) << ", .alias = " << step.alias
                            << ", .number = " << step.number << " },\n";
                    }
                    out << "    };\n";
                    if (!s->superseded.empty())
                    {
                        out << "    constexpr U64 " << id << "_superseded[] = { ";
                        for (size_t i = 0; i < s->superseded.size(); ++i)
                        {
                            out << (i ? ", " : "") << hex64(s->superseded[i]);
                        }
                        out << " };\n";
                    }
                    for (size_t f = 0; f < s->sources.size(); ++f)
                    {
                        out << "    constexpr ALCVarStep " << id << "_from" << f << "[] = {\n";
                        for (const Step& step : s->sources[f].steps)
                        {
                            out << "        { .decl = " << hex64(step.decl) << ", .type = " << enumType(step.type)
                                << ", .unit = " << enumUnit(step.unit) << ", .number = " << step.number << " },\n";
                        }
                        out << "    };\n";
                    }
                    if (!s->sources.empty())
                    {
                        out << "    constexpr ALCVarFrom " << id << "_from[] = {\n";
                        for (size_t f = 0; f < s->sources.size(); ++f)
                        {
                            const Source& source = s->sources[f];
                            out << "        { .name = " << cppString(source.from.name) << ", .hook = " << cppString(source.from.hook)
                                << ", .steps = " << spanOf(id + "_from" + std::to_string(f), source.steps.size());
                            if (source.declared)
                            {
                                out << ", .desc = &" << namespaceOf(options, source.declared->group->name) << "::Group_::desc["
                                    << source.declared->keyIndex << "]";
                            }
                            out << " },\n";
                        }
                        out << "    };\n";
                    }

                    // The migrates by step: the entry's, and each rename's,
                    // whose codec is its old name's alias.
                    std::vector<Migrate> migrates = s->migrates;
                    std::map<S32, S16>   renames;
                    for (size_t k = 1; k < s->steps.size(); ++k)
                    {
                        if (s->steps[k - 1].name != s->steps[k].name)
                        {
                            Migrate rename;
                            rename.step = s->steps[k].number;
                            rename.kind = ALCVarMigrateKind::Rename;
                            renames.emplace(rename.step, aliasIndex(*s, s->steps[k - 1].name));
                            migrates.push_back(rename);
                        }
                    }
                    std::stable_sort(migrates.begin(), migrates.end(),
                                     [](const Migrate& a, const Migrate& b) { return a.step < b.step; });
                    auto kindOf = [](ALCVarMigrateKind kind) {
                        switch (kind)
                        {
                            case ALCVarMigrateKind::None:   return "None";
                            case ALCVarMigrateKind::Map:    return "Map";
                            case ALCVarMigrateKind::Scale:  return "Scale";
                            case ALCVarMigrateKind::Reset:  return "Reset";
                            case ALCVarMigrateKind::Hook:   return "Hook";
                            case ALCVarMigrateKind::Rename: return "Rename";
                            case ALCVarMigrateKind::Join:   return "Join";
                        }
                        return "None";
                    };
                    auto elseOf = [](ALCVarMapElse otherwise) {
                        return otherwise == ALCVarMapElse::Keep      ? "ALCVarMapElse::Keep"
                               : otherwise == ALCVarMapElse::Default ? "ALCVarMapElse::Default"
                                                                     : "ALCVarMapElse::Value";
                    };
                    auto mapOf = [&](const std::string& name, const std::vector<MapEntry>& map) {
                        if (map.empty())
                        {
                            return;
                        }
                        out << "    constexpr ALCVarMapEntry " << name << "[] = {\n";
                        for (const MapEntry& e : map)
                        {
                            out << "        { .from = " << number(e.fromNumber.value_or(0.0))
                                << ", .to = " << number(e.toNumber.value_or(0.0)) << ", .fromText = " << cppString(e.from)
                                << ", .toText = " << cppString(e.toText) << " },\n";
                        }
                        out << "    };\n";
                    };
                    auto answerOf = [&](const Answer& a, const std::string& map) {
                        return ".kind = ALCVarMigrate::Kind::" + std::string(kindOf(a.kind)) + ", .map = " +
                               spanOf(map, a.map.size()) + ", .otherwise = " + elseOf(a.otherwise) +
                               ", .elseValue = " + number(a.elseValue) + ", .elseText = " + cppString(a.elseText.value_or("")) +
                               ", .scale = " + number(a.scale) + ", .offset = " + number(a.offset) +
                               ", .hook = " + cppString(a.hook);
                    };
                    for (const Migrate& m : migrates)
                    {
                        const std::string at = std::to_string(m.step);
                        mapOf(id + "_map" + at, m.map);
                        const auto joined = s->joined.find(m.step);
                        if (joined == s->joined.end())
                        {
                            continue;
                        }
                        for (size_t j = 0; j < joined->second.size(); ++j)
                        {
                            mapOf(id + "_join" + at + "_map" + std::to_string(j), joined->second[j].answer.map);
                        }
                        out << "    constexpr ALCVarJoin " << id << "_join" << at << "[] = {\n";
                        for (size_t j = 0; j < joined->second.size(); ++j)
                        {
                            const Joined& other = joined->second[j];
                            out << "        { .decl = " << hex64(other.decl) << ", .type = " << enumType(other.type)
                                << ", .unit = " << enumUnit(other.unit) << ", "
                                << answerOf(other.answer, id + "_join" + at + "_map" + std::to_string(j)) << " },\n";
                        }
                        out << "    };\n";
                    }
                    if (!migrates.empty())
                    {
                        out << "    constexpr ALCVarMigrate " << id << "_migrates[] = {\n";
                        for (const Migrate& m : migrates)
                        {
                            const std::string at     = std::to_string(m.step);
                            const auto        joined = s->joined.find(m.step);
                            const auto        rename = renames.find(m.step);
                            out << "        { .step = " << m.step << ", " << answerOf(m, id + "_map" + at)
                                << ", .alias = " << (rename != renames.end() ? rename->second : -1) << ", .join = "
                                << spanOf(id + "_join" + at, joined != s->joined.end() ? joined->second.size() : 0)
                                << " },\n";
                        }
                        out << "    };\n";
                    }
                }
            }
            out << "}\n";

            for (const Group* g : groups)
            {
                std::array<std::vector<const Setting*>, ALCVAR_STORAGE_COUNT> byStorage;
                for (const Setting* s : g->settings)
                {
                    byStorage[size_t(ALCVarTypes::storageOf(s->type))].push_back(s);
                }
                out << "\nnamespace " << namespaceOf(options, g->name) << "\n{\n"
                    << "    constinit ALCVarGroupState Group_::state{};\n";

                auto values = [&](ALCVarStorage storage, const char* decl, auto&& element) {
                    const auto& list = byStorage[size_t(storage)];
                    if (list.empty())
                    {
                        return;
                    }
                    out << "    constinit " << decl << "[" << list.size() << "] = {\n";
                    for (const Setting* s : list)
                    {
                        out << "        " << element(*s) << ", // " << s->key << "\n";
                    }
                    out << "    };\n";
                };
                values(ALCVarStorage::Byte, "U8 Group_::bytes", [](const Setting& s) { return std::to_string(s.words[0]); });
                values(ALCVarStorage::Word, "U32 Group_::words", [](const Setting& s) {
                    char text[16];
                    std::snprintf(text, sizeof(text), "0x%08xu", unsigned(U32(s.words[0])));
                    return std::string(text);
                });
                values(ALCVarStorage::DWord, "U64 Group_::dwords", [](const Setting& s) { return hex64(s.words[0]); });
                values(ALCVarStorage::Pair, "ALCVarSeqSlot<2> Group_::pairs", [](const Setting& s) {
                    return "{ 0, { " + hex64(s.words[0]) + ", " + hex64(s.words[1]) + " } }";
                });
                values(ALCVarStorage::Triple, "ALCVarSeqSlot<3> Group_::triples", [](const Setting& s) {
                    return "{ 0, { " + hex64(s.words[0]) + ", " + hex64(s.words[1]) + ", " + hex64(s.words[2]) + " } }";
                });
                values(ALCVarStorage::Shared, "ALCVarSharedSlot Group_::shared", [](const Setting&) { return std::string("{}"); });
                values(ALCVarStorage::Boxed, "ALCVarBoxedSlot Group_::boxed", [](const Setting&) { return std::string("{}"); });

                if (!g->settings.empty())
                {
                    out << "    constinit const ALCVarDesc Group_::desc[count] = {\n";
                    for (const Setting* s : g->settings)
                    {
                        const std::string id = identifier(s->name);
                        out << "        {\n"
                            << "            .name        = " << cppString(s->name) << ",\n"
                            << "            .label       = " << cppString(s->label) << ",\n"
                            << "            .help        = " << cppString(s->help) << ",\n"
                            << "            .type        = " << enumType(s->type) << ",\n"
                            << "            .scope       = " << enumScope(s->scope) << ",\n"
                            << "            .unit        = " << enumUnit(s->unit) << ",\n"
                            << "            .backup      = " << enumBackup(s->backup) << ",\n"
                            << "            .preset      = " << enumPreset(s->preset) << ",\n"
                            << "            .refers      = " << enumRefers(s->refers) << ",\n"
                            << "            .origin      = " << enumOrigin(s->origin) << ",\n"
                            << "            .flags       = " << s->flags << ",\n"
                            << "            .key         = " << s->keyIndex << ",\n"
                            << "            .slot        = " << s->slot << ",\n"
                            << "            .def         = { .words = { " << hex64(s->words[0]) << ", "
                            << hex64(s->words[1]) << ", " << hex64(s->words[2]) << " }, .text = " << cppString(s->text)
                            << " },\n"
                            << "            .ranged      = " << (s->ranged ? "true" : "false") << ",\n"
                            << "            .uiRanged    = " << (s->uiRanged ? "true" : "false") << ",\n"
                            << "            .rangeMin    = " << number(s->rangeMin) << ",\n"
                            << "            .rangeMax    = " << number(s->rangeMax) << ",\n"
                            << "            .uiMin       = " << number(s->uiMin) << ",\n"
                            << "            .uiMax       = " << number(s->uiMax) << ",\n"
                            << "            .uiStep      = " << number(s->uiStep) << ",\n"
                            << "            .choices     = " << spanOf(id + "_choices", s->choices.size()) << ",\n"
                            << "            .aliases     = " << spanOf(id + "_aliases", s->aliases.size()) << ",\n"
                            << "            .tags        = " << spanOf(id + "_tags", s->tags.size()) << ",\n"
                            << "            .validate    = " << cppString(s->validate) << ",\n"
                            << "            .asset       = " << cppString(s->asset) << ",\n"
                            << "            .steps       = " << id << "_steps,\n"
                            << "            .migrates    = " << spanOf(id + "_migrates", s->migrates.size() + renamesIn(*s))
                            << ",\n"
                            << "            .superseded  = " << spanOf(id + "_superseded", s->superseded.size()) << ",\n"
                            << "            .from        = " << spanOf(id + "_from", s->sources.size()) << ",\n"
                            << "            .file        = " << cppString(s->file) << ",\n"
                            << "            .line        = " << s->line.line << ",\n"
                            << "        },\n";
                    }
                    out << "    };\n";
                }

                auto storageSpan = [&](ALCVarStorage storage, const char* array) {
                    return byStorage[size_t(storage)].empty() ? std::string("{}") : std::string("Group_::") + array;
                };
                out << "    constinit const ALCVarGroupDesc Group_::info = {\n"
                    << "        .name     = " << cppString(g->name) << ",\n"
                    << "        .label    = " << cppString(g->label) << ",\n"
                    << "        .help     = " << cppString(g->help) << ",\n"
                    << "        .parent   = " << cppString(g->parentName) << ",\n"
                    << "        .header   = " << cppString(headerOf(options, g->name)) << ",\n"
                    << "        .backup   = " << enumBackup(inherited(g, &Group::backup).value_or(ALCVarBackup::Yes)) << ",\n"
                    << "        .preset   = " << enumPreset(inherited(g, &Group::preset).value_or(ALCVarPreset::None)) << ",\n"
                    << "        .state    = &Group_::state,\n"
                    << "        .settings = " << (g->settings.empty() ? "{}" : "Group_::desc") << ",\n"
                    << "        .bytes    = " << storageSpan(ALCVarStorage::Byte, "bytes") << ",\n"
                    << "        .words    = " << storageSpan(ALCVarStorage::Word, "words") << ",\n"
                    << "        .dwords   = " << storageSpan(ALCVarStorage::DWord, "dwords") << ",\n"
                    << "        .pairs    = " << storageSpan(ALCVarStorage::Pair, "pairs") << ",\n"
                    << "        .triples  = " << storageSpan(ALCVarStorage::Triple, "triples") << ",\n"
                    << "        .shared   = " << storageSpan(ALCVarStorage::Shared, "shared") << ",\n"
                    << "        .boxed    = " << storageSpan(ALCVarStorage::Boxed, "boxed") << ",\n"
                    << "        .file     = " << cppString(g->file) << ",\n"
                    << "        .line     = " << g->line.line << ",\n"
                    << "    };\n}\n";
            }
            return out.str();
        }

        std::string registrySource(const Options& options, const Schema& schema, const std::vector<std::string>& tags)
        {
            std::ostringstream out;
            out << "// The schema's groups, for ALCVars to take at startup.\n"
                << GENERATED_NOTE << "\n#include \"linden_common.h\"\n\n"
                << "#include \"" << options.includeDir << "/schema.h\"\n";
            for (const auto& g : schema.groups)
            {
                out << "#include \"" << headerOf(options, g->name) << "\"\n";
            }
            out << "\nnamespace " << options.root << "\n{\n"
                << "    const ALCVarSchema& schema()\n    {\n";
            if (!schema.groups.empty())
            {
                out << "        static constexpr const ALCVarGroupDesc* groups[] = {\n";
                for (const auto& g : schema.groups)
                {
                    out << "            &" << namespaceOf(options, g->name) << "::Group_::info,\n";
                }
                out << "        };\n";
            }
            if (!tags.empty())
            {
                out << "        static constexpr std::string_view tags[] = { ";
                for (size_t i = 0; i < tags.size(); ++i)
                {
                    out << (i ? ", " : "") << cppString(tags[i]);
                }
                out << " };\n";
            }
            out << "        static constexpr ALCVarSchema value{ .root = " << cppString(options.root)
                << ", .groups = " << (schema.groups.empty() ? "{}" : "groups")
                << ", .tags = " << (tags.empty() ? "{}" : "tags") << " };\n"
                << "        return value;\n    }\n}\n";
            return out.str();
        }

        std::string schemaHeader(const Options& options, const std::vector<std::string>& tags)
        {
            std::ostringstream out;
            out << "// " << options.includeDir << "/schema.h: the whole schema, and its tags.\n"
                << GENERATED_NOTE << "\n#pragma once\n\n#include \"alcvar.h\"\n\n"
                << "namespace " << options.root << "\n{\n"
                << "    // Every group, for ALCVars::registerSchema.\n"
                << "    const ALCVarSchema& schema();\n";
            if (!tags.empty())
            {
                out << "\n    // The tags settings carry, each a generation that moves with any of them.\n"
                    << "    namespace tags\n    {\n";
                for (size_t i = 0; i < tags.size(); ++i)
                {
                    out << "        inline constexpr ALCVarTag<" << i << "> " << tags[i] << "{};\n";
                }
                out << "    }\n";
            }
            out << "}\n";
            return out.str();
        }

        std::string allHeader(const Options& options, const std::string& area, const std::vector<const Group*>& groups)
        {
            std::ostringstream out;
            out << "// " << options.includeDir << "/" << area << "/all.h: every group of the area " << area << ".\n"
                << GENERATED_NOTE << "\n#pragma once\n\n";
            for (const Group* g : groups)
            {
                out << "#include \"" << headerOf(options, g->name) << "\"\n";
            }
            return out.str();
        }

        std::string schemaJson(const Options& options, const Schema& schema)
        {
            std::ostringstream out;
            out << "{\n  \"root\": " << jsonString(options.root) << ",\n  \"groups\": [";
            for (size_t i = 0; i < schema.groups.size(); ++i)
            {
                const Group& g = *schema.groups[i];
                out << (i ? "," : "") << "\n    { \"name\": " << jsonString(g.name) << ", \"label\": " << jsonString(g.label)
                    << ", \"help\": " << jsonString(g.help) << ", \"header\": " << jsonString(headerOf(options, g.name))
                    << ", \"file\": " << jsonString(g.file) << ", \"line\": " << g.line.line << " }";
            }
            out << "\n  ],\n  \"settings\": [";
            bool first = true;
            for (const auto& g : schema.groups)
            {
                for (const Setting* s : g->settings)
                {
                    out << (first ? "" : ",") << "\n    {\n      \"name\": " << jsonString(s->name)
                        << ",\n      \"type\": " << jsonString(ALCVarTypes::nameOf(s->type))
                        << ",\n      \"default\": " << (s->json.empty() ? "null" : s->json)
                        << ",\n      \"unit\": " << jsonString(ALCVarUnits::nameOf(s->unit))
                        << ",\n      \"scope\": " << jsonString(scopeName(s->scope))
                        << ",\n      \"backup\": " << jsonString(backupName(s->backup))
                        << ",\n      \"preset\": " << jsonString(presetName(s->preset))
                        << ",\n      \"origin\": " << jsonString(originName(s->origin));
                    if (s->type == ALCVarType::UUID || s->type == ALCVarType::UUIDList)
                    {
                        out << ",\n      \"refers\": " << jsonString(refersName(s->refers));
                    }
                    if (s->ranged)
                    {
                        out << ",\n      \"range\": [" << number(s->rangeMin) << ", " << number(s->rangeMax) << "]";
                    }
                    if (s->uiRanged)
                    {
                        out << ",\n      \"ui_range\": [" << number(s->uiMin) << ", " << number(s->uiMax) << "]";
                    }
                    if (s->uiStep > 0)
                    {
                        out << ",\n      \"ui_step\": " << number(s->uiStep);
                    }
                    if (!s->choices.empty())
                    {
                        out << ",\n      \"choices\": [";
                        for (size_t c = 0; c < s->choices.size(); ++c)
                        {
                            const Choice& choice = s->choices[c];
                            out << (c ? ", " : "") << "{ \"value\": "
                                << (ALCVarTypes::isInteger(s->type) ? std::to_string(choice.number) : jsonString(choice.text))
                                << ", \"label\": " << jsonString(choice.label) << " }";
                        }
                        out << "]";
                    }
                    if (s->flags)
                    {
                        std::vector<std::string> flags;
                        for (const auto& [text, flag] : FLAGS)
                        {
                            if (s->flags & flag)
                            {
                                flags.push_back(jsonString(text));
                            }
                        }
                        out << ",\n      \"flags\": [" << join(flags, ", ") << "]";
                    }
                    if (!s->tags.empty())
                    {
                        std::vector<std::string> tags;
                        for (const auto& tag : s->tags)
                        {
                            tags.push_back(jsonString(tag));
                        }
                        out << ",\n      \"tags\": [" << join(tags, ", ") << "]";
                    }
                    if (!s->aliases.empty())
                    {
                        out << ",\n      \"aliases\": [";
                        for (size_t a = 0; a < s->aliases.size(); ++a)
                        {
                            const Alias& alias = s->aliases[a];
                            out << (a ? ", " : "") << "{ \"name\": " << jsonString(alias.name);
                            if (alias.inverted)
                            {
                                out << ", \"inverted\": true";
                            }
                            if (alias.stringEncoded)
                            {
                                out << ", \"encoding\": \"string\"";
                            }
                            if (!alias.joined.empty())
                            {
                                out << ", \"joined\": " << jsonString(alias.joined);
                            }
                            if (alias.unit != ALCVarUnit::None)
                            {
                                out << ", \"unit\": " << jsonString(ALCVarUnits::nameOf(alias.unit));
                            }
                            out << " }";
                        }
                        out << "]";
                    }
                    if (!s->validate.empty())
                    {
                        out << ",\n      \"validate\": " << jsonString(s->validate);
                    }
                    if (!s->asset.empty())
                    {
                        out << ",\n      \"asset\": " << jsonString(s->asset);
                    }
                    out << ",\n      \"label\": " << jsonString(s->label) << ",\n      \"help\": " << jsonString(s->help)
                        << ",\n      \"step\": " << s->steps.back().number << ",\n      \"decl\": \""
                        << hex16(s->steps.back().decl) << "\""
                        << ",\n      \"header\": " << jsonString(headerOf(options, g->name))
                        << ",\n      \"file\": " << jsonString(s->file) << ",\n      \"line\": " << s->line.line << "\n    }";
                    first = false;
                }
            }
            out << "\n  ]\n}\n";
            return out.str();
        }

        // Where the value of a setting lives, as a debugger's expression.
        std::string slotExpression(const Options& options, const Setting& s, bool natvis)
        {
            const std::string group = namespaceOf(options, s.group->name) + "::Group_::";
            const std::string amp   = natvis ? "&amp;" : "&";
            const std::string slot  = std::to_string(s.slot);
            switch (ALCVarTypes::storageOf(s.type))
            {
                case ALCVarStorage::Byte:
                    return "(bool)" + group + "bytes[" + slot + "]";
                case ALCVarStorage::Word:
                    if (s.type == ALCVarType::F32)
                    {
                        return "*(float*)" + amp + group + "words[" + slot + "]";
                    }
                    return std::string(s.type == ALCVarType::S32 ? "(int)" : "") + group + "words[" + slot + "]";
                case ALCVarStorage::DWord:
                    if (s.type == ALCVarType::F64)
                    {
                        return "*(double*)" + amp + group + "dwords[" + slot + "]";
                    }
                    if (s.type == ALCVarType::Vec2)
                    {
                        return "*(LLVector2*)" + amp + group + "dwords[" + slot + "]";
                    }
                    return std::string(natvis ? "(__int64)" : "(long long)") + group + "dwords[" + slot + "]";
                case ALCVarStorage::Pair:
                    return "*(" + cppType(s.type) + "*)" + amp + group + "pairs[" + slot + "].words[0]";
                case ALCVarStorage::Triple:
                    return "*(LLVector3d*)" + amp + group + "triples[" + slot + "].words[0]";
                case ALCVarStorage::Shared:
                    if (s.type == ALCVarType::String)
                    {
                        return natvis ? "*(std::string*)" + group + "shared[" + slot + "].current"
                                      : "*(std::string*)" + group + "shared[" + slot + "].current";
                    }
                    return "*(" + (natvis ? xmlText(cppType(s.type)) : cppType(s.type)) + "*)" + group + "shared[" + slot + "].current";
                case ALCVarStorage::Boxed:
                    return "*" + group + "boxed[" + slot + "].value";
            }
            return "";
        }

        std::string natvis(const Options& options, const Schema& schema)
        {
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                << "<!-- Written by alcvargen from the settings schema: each handle shown as its name and value. -->\n"
                << "<AutoVisualizer xmlns=\"http://schemas.microsoft.com/vstudio/debugger/natvis/2010\">\n";
            for (const auto& g : schema.groups)
            {
                const std::string group = namespaceOf(options, g->name) + "::Group_";
                out << "  <Type Name=\"ALCVarGroup&lt;" << group << "&gt;\">\n"
                    << "    <DisplayString>" << xmlText(g->name) << " (" << g->settings.size() << " settings)</DisplayString>\n"
                    << "  </Type>\n";
                for (const Setting* s : g->settings)
                {
                    const ALCVarStorage storage = ALCVarTypes::storageOf(s->type);
                    out << "  <Type Name=\"ALCVar&lt;*," << group << "," << s->keyIndex << "&gt;\">\n";
                    if (storage == ALCVarStorage::Shared)
                    {
                        out << "    <DisplayString Condition=\"" << group << "::shared[" << s->slot
                            << "].current == 0\">" << xmlText(s->name) << " = (default)</DisplayString>\n";
                    }
                    out << "    <DisplayString>" << xmlText(s->name) << " = {" << slotExpression(options, *s, true)
                        << "}</DisplayString>\n"
                        << "    <Expand><Item Name=\"value\">" << slotExpression(options, *s, true) << "</Item></Expand>\n"
                        << "  </Type>\n";
                }
            }
            out << "</AutoVisualizer>\n";
            return out.str();
        }

        std::string lldbFormatter(const Options& options, const Schema& schema)
        {
            std::ostringstream out;
            out << "# Written by alcvargen from the settings schema: each handle shown as its name and value.\n"
                << "# Load it into LLDB with: command script import <this file>\n"
                << "import re\n\nimport lldb\n\n"
                << "# The group's storage and the key, as a handle's type names them, to the setting's name and\n"
                << "# the expression its value is read through.\n"
                << "SETTINGS = {\n";
            for (const auto& g : schema.groups)
            {
                for (const Setting* s : g->settings)
                {
                    out << "    (" << jsonString(namespaceOf(options, g->name) + "::Group_") << ", " << s->keyIndex
                        << "): (" << jsonString(s->name) << ", " << jsonString(slotExpression(options, *s, false)) << "),\n";
                }
            }
            out << "}\n\n"
                << "HANDLE = re.compile(r\"^(?:const )?ALCVar<.*,\\s*([A-Za-z0-9_:]+::Group_),\\s*(\\d+)>$\")\n\n\n"
                << "def summary(valobj, internal_dict):\n"
                << "    match = HANDLE.match(valobj.GetType().GetUnqualifiedType().GetName())\n"
                << "    if not match:\n"
                << "        return None\n"
                << "    entry = SETTINGS.get((match.group(1), int(match.group(2))))\n"
                << "    if not entry:\n"
                << "        return None\n"
                << "    name, expression = entry\n"
                << "    value = valobj.GetTarget().EvaluateExpression(expression)\n"
                << "    shown = value.GetSummary() or value.GetValue() if value.IsValid() else None\n"
                << "    return \"%s = %s\" % (name, shown if shown is not None else \"(default)\")\n\n\n"
                << "def __lldb_init_module(debugger, internal_dict):\n"
                << "    debugger.HandleCommand('type summary add -x \"^(const )?ALCVar<.+>$\" -F ' + __name__ + '.summary')\n";
            return out.str();
        }

        void where(const Options& options, const Schema& schema, Result& result)
        {
            std::string target = options.where;
            for (const auto& setting : schema.settings)
            {
                for (const auto& alias : setting->aliases)
                {
                    if (alias.name == target)
                    {
                        result.where += alias.name + " is an old name of " + setting->name + "\n";
                        target = setting->name;
                    }
                }
            }
            if (const auto s = schema.settingByName.find(target); s != schema.settingByName.end())
            {
                result.where += s->second->file + ":" + std::to_string(s->second->line.line) + ": " + target + "\n" +
                                "  header: " + headerOf(options, s->second->group->name) + "\n" +
                                "  handle: " + namespaceOf(options, s->second->group->name) + "::" + s->second->key + "\n";
                result.ok = true;
                return;
            }
            if (const auto g = schema.groupByName.find(target); g != schema.groupByName.end())
            {
                result.where += g->second->file + ":" + std::to_string(g->second->line.line) + ": " + target + "\n" +
                                "  header: " + headerOf(options, target) + "\n";
                result.ok = true;
                return;
            }
            result.where += target + " is not declared\n";
        }

        // --where over a written cvars.json, for a checkout whose build has
        // run, without the declarations.
        void whereInJson(const Options& options, const std::string& path, Result& result)
        {
            std::ifstream     in(fs::path(path), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            Json        root;
            std::string error;
            if (!parseJson(text.str(), root, error))
            {
                result.errors.push_back(said(Message{ path, {}, "", error }, "error", options.msvc));
                return;
            }
            std::string target = options.where;
            for (const char* list : { "settings", "groups" })
            {
                const Json* entries = root.find(list);
                for (const Json& entry : entries ? entries->items : std::vector<Json>())
                {
                    bool aliased = false;
                    if (const Json* aliases = entry.find("aliases"))
                    {
                        for (const Json& alias : aliases->items)
                        {
                            aliased = aliased || alias.string("name") == target;
                        }
                    }
                    if (entry.string("name") != target && !aliased)
                    {
                        continue;
                    }
                    if (aliased)
                    {
                        result.where += target + " is an old name of " + std::string(entry.string("name")) + "\n";
                    }
                    result.where += std::string(entry.string("file")) + ":" +
                                    std::to_string(static_cast<long long>(entry.numberOr("line", 0))) + ": " +
                                    std::string(entry.string("name")) + "\n  header: " + std::string(entry.string("header")) +
                                    "\n";
                    result.ok = true;
                    return;
                }
            }
            result.where += target + " is not declared\n";
        }
    }

    namespace
    {
        // A JSON value as text, two spaces a level, an object's members in
        // the order they were given.
        std::string written(const Json& json, size_t level)
        {
            const std::string inner(2 * (level + 1), ' ');
            const std::string outer(2 * level, ' ');
            switch (json.kind)
            {
                case Json::Kind::Null:   return "null";
                case Json::Kind::Bool:   return json.boolean ? "true" : "false";
                case Json::Kind::Number:
                    return json.number == std::floor(json.number) ? std::to_string(static_cast<long long>(json.number))
                                                                  : number(json.number);
                case Json::Kind::String: return jsonString(json.text);
                case Json::Kind::Array:
                {
                    // A list of strings or numbers on one line.
                    const bool flat = std::all_of(json.items.begin(), json.items.end(), [](const Json& item) {
                        return item.kind != Json::Kind::Array && item.kind != Json::Kind::Object;
                    });
                    std::string out = "[";
                    for (size_t i = 0; i < json.items.size(); ++i)
                    {
                        out += (i ? "," : "") + (flat ? std::string(i ? " " : "") : "\n" + inner) + written(json.items[i], level + 1);
                    }
                    return out + (flat || json.items.empty() ? "]" : "\n" + outer + "]");
                }
                case Json::Kind::Object:
                {
                    std::string out = "{";
                    for (size_t i = 0; i < json.members.size(); ++i)
                    {
                        out += (i ? ",\n" : "\n") + inner + jsonString(json.members[i].name) + ": " +
                               written(json.members[i].value, level + 1);
                    }
                    return out + (json.members.empty() ? "}" : "\n" + outer + "}");
                }
            }
            return "null";
        }

        Json text(std::string_view value)
        {
            Json json;
            json.kind = Json::Kind::String;
            json.text = std::string(value);
            return json;
        }

        Json numeral(double value)
        {
            Json json;
            json.kind   = Json::Kind::Number;
            json.number = value;
            return json;
        }

        Json truth(bool value)
        {
            Json json;
            json.kind    = Json::Kind::Bool;
            json.boolean = value;
            return json;
        }

        Json yes() { return truth(true); }
        Json no() { return truth(false); }

        Json list(std::vector<Json> items)
        {
            Json json;
            json.kind  = Json::Kind::Array;
            json.items = std::move(items);
            return json;
        }

        Json object(std::vector<Json::Member> members)
        {
            Json json;
            json.kind    = Json::Kind::Object;
            json.members = std::move(members);
            return json;
        }

        template <typename Names>
        Json names(const Names& values)
        {
            std::vector<Json> items;
            for (const auto& value : values)
            {
                items.push_back(text(value));
            }
            return list(std::move(items));
        }

        template <typename E, size_t N>
        Json names(const std::array<std::pair<std::string_view, E>, N>& table)
        {
            std::vector<Json> items;
            for (const auto& [spelling, value] : table)
            {
                items.push_back(text(spelling));
            }
            return list(std::move(items));
        }

        Json ref(std::string_view definition) { return object({ { "$ref", text("#/definitions/" + std::string(definition)) } }); }

        // The schema of the declarations, from the generator's own tables: an
        // area of the schema's a table of its own, a table under it a setting
        // where it has a type and a group otherwise.
        std::string jsonSchemaText()
        {
            std::vector<std::string> types;
            for (U32 i = 0; i < ALCVAR_TYPE_COUNT; ++i)
            {
                types.emplace_back(ALCVarTypes::nameOf(ALCVarType(i)));
            }
            std::vector<std::string> units;
            for (U32 i = 1; i < U32(ALCVarUnit::Count); ++i)
            {
                units.emplace_back(ALCVarUnits::nameOf(ALCVarUnit(i)));
            }
            auto described = [](std::string_view description, std::vector<Json::Member> members) {
                members.insert(members.begin(), Json::Member{ "description", text(description) });
                return object(std::move(members));
            };
            auto field = [&](std::string_view name, std::vector<Json::Member> members) {
                return Json::Member{ std::string(name), described(SETTING_FIELDS.find(name)->second, std::move(members)) };
            };
            auto groupField = [&](std::string_view name, std::vector<Json::Member> members) {
                return Json::Member{ std::string(name), described(GROUP_FIELDS.find(name)->second, std::move(members)) };
            };
            const Json pair = object({ { "type", text("array") },
                                       { "items", object({ { "type", text("number") } }) },
                                       { "minItems", numeral(2) },
                                       { "maxItems", numeral(2) } });
            const Json value      = object({ { "type", names(std::vector<std::string>{ "integer", "string" }) } });
            const Json mapValue   = object({ { "type", names(std::vector<std::string>{ "number", "string" }) } });
            const Json elseValue  = object({ { "type", names(std::vector<std::string>{ "number", "string" }) },
                                             { "description", text("\"keep\": the value as it was; \"default\": the "
                                                                   "setting's default; or a value of its own") } });
            std::vector<Json::Member> answer = {
                { "map", object({ { "description", text("A value of the step before to what it becomes: numbers or strings, "
                                                        "as each step holds them") },
                                  { "type", text("object") },
                                  { "additionalProperties", mapValue } }) },
                { "else", elseValue },
                { "scale", object({ { "description", text("value * scale + offset") }, { "type", text("number") } }) },
                { "offset", object({ { "description", text("value * scale + offset") }, { "type", text("number") } }) },
                { "reset", object({ { "description", text("Stored values take the default") }, { "const", yes() } }) },
                { "fn", object({ { "description", text("A registered migration's name") }, { "type", text("string") } }) },
            };
            std::vector<Json::Member> migrate = answer;
            migrate.insert(migrate.begin(),
                           Json::Member{ "step", object({ { "description", text("The step of the lock it begins: 2 or later") },
                                              { "type", text("integer") },
                                              { "minimum", numeral(2) } }) });
            migrate.push_back({ "join", object({ { "description", text("Ends a fork: each of the other branch's steps, by "
                                                                       "its fingerprint, answered") },
                                                 { "type", text("object") },
                                                 { "propertyNames", object({ { "pattern", text("^[0-9a-f]{16}$") } }) },
                                                 { "additionalProperties", ref("answer") } }) });

            const Json definitions = object({
                { "entry", object({ { "anyOf", list({ ref("setting"), ref("group") }) } }) },
                { "group",
                  object({ { "type", text("object") },
                           { "properties",
                             object({ groupField("label", { { "type", text("string") } }),
                                      groupField("help", { { "type", text("string") } }),
                                      groupField("scope", { { "enum", names(SCOPES) } }),
                                      groupField("backup", { { "enum", names(BACKUPS) } }),
                                      groupField("preset", { { "enum", names(PRESETS) } }),
                                      groupField("origin", { { "enum", names(ORIGINS) } }) }) },
                           { "additionalProperties", ref("entry") } }) },
                { "setting",
                  object({ { "type", text("object") },
                           { "required", names(std::vector<std::string>{ "type", "default", "help" }) },
                           { "properties",
                             object({ field("type", { { "enum", names(types) } }),
                                      field("default", {}),
                                      field("help", { { "type", text("string") }, { "minLength", numeral(1) } }),
                                      field("label", { { "type", text("string") } }),
                                      field("scope", { { "enum", names(SCOPES) } }),
                                      field("range", pair.members),
                                      field("ui_range", pair.members),
                                      field("ui_step", { { "type", text("number") }, { "exclusiveMinimum", numeral(0) } }),
                                      field("unit", { { "enum", names(units) } }),
                                      field("choices",
                                            { { "anyOf", list({ object({ { "type", text("object") },
                                                                         { "additionalProperties", object({ { "type", text("string") } }) } }),
                                                                object({ { "type", text("array") }, { "items", ref("choice") } }) }) } }),
                                      field("flags", { { "type", text("array") },
                                                       { "items", object({ { "enum", names(FLAGS) } }) },
                                                       { "uniqueItems", yes() } }),
                                      field("tags", { { "type", text("array") },
                                                      { "items", object({ { "type", text("string") },
                                                                          { "pattern", text("^[a-z][a-z0-9_]*$") } }) } }),
                                      field("validate", { { "type", text("string") } }),
                                      field("alias", { { "anyOf", list({ object({ { "type", text("string") } }), ref("alias"),
                                                                         object({ { "type", text("array") },
                                                                                  { "items", object({ { "anyOf", list({ object({ { "type", text("string") } }),
                                                                                                                        ref("alias") }) } }) } }) }) } }),
                                      field("origin", { { "enum", names(ORIGINS) } }),
                                      field("migrate", { { "type", text("array") }, { "items", ref("migrate") } }),
                                      field("backup", { { "enum", names(BACKUPS) } }),
                                      field("preset", { { "enum", names(PRESETS) } }),
                                      field("asset", { { "type", text("string") } }),
                                      field("refers", { { "enum", names(REFERS) } }),
                                      field("from", { { "type", text("array") }, { "items", ref("source") } }),
                                      field("reuse", { { "type", text("boolean") } }) }) },
                           { "additionalProperties", no() } }) },
                { "choice", object({ { "type", text("object") },
                                     { "required", names(std::vector<std::string>{ "value" }) },
                                     { "properties", object({ { "value", value },
                                                              { "label", object({ { "type", text("string") } }) },
                                                              { "was", object({ { "description", text("The values that became it") },
                                                                                { "type", text("array") },
                                                                                { "items", value } }) } }) },
                                     { "additionalProperties", no() } }) },
                { "alias", object({ { "type", text("object") },
                                    { "required", names(std::vector<std::string>{ "name" }) },
                                    { "properties",
                                      object({ { "name", object({ { "description", text("The old name") }, { "type", text("string") } }) },
                                               { "inverted", object({ { "description", text("A boolean that said the opposite") },
                                                                      { "type", text("boolean") } }) },
                                               { "encoding", object({ { "description", text("A UUID or a list kept in a string") },
                                                                      { "enum", names(std::vector<std::string>{ "string" }) } }) },
                                               { "joined", object({ { "description", text("A list kept in a string, its elements joined by this") },
                                                                    { "type", text("string") } }) },
                                               { "unit", object({ { "description", text("The unit the old name's values were in") },
                                                                  { "enum", names(units) } }) } }) },
                                    { "additionalProperties", no() } }) },
                { "answer", object({ { "type", text("object") }, { "properties", object(answer) }, { "additionalProperties", no() } }) },
                { "migrate", object({ { "type", text("object") },
                                      { "required", names(std::vector<std::string>{ "step" }) },
                                      { "properties", object(migrate) },
                                      { "additionalProperties", no() } }) },
                { "source", object({ { "type", text("object") },
                                     { "required", names(std::vector<std::string>{ "name", "fn" }) },
                                     { "properties", object({ { "name", object({ { "description", text("A setting the lock holds") },
                                                                                 { "type", text("string") } }) },
                                                              { "fn", object({ { "description", text("The registered migration that gives "
                                                                                                     "the value") },
                                                                               { "type", text("string") } }) } }) },
                                     { "additionalProperties", no() } }) },
            });
            Json root = object({
                { "$schema", text("http://json-schema.org/draft-07/schema#") },
                { "title", text("Alchemy settings schema") },
                { "description", text("A file of indra/cvars: an area's groups and settings, each [area.group.key] a setting "
                                      "where it has a type and a group otherwise. Written by alcvargen --json-schema; "
                                      "doc/CVARS.md says how to use them.") },
                { "type", text("object") },
                { "propertyNames", object({ { "enum", names(AREAS) } }) },
                { "additionalProperties", ref("group") },
                { "definitions", definitions },
            });
            return written(root, 0) + "\n";
        }

        void writeJsonSchema(const Options& options, Result& result)
        {
            if (!writeText(options.jsonSchema, jsonSchemaText()))
            {
                result.errors.push_back(said(Message{ options.jsonSchema, {}, "", "cannot be written" }, "error", options.msvc));
            }
            result.ok = result.errors.empty();
        }
    }

    Result run(const Options& options)
    {
        Result      result;
        Schema      schema;
        Diagnostics diag;

        if (!options.where.empty() && options.inputs.size() == 1 && options.inputs[0].ends_with(".json"))
        {
            whereInJson(options, options.inputs[0], result);
            return result;
        }

        if (!options.jsonSchema.empty())
        {
            writeJsonSchema(options, result);
            return result;
        }

        if (!options.words.empty())
        {
            std::ifstream in(fs::path(options.words));
            if (!in)
            {
                diag.error(fs::path(options.words).generic_string(), 0, "", "cannot be read");
            }
            std::string word;
            while (in >> word)
            {
                if (!word.starts_with("#"))
                {
                    schema.words.insert(word);
                }
            }
        }

        // The lock, read before the declarations so that its problems come
        // first. Without one, each setting is the step a first lock would
        // record, and nothing is said of it.
        const bool  haveLock = !options.lock.empty() && options.where.empty();
        Lock        lock;
        Diagnostics lockDiag;
        if (options.writeLock && !haveLock)
        {
            diag.error("", 0, "", "--write-lock records the schema in the lock --lock names");
        }
        if (haveLock)
        {
            readLock(options.lock, lock, lockDiag);
        }
        // Recording, the lines the commit does not hold yet are worked out
        // again from the entries, so that the steps a setting gained since
        // are one: a change made twice before a commit is one step.
        if (options.writeLock && haveLock && !options.committed.empty() && fs::exists(fs::path(options.committed)))
        {
            Lock        committed;
            Diagnostics unused;
            readLock(options.committed, committed, unused);
            lock = heldIn(lock, committed);
        }

        Loader loader(options, schema, diag);
        for (const auto& input : options.inputs)
        {
            loader.load(input);
        }
        inherit(schema, diag);
        checkUnitSuffixes(schema, diag);
        checkAliases(schema, diag);

        // Keys and slots in declaration order, each group's settings by line.
        for (auto& g : schema.groups)
        {
            std::stable_sort(g->settings.begin(), g->settings.end(),
                             [](const Setting* a, const Setting* b) { return a->line < b->line; });
            std::array<U16, ALCVAR_STORAGE_COUNT> next{};
            for (size_t i = 0; i < g->settings.size(); ++i)
            {
                Setting* s  = g->settings[i];
                s->keyIndex = U16(i);
                s->slot     = next[size_t(ALCVarTypes::storageOf(s->type))]++;
            }
        }

        if (!options.where.empty())
        {
            where(options, schema, result);
            result.errors = said(diag.errors, "error", options.msvc);
            return result;
        }

        std::vector<Message> notes;
        History              history(schema, lock, haveLock, lockDiag, notes);
        history.run(options.writeLock);
        if (!options.base.empty())
        {
            Lock base;
            readLock(options.base, base, lockDiag);
            history.checkBase(base);
        }
        std::vector<Step> added = history.added;
        for (const auto& setting : schema.settings)
        {
            foldWas(*setting);
        }

        // A local build records a new setting's first step, which changes
        // nothing stored, and goes on; every other line waits for the lock's
        // target, and a strict build records none.
        if (haveLock && options.recordNew && !options.writeLock && diag.errors.empty() && lockDiag.errors.empty())
        {
            std::vector<Step> firsts;
            std::vector<Step> rest;
            for (const Step& step : added)
            {
                const bool alone = std::count_if(added.begin(), added.end(),
                                                 [&](const Step& other) { return other.name == step.name; }) == 1;
                (alone && step.kind == Step::Kind::Step && step.parent.empty() ? firsts : rest).push_back(step);
            }
            if (!firsts.empty() && writeText(options.lock, lockText(lock, firsts)))
            {
                for (Message& note : notes)
                {
                    const bool first = std::any_of(firsts.begin(), firsts.end(),
                                                   [&](const Step& step) { return step.name == note.name; });
                    if (first && note.text.starts_with("declared (step "))
                    {
                        note.text += "; recorded in the lock";
                    }
                }
                added = std::move(rest);
            }
            else if (!firsts.empty())
            {
                lockDiag.error(options.lock, 0, "", "cannot be written");
            }
        }
        if (haveLock && !added.empty() && !options.writeLock)
        {
            lockDiag.error(options.lock, 1, "",
                           "behind the schema; run " + options.lockCommand + ", and commit it with the change");
        }
        // A configure goes on, so that the tree it writes can build the
        // lock's target, and the build fails in its place.
        std::vector<Message> warnings;
        auto&                lockErrors = options.provisional ? warnings : diag.errors;
        lockErrors.insert(lockErrors.end(), lockDiag.errors.begin(), lockDiag.errors.end());

        result.errors   = said(diag.errors, "error", options.msvc);
        result.warnings = said(warnings, "warning", options.msvc);
        result.notes    = said(notes, "note", options.msvc);
        if (options.writeLock)
        {
            if (result.errors.empty() && !writeText(options.lock, lockText(lock, added)))
            {
                result.errors.push_back(said(Message{ options.lock, {}, "", "cannot be written" }, "error", options.msvc));
            }
            result.ok = result.errors.empty();
            return result;
        }
        if (!result.errors.empty() || options.check)
        {
            result.ok = result.errors.empty();
            return result;
        }

        std::set<std::string> tagSet;
        for (const auto& s : schema.settings)
        {
            tagSet.insert(s->tags.begin(), s->tags.end());
        }
        const std::vector<std::string> tags(tagSet.begin(), tagSet.end());

        // The previous run's outputs, so that what it wrote and this one
        // does not -- a group deleted -- does not stay behind.
        std::set<std::string> previous;
        {
            std::ifstream in(fs::path(options.outDir) / "cvars.outputs");
            std::string   row;
            while (std::getline(in, row))
            {
                if (const size_t space = row.find(' '); space != std::string::npos)
                {
                    previous.insert(row.substr(space + 1));
                }
            }
        }

        Writer writer(options, result);
        std::map<std::string, std::vector<const Group*>> areas;
        std::vector<std::string>                         areaOrder;
        for (const auto& g : schema.groups)
        {
            const std::string area = split(g->name, '.')[0];
            if (!areas.contains(area))
            {
                areaOrder.push_back(area);
            }
            areas[area].push_back(g.get());
        }

        std::string manifest;
        for (const auto& g : schema.groups)
        {
            const std::string header = "include/" + headerOf(options, g->name);
            writer.write(header, groupHeader(options, *g));
            manifest += "header " + header + "\n";
        }
        for (const auto& area : areaOrder)
        {
            const std::string all = "include/" + options.includeDir + "/" + area + "/all.h";
            writer.write(all, allHeader(options, area, areas[area]));
            manifest += "header " + all + "\n";
            const std::string source = "src/" + options.includeDir + "_" + area + ".cpp";
            writer.write(source, areaSource(options, area, areas[area]));
            manifest += "source " + source + "\n";
        }
        const std::string schemaH = "include/" + options.includeDir + "/schema.h";
        writer.write(schemaH, schemaHeader(options, tags));
        manifest += "header " + schemaH + "\n";
        const std::string registry = "src/" + options.includeDir + "_registry.cpp";
        writer.write(registry, registrySource(options, schema, tags));
        manifest += "source " + registry + "\n";
        writer.write("cvars.json", schemaJson(options, schema));
        manifest += "schema cvars.json\n";
        writer.write("alcvar.natvis", natvis(options, schema));
        manifest += "natvis alcvar.natvis\n";
        writer.write("alcvar_lldb.py", lldbFormatter(options, schema));
        manifest += "lldb alcvar_lldb.py\n";
        writer.write("cvars.outputs", manifest);

        for (const auto& stale : previous)
        {
            if (std::find(result.outputs.begin(), result.outputs.end(), stale) == result.outputs.end())
            {
                std::error_code ec;
                fs::remove(fs::path(options.outDir) / fs::path(stale), ec);
            }
        }
        result.ok = result.errors.empty();
        return result;
    }

    namespace
    {
        const char* const HELP =
            "usage: alcvargen --out <dir> [options] <file.toml>...\n"
            "       alcvargen --write-lock --lock <cvars.lock> [--committed <file>] <file.toml>...\n"
            "       alcvargen --check [--lock <cvars.lock>] [--base <cvars.lock>] <file.toml>...\n"
            "       alcvargen --where <name> <file.toml>... | <cvars.json>\n"
            "       alcvargen --json-schema <file>\n"
            "\n"
            "Reads the settings schema, one TOML file an area, checks every declaration and\n"
            "each setting's history in the lock, and writes the headers, descriptor tables\n"
            "and schema files alcvar compiles. How to add, change, rename, split or delete\n"
            "a setting, and what each message asks, is in doc/CVARS.md.\n"
            "\n"
            "  --out <dir>              where the outputs go: include/, src/ and cvars.json\n"
            "  --root <ns>              the namespace the handles are under (cvar)\n"
            "  --include-dir <dir>      the directory their headers are included from (cvars)\n"
            "  --lock <cvars.lock>      each setting's steps: what its stored values have meant\n"
            "  --lock-command <text>    the command that records the lock, as an error names it\n"
            "  --base <cvars.lock>      a target branch's lock: every line of it must be in --lock\n"
            "  --committed <file>       with --write-lock, the lock as HEAD holds it: the steps a\n"
            "                           setting gained since are recorded as one\n"
            "  --record-new             write a new setting's first step into the lock and go on,\n"
            "                           as a local build does; any other change still fails\n"
            "  --provisional            a lock behind the schema is a warning, as a configure's is\n"
            "  --write-lock             record the schema's changes in the lock, refusing one\n"
            "                           nothing answers, and write nothing else\n"
            "  --words <file>           accepted words, one a line: every word of every name\n"
            "                           is checked against them\n"
            "  --check                  check the declarations and the lock, and write nothing\n"
            "  --where <name>           say where a setting or group, or an old name, is declared\n"
            "  --diagnostics <gcc|msvc> messages as compilers write them, file:line:col: error:\n"
            "                           (gcc), or as Visual Studio lists them (msvc)\n"
            "  --json-schema <file>     write the JSON Schema an editor checks the TOML by\n"
            "  --help                   say this\n";
    }

    int main(int argc, char* argv[])
    {
        Options options;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            auto              next = [&]() -> std::string {
                if (i + 1 >= argc)
                {
                    std::cerr << "alcvargen: " << arg << " needs a value\n";
                    std::exit(2);
                }
                return argv[++i];
            };
            if (arg == "--out")
            {
                options.outDir = next();
            }
            else if (arg == "--root")
            {
                options.root = next();
            }
            else if (arg == "--include-dir")
            {
                options.includeDir = next();
            }
            else if (arg == "--lock")
            {
                options.lock = next();
            }
            else if (arg == "--base")
            {
                options.base = next();
            }
            else if (arg == "--lock-command")
            {
                options.lockCommand = next();
            }
            else if (arg == "--provisional")
            {
                options.provisional = true;
            }
            else if (arg == "--write-lock")
            {
                options.writeLock = true;
            }
            else if (arg == "--words")
            {
                options.words = next();
            }
            else if (arg == "--where")
            {
                options.where = next();
            }
            else if (arg == "--check")
            {
                options.check = true;
            }
            else if (arg == "--record-new")
            {
                options.recordNew = true;
            }
            else if (arg == "--committed")
            {
                options.committed = next();
            }
            else if (arg == "--json-schema")
            {
                options.jsonSchema = next();
            }
            else if (arg == "--diagnostics")
            {
                const std::string form = next();
                if (form != "gcc" && form != "msvc")
                {
                    std::cerr << "alcvargen: --diagnostics is gcc or msvc\n";
                    return 2;
                }
                options.msvc = form == "msvc";
            }
            else if (arg == "--help" || arg == "-h")
            {
                std::cout << HELP;
                return 0;
            }
            else if (arg.starts_with("--"))
            {
                std::cerr << "alcvargen: unknown option " << arg << "; alcvargen --help lists them\n";
                return 2;
            }
            else
            {
                options.inputs.push_back(arg);
            }
        }
        if (options.outDir.empty() && options.where.empty() && !options.check && !options.writeLock &&
            options.jsonSchema.empty())
        {
            std::cerr << HELP;
            return 2;
        }

        const Result result = run(options);
        for (const auto& note : result.notes)
        {
            std::cout << note << "\n";
        }
        for (const auto& error : result.errors)
        {
            std::cerr << error << "\n";
        }
        for (const auto& warning : result.warnings)
        {
            std::cerr << warning << "\n";
        }
        if (!options.where.empty())
        {
            std::cout << result.where;
        }
        return result.ok ? 0 : 1;
    }
}
