/**
 * @file alvimmappings.cpp
 * @brief Vim's key mappings: the :map family's table, vim's key notation, and which mapping keys typed make.
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

#include "alvimmappings.h"

#include "alsaid.h"
#include "llstring.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
    struct KeyName
    {
        KEY         key;
        const char* name;
    };
    // By their names in vim's notation, lowered; the first of a key's names
    // is the one it is shown by.
    const KeyName KEY_NAMES[] = {
        { KEY_ESCAPE, "Esc" },       { KEY_RETURN, "CR" },      { KEY_RETURN, "Enter" },     { KEY_RETURN, "Return" },
        { KEY_TAB, "Tab" },          { KEY_BACKSPACE, "BS" },   { KEY_BACKSPACE, "Backspace" }, { KEY_DELETE, "Del" },
        { KEY_DELETE, "Delete" },    { KEY_INSERT, "Insert" },  { KEY_INSERT, "Ins" },       { KEY_UP, "Up" },
        { KEY_DOWN, "Down" },        { KEY_LEFT, "Left" },      { KEY_RIGHT, "Right" },      { KEY_HOME, "Home" },
        { KEY_END, "End" },          { KEY_PAGE_UP, "PageUp" }, { KEY_PAGE_DOWN, "PageDown" }, { KEY_F1, "F1" },
        { KEY_F2, "F2" },            { KEY_F3, "F3" },          { KEY_F4, "F4" },            { KEY_F5, "F5" },
        { KEY_F6, "F6" },            { KEY_F7, "F7" },          { KEY_F8, "F8" },            { KEY_F9, "F9" },
        { KEY_F10, "F10" },          { KEY_F11, "F11" },        { KEY_F12, "F12" },
    };

    // A character, as the notation takes it to be one; else none.
    const char* charName(llwchar ch)
    {
        switch (ch)
        {
            case '<':  return "lt";
            case ' ':  return "Space";
            case '|':  return "Bar";
            case '\\': return "Bslash";
            default:   return nullptr;
        }
    }

    // The :map family: each command by the least of its name that vim
    // takes and its whole name, the modes it is for, and what it does.
    enum class Kind : U8
    {
        Map,
        Noremap,
        Unmap,
        Clear
    };
    struct Family
    {
        const char* least;
        const char* whole;
        U8          modes;
        Kind        kind;
    };
    constexpr U8 NVO = ALVimMappings::NORMAL | ALVimMappings::VISUAL | ALVimMappings::OPERATOR;
    constexpr U8 IC  = ALVimMappings::INSERT | ALVimMappings::COMMAND_LINE;
    const Family FAMILY[] = {
        { "map", "map", NVO, Kind::Map },
        { "nm", "nmap", ALVimMappings::NORMAL, Kind::Map },
        { "vm", "vmap", ALVimMappings::VISUAL, Kind::Map },
        { "xm", "xmap", ALVimMappings::VISUAL, Kind::Map },
        { "om", "omap", ALVimMappings::OPERATOR, Kind::Map },
        { "im", "imap", ALVimMappings::INSERT, Kind::Map },
        { "cm", "cmap", ALVimMappings::COMMAND_LINE, Kind::Map },
        { "smap", "smap", 0, Kind::Map },
        { "no", "noremap", NVO, Kind::Noremap },
        { "nn", "nnoremap", ALVimMappings::NORMAL, Kind::Noremap },
        { "vn", "vnoremap", ALVimMappings::VISUAL, Kind::Noremap },
        { "xn", "xnoremap", ALVimMappings::VISUAL, Kind::Noremap },
        { "ono", "onoremap", ALVimMappings::OPERATOR, Kind::Noremap },
        { "ino", "inoremap", ALVimMappings::INSERT, Kind::Noremap },
        { "cno", "cnoremap", ALVimMappings::COMMAND_LINE, Kind::Noremap },
        { "snor", "snoremap", 0, Kind::Noremap },
        { "unm", "unmap", NVO, Kind::Unmap },
        { "nun", "nunmap", ALVimMappings::NORMAL, Kind::Unmap },
        { "vu", "vunmap", ALVimMappings::VISUAL, Kind::Unmap },
        { "xu", "xunmap", ALVimMappings::VISUAL, Kind::Unmap },
        { "ou", "ounmap", ALVimMappings::OPERATOR, Kind::Unmap },
        { "iu", "iunmap", ALVimMappings::INSERT, Kind::Unmap },
        { "cu", "cunmap", ALVimMappings::COMMAND_LINE, Kind::Unmap },
        { "sunm", "sunmap", 0, Kind::Unmap },
        { "mapc", "mapclear", NVO, Kind::Clear },
        { "nmapc", "nmapclear", ALVimMappings::NORMAL, Kind::Clear },
        { "vmapc", "vmapclear", ALVimMappings::VISUAL, Kind::Clear },
        { "xmapc", "xmapclear", ALVimMappings::VISUAL, Kind::Clear },
        { "omapc", "omapclear", ALVimMappings::OPERATOR, Kind::Clear },
        { "imapc", "imapclear", ALVimMappings::INSERT, Kind::Clear },
        { "cmapc", "cmapclear", ALVimMappings::COMMAND_LINE, Kind::Clear },
        { "smapc", "smapclear", 0, Kind::Clear },
    };

    // `name` is `whole` cut no shorter than `least`.
    bool abbreviates(std::string_view name, std::string_view least, std::string_view whole)
    {
        return name.size() >= least.size() && name.size() <= whole.size() && whole.compare(0, name.size(), name) == 0;
    }

    std::string lowered(std::string text)
    {
        for (char& c : text)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    }

    bool isBlank(char c) { return c == ' ' || c == '\t'; }

    // What a mapping's keys are in, as :map lists them: blank for :map's
    // three, ! for :map!'s two, else a letter each.
    std::string modesShown(U8 modes)
    {
        if (modes == NVO)
        {
            return " ";
        }
        if (modes == IC)
        {
            return "!";
        }
        std::string out;
        const std::pair<U8, char> LETTERS[] = { { ALVimMappings::NORMAL, 'n' },
                                                { ALVimMappings::VISUAL, 'x' },
                                                { ALVimMappings::OPERATOR, 'o' },
                                                { ALVimMappings::INSERT, 'i' },
                                                { ALVimMappings::COMMAND_LINE, 'c' } };
        for (const auto& [bit, letter] : LETTERS)
        {
            if (modes & bit)
            {
                out += letter;
            }
        }
        return out;
    }

    bool startsWith(const std::vector<ALVimInput>& keys, const std::vector<ALVimInput>& prefix)
    {
        if (prefix.size() > keys.size())
        {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (!keys[i].sameAs(prefix[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool sameKeys(const std::vector<ALVimInput>& a, const std::vector<ALVimInput>& b) { return a.size() == b.size() && startsWith(a, b); }
}

// --- the notation ------------------------------------------------------------------

std::vector<ALVimInput> ALVimMappings::keysOf(std::string_view text) const
{
    std::vector<ALVimInput> out;
    size_t                  at = 0;
    while (at < text.size())
    {
        if (text[at] == '<')
        {
            const size_t close = text.find('>', at + 1);
            if (close != std::string_view::npos && close > at + 1 && named(std::string(text.substr(at + 1, close - at - 1)), out))
            {
                at = close + 1;
                continue;
            }
        }
        const LLCodepointAt cp = utf8str_decode_at(text, at);
        out.push_back(ALVimInput::character(cp.cp));
        at = llmax(cp.next, at + 1);
    }
    return out;
}

bool ALVimMappings::named(const std::string& name_in, std::vector<ALVimInput>& out) const
{
    const std::string name = lowered(name_in);
    if (name == "leader" || name == "localleader")
    {
        // As the leader is now, which a later :let mapleader leaves be:
        // spelt in the notation itself, but for another <Leader>.
        const std::string& leader = name == "leader" ? mLeader : mLocalLeader;
        if (lowered(leader).find("leader>") == std::string::npos)
        {
            const std::vector<ALVimInput> keys = keysOf(leader);
            out.insert(out.end(), keys.begin(), keys.end());
        }
        return true;
    }
    if (name == "nop")
    {
        return true;
    }
    return namedKey(name_in, out);
}

// static
bool ALVimMappings::namedKey(const std::string& name_in, std::vector<ALVimInput>& out)
{
    const std::string name = lowered(name_in);
    for (const llwchar ch : { U'<', U' ', U'|', U'\\' })
    {
        if (name == lowered(charName(ch)))
        {
            out.push_back(ALVimInput::character(ch));
            return true;
        }
    }
    // Modifiers: C- S- A- M-, before a key or a character.
    MASK   mask = MASK_NONE;
    size_t from = 0;
    while (name.size() > from + 2 && name[from + 1] == '-' && std::strchr("csam", name[from]))
    {
        mask |= name[from] == 'c' ? ALVimInput::CONTROL : name[from] == 's' ? MASK_SHIFT : MASK_ALT;
        from += 2;
    }
    const std::string rest = name.substr(from);
    if (mask != MASK_NONE && rest.size() == 1)
    {
        const char c = name_in[from];
        if (mask == MASK_SHIFT && std::isalpha(static_cast<unsigned char>(c)))
        {
            // <S-a> is the capital.
            out.push_back(ALVimInput::character(static_cast<llwchar>(std::toupper(static_cast<unsigned char>(c)))));
            return true;
        }
        if (mask == ALVimInput::CONTROL && c == '[')
        {
            // Control-[ is Escape, as a terminal has it.
            out.push_back(ALVimInput::keyOf(KEY_ESCAPE));
            return true;
        }
        out.push_back(ALVimInput::keyOf(static_cast<KEY>(std::toupper(static_cast<unsigned char>(c))), mask));
        return true;
    }
    if (rest == "space")
    {
        out.push_back(ALVimInput::keyOf(' ', mask));
        return true;
    }
    if (rest == "nl")
    {
        // Control-J, which is Return where Return is a line break.
        out.push_back(ALVimInput::keyOf('J', ALVimInput::CONTROL | mask));
        return true;
    }
    for (const KeyName& known : KEY_NAMES)
    {
        if (rest == lowered(known.name))
        {
            out.push_back(ALVimInput::keyOf(known.key, mask));
            return true;
        }
    }
    return false;
}

namespace
{
    // A key that is no character in the notation, its modifiers before its
    // name: a letter's key as a register writes it, lowered, or as :map
    // lists it. Nothing for a key with no name and no character.
    std::string keySpelt(const ALVimInput& in, bool lower_letters)
    {
        std::string name;
        for (const KeyName& known : KEY_NAMES)
        {
            if (known.key == in.key)
            {
                name = known.name;
                break;
            }
        }
        if (name.empty())
        {
            if (in.key == ' ')
            {
                name = "Space";
            }
            else if (in.key > 0x20 && in.key < 0x7F)
            {
                const char c = static_cast<char>(in.key);
                name         = std::string(1, lower_letters ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c);
            }
            else
            {
                return std::string();
            }
        }
        std::string mods;
        if (in.mask & ALVimInput::CONTROL)
        {
            mods += "C-";
        }
        if (in.mask & MASK_ALT)
        {
            mods += "A-";
        }
        if (in.mask & MASK_SHIFT)
        {
            mods += "S-";
        }
        return "<" + mods + name + ">";
    }
}

// static
std::string ALVimMappings::shown(const std::vector<ALVimInput>& keys)
{
    std::string out;
    for (const ALVimInput& in : keys)
    {
        if (in.isChar)
        {
            const char* name = charName(in.ch);
            out += name ? "<" + std::string(name) + ">" : utf8str_from_cp(in.ch);
            continue;
        }
        out += keySpelt(in, false);
    }
    return out;
}

// static
std::string ALVimMappings::written(const std::vector<ALVimInput>& keys)
{
    std::string out;
    for (const ALVimInput& in : keys)
    {
        if (in.isChar)
        {
            out += in.ch == '<' ? std::string("<lt>") : utf8str_from_cp(in.ch);
            continue;
        }
        out += keySpelt(in, true);
    }
    return out;
}

// static
std::vector<ALVimInput> ALVimMappings::keysWritten(std::string_view text)
{
    std::vector<ALVimInput> out;
    size_t                  at = 0;
    while (at < text.size())
    {
        if (text[at] == '<')
        {
            const size_t close = text.find('>', at + 1);
            if (close != std::string_view::npos && close > at + 1 && namedKey(std::string(text.substr(at + 1, close - at - 1)), out))
            {
                at = close + 1;
                continue;
            }
        }
        const LLCodepointAt cp = utf8str_decode_at(text, at);
        out.push_back(ALVimInput::character(cp.cp));
        at = llmax(cp.next, at + 1);
    }
    return out;
}

// --- the :map family ------------------------------------------------------------------

bool ALVimMappings::command(const std::string& name_in, const std::string& args_in, bool vimrc, std::string& listing, std::string& error)
{
    const bool  bang = !name_in.empty() && name_in.back() == '!';
    const std::string name = bang ? name_in.substr(0, name_in.size() - 1) : name_in;
    const Family* family   = nullptr;
    for (const Family& each : FAMILY)
    {
        if (abbreviates(name, each.least, each.whole))
        {
            family = &each;
            break;
        }
    }
    // The bang is :map!'s -- insert mode and the : line -- and only the
    // bare four take it.
    if (!family || (bang && family->modes != NVO))
    {
        return false;
    }
    const U8 modes = bang ? IC : family->modes;
    if (family->kind == Kind::Clear)
    {
        for (Mapping& each : mMappings)
        {
            each.modes &= static_cast<U8>(~modes);
        }
        std::erase_if(mMappings, [](const Mapping& each) { return each.modes == 0; });
        return true;
    }

    // What comes before the keys: <buffer> <nowait> <silent> <special>
    // <script> <unique>, and <expr>, which there is nothing here to
    // evaluate.
    std::string_view args = args_in;
    bool             nowait = false;
    bool             unique = false;
    for (;;)
    {
        while (!args.empty() && isBlank(args.front()))
        {
            args.remove_prefix(1);
        }
        const size_t close = args.empty() || args.front() != '<' ? std::string_view::npos : args.find('>');
        if (close == std::string_view::npos)
        {
            break;
        }
        const std::string word = lowered(std::string(args.substr(1, close - 1)));
        if (word == "expr")
        {
            error = alSaid("VimMapExpr", "E474: <expr> mappings are not supported");
            return true;
        }
        if (word != "buffer" && word != "nowait" && word != "silent" && word != "special" && word != "script" && word != "unique")
        {
            break;
        }
        nowait = nowait || word == "nowait";
        unique = unique || word == "unique";
        args.remove_prefix(close + 1);
    }
    // The keys, to the first blank; then what they stand for, to a bar
    // that is not \| -- which is a bar -- or the end.
    size_t lhs_end = 0;
    while (lhs_end < args.size() && !isBlank(args[lhs_end]))
    {
        ++lhs_end;
    }
    const std::string lhs_text(args.substr(0, lhs_end));
    std::string       rhs_text;
    for (size_t i = lhs_end; i < args.size(); ++i)
    {
        if (args[i] == '\\' && i + 1 < args.size() && args[i + 1] == '|')
        {
            rhs_text += '|';
            ++i;
            continue;
        }
        if (args[i] == '|')
        {
            break;
        }
        rhs_text += args[i];
    }
    LLStringUtil::trim(rhs_text);
    const std::vector<ALVimInput> from = keysOf(lhs_text);

    if (family->kind == Kind::Unmap)
    {
        if (from.empty())
        {
            error = alSaid("VimInvalidArgument", "E474: Invalid argument");
            return true;
        }
        bool found = false;
        for (Mapping& each : mMappings)
        {
            if ((each.modes & modes) && sameKeys(each.from, from))
            {
                each.modes &= static_cast<U8>(~modes);
                found = true;
            }
        }
        std::erase_if(mMappings, [](const Mapping& each) { return each.modes == 0; });
        if (!found)
        {
            error = alSaid("VimNoSuchMapping", "E31: No such mapping");
        }
        return true;
    }

    // Keys alone, or none: what there is, listed.
    if (rhs_text.empty())
    {
        listing = list(modes, from);
        return true;
    }
    if (from.empty())
    {
        error = alSaid("VimInvalidArgument", "E474: Invalid argument");
        return true;
    }
    if (modes == 0)
    {
        // Select mode's, which there is none of here.
        return true;
    }
    if (unique)
    {
        for (const Mapping& each : mMappings)
        {
            if ((each.modes & modes) && sameKeys(each.from, from))
            {
                error = alSaid("VimMappingExists", "E227: Mapping already exists for [KEYS]", { { "[KEYS]", lhs_text } });
                return true;
            }
        }
    }
    // One mapping for all its modes; any other with the same keys in any
    // of them gives those modes up to it.
    for (Mapping& each : mMappings)
    {
        if (sameKeys(each.from, from))
        {
            each.modes &= static_cast<U8>(~modes);
        }
    }
    std::erase_if(mMappings, [](const Mapping& each) { return each.modes == 0; });
    Mapping made;
    made.from    = from;
    made.to      = keysOf(rhs_text);
    made.modes   = modes;
    made.noremap = family->kind == Kind::Noremap;
    made.nowait  = nowait;
    made.vimrc   = vimrc;
    mMappings.push_back(std::move(made));
    return true;
}

std::string ALVimMappings::list(U8 modes, const std::vector<ALVimInput>& from) const
{
    std::string out;
    for (const Mapping& each : mMappings)
    {
        if (!(each.modes & modes) || !startsWith(each.from, from))
        {
            continue;
        }
        if (!out.empty())
        {
            out += '\n';
        }
        const std::string keys = shown(each.from);
        out += llformat("%-3s%-12s %c %s", modesShown(each.modes).c_str(), keys.c_str(), each.noremap ? '*' : ' ', shown(each.to).c_str());
    }
    return out.empty() ? alSaid("VimNoMappingFound", "No mapping found") : out;
}

bool ALVimMappings::let(const std::string& args, std::string& error)
{
    const size_t eq = args.find('=');
    if (eq == std::string::npos)
    {
        return false;
    }
    std::string name = args.substr(0, eq);
    LLStringUtil::trim(name);
    if (name.compare(0, 2, "g:") == 0)
    {
        name.erase(0, 2);
    }
    if (name != "mapleader" && name != "maplocalleader")
    {
        return false;
    }
    std::string value = args.substr(eq + 1);
    LLStringUtil::trim(value);
    std::string read;
    bool        closed = false;
    if (!value.empty() && value[0] == '"')
    {
        for (size_t i = 1; i < value.size(); ++i)
        {
            if (value[i] == '"')
            {
                closed = i + 1 == value.size();
                break;
            }
            if (value[i] == '\\' && i + 1 < value.size())
            {
                // \<Space> is the key, spelt as the notation spells it; \\ and
                // \" what follows them.
                ++i;
                if (value[i] == '<')
                {
                    const size_t close = value.find('>', i);
                    if (close != std::string::npos)
                    {
                        read += value.substr(i, close - i + 1);
                        i = close;
                        continue;
                    }
                }
                read += value[i] == 't' ? '\t' : value[i];
                continue;
            }
            read += value[i];
        }
    }
    else if (!value.empty() && value[0] == '\'')
    {
        for (size_t i = 1; i < value.size(); ++i)
        {
            if (value[i] == '\'')
            {
                if (i + 1 < value.size() && value[i + 1] == '\'')
                {
                    read += '\'';
                    ++i;
                    continue;
                }
                closed = i + 1 == value.size();
                break;
            }
            read += value[i];
        }
        // A quote is itself here, where the notation would take a < as
        // the start of a name: spelt so that it is read as it is.
        std::string spelt;
        for (const char c : read)
        {
            spelt += c == '<' ? std::string("<lt>") : std::string(1, c);
        }
        read = spelt;
    }
    if (!closed)
    {
        error = alSaid("VimBadExpression", "E15: Invalid expression: [VALUE]", { { "[VALUE]", value } });
        return true;
    }
    (name == "mapleader" ? mLeader : mLocalLeader) = read;
    return true;
}

// --- matching ------------------------------------------------------------------------

ALVimMappings::Match ALVimMappings::match(U8 mode, const std::vector<ALVimInput>& keys, bool more_may_come) const
{
    Match out;
    for (const Mapping& each : mMappings)
    {
        if (!(each.modes & mode))
        {
            continue;
        }
        size_t n = 0;
        while (n < each.from.size() && n < keys.size() && each.from[n].sameAs(keys[n]))
        {
            ++n;
        }
        if (n == each.from.size())
        {
            if (!out.full || each.from.size() > out.full->from.size())
            {
                out.full = &each;
            }
        }
        else if (n == keys.size() && more_may_come)
        {
            out.longer = true;
        }
    }
    return out;
}

bool ALVimMappings::starts(U8 mode, const ALVimInput& key) const
{
    for (const Mapping& each : mMappings)
    {
        if ((each.modes & mode) && !each.from.empty() && each.from.front().sameAs(key))
        {
            return true;
        }
    }
    return false;
}

size_t ALVimMappings::longest(U8 mode) const
{
    size_t most = 0;
    for (const Mapping& each : mMappings)
    {
        if (each.modes & mode)
        {
            most = std::max(most, each.from.size());
        }
    }
    return most;
}

void ALVimMappings::forgetVimrc()
{
    std::erase_if(mMappings, [](const Mapping& each) { return each.vimrc; });
    mLeader      = "\\";
    mLocalLeader = "\\";
}
