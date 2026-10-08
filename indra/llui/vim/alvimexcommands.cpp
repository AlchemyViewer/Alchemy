/**
 * @file alvimexcommands.cpp
 * @brief Vim's : commands: running them, :s and its asking, :g, :set, :registers and the vimrc.
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

#include "alvimexcommands.h"

#include "albracketindex.h"
#include "alsaid.h"
#include "altextchars.h"
#include "altextsearch.h"
#include "alvimhost.h"
#include "alvimtext.h"
#include "llstring.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>
#include <string_view>

using namespace ALVimText;

// --- options ---------------------------------------------------------------------------

namespace
{
    // One option as :set is given it: its name, and what is done with it
    // -- set, unset, toggled (inv, !), shown (?), set back (&), given a
    // value (= or :), or added to or taken from (+= -=).
    struct OptionSetting
    {
        enum class Op : U8
        {
            On,
            Off,
            Toggle,
            Query,
            Default,
            Assign,
            Add,
            Remove
        };
        std::string name;
        Op          op = Op::On;
        std::string value;
    };

    OptionSetting optionSetting(const std::string& word)
    {
        typedef OptionSetting::Op Op;
        OptionSetting             out;
        std::string               rest = word;
        if (const size_t at = rest.find_first_of("=:"); at != std::string::npos)
        {
            out.value = rest.substr(at + 1);
            rest.erase(at);
            out.op = Op::Assign;
            if (!rest.empty() && (rest.back() == '+' || rest.back() == '-' || rest.back() == '^'))
            {
                out.op = rest.back() == '-' ? Op::Remove : Op::Add;
                rest.pop_back();
            }
        }
        else if (!rest.empty() && (rest.back() == '?' || rest.back() == '!' || rest.back() == '&'))
        {
            out.op = rest.back() == '?' ? Op::Query : rest.back() == '!' ? Op::Toggle : Op::Default;
            rest.pop_back();
        }
        else if (rest.compare(0, 3, "inv") == 0)
        {
            out.op = Op::Toggle;
            rest.erase(0, 3);
        }
        else if (rest.compare(0, 2, "no") == 0)
        {
            out.op = Op::Off;
            rest.erase(0, 2);
        }
        out.name = rest;
        return out;
    }

    // What follows :set, a word an option: split at blanks, a backslash
    // keeping the character after it.
    std::vector<std::string> optionWords(const std::string& args)
    {
        std::vector<std::string> out;
        std::string              word;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == '\\' && i + 1 < args.size())
            {
                word += args[++i];
            }
            else if (args[i] == ' ' || args[i] == '\t')
            {
                if (!word.empty())
                {
                    out.push_back(word);
                    word.clear();
                }
            }
            else
            {
                word += args[i];
            }
        }
        if (!word.empty())
        {
            out.push_back(word);
        }
        return out;
    }

    std::string badOption(const std::string& word)
    {
        return alSaid("VimBadOptionValue", "E474: Invalid argument: [OPTION]", { { "[OPTION]", word } });
    }

    // A boolean option: set, unset, toggled, set back to `fallback`, or
    // shown; a value is no value for it.
    bool setFlag(bool& flag, bool fallback, const OptionSetting& setting, const char* name, const std::string& word, std::string& shown,
                 std::string& error)
    {
        typedef OptionSetting::Op Op;
        switch (setting.op)
        {
            case Op::On:      flag = true; break;
            case Op::Off:     flag = false; break;
            case Op::Toggle:  flag = !flag; break;
            case Op::Default: flag = fallback; break;
            case Op::Query:   shown = (flag ? "  " : "no") + std::string(name); break;
            default:          error = badOption(word); break;
        }
        return true;
    }

    // A number option, from `least` to `most`: given, added to or taken
    // from, set back to `fallback`, or shown -- by its name alone, too.
    bool setNumber(S32& number, S32 fallback, S32 least, S32 most, const OptionSetting& setting, const char* name, const std::string& word,
                   std::string& shown, std::string& error)
    {
        typedef OptionSetting::Op Op;
        switch (setting.op)
        {
            case Op::On:
            case Op::Query:
                shown = fmt::format("  {}={}", name, number);
                return true;
            case Op::Default:
                number = fallback;
                return true;
            case Op::Assign:
            case Op::Add:
            case Op::Remove:
            {
                const std::string& value = setting.value;
                if (value.empty() || value.size() > 6 || value.find_first_not_of("0123456789") != std::string::npos)
                {
                    error = badOption(word);
                    return true;
                }
                const S32 given = std::atoi(value.c_str());
                const S32 to    = setting.op == Op::Assign ? given : setting.op == Op::Add ? number + given : number - given;
                if (to < least || to > most)
                {
                    error = badOption(word);
                    return true;
                }
                number = to;
                return true;
            }
            default:
                error = badOption(word);
                return true;
        }
    }

    // One of the options the mode keeps, set; false where the option is
    // none of those.
    bool setSharedOption(ALVimKeymap::Shared& shared, const OptionSetting& setting, const std::string& word, std::string& shown,
                         std::string& error)
    {
        const std::string& name = setting.name;
        if (name == "ic" || name == "ignorecase")
        {
            return setFlag(shared.ignoreCase, false, setting, "ignorecase", word, shown, error);
        }
        if (name == "scs" || name == "smartcase")
        {
            return setFlag(shared.smartCase, false, setting, "smartcase", word, shown, error);
        }
        if (name == "is" || name == "incsearch")
        {
            return setFlag(shared.incrementalSearch, true, setting, "incsearch", word, shown, error);
        }
        if (name == "hls" || name == "hlsearch")
        {
            return setFlag(shared.highlightSearch, true, setting, "hlsearch", word, shown, error);
        }
        if (name == "to" || name == "timeout")
        {
            return setFlag(shared.timeout, true, setting, "timeout", word, shown, error);
        }
        if (name == "tm" || name == "timeoutlen")
        {
            return setNumber(shared.timeoutLength, 1000, 0, 100000, setting, "timeoutlen", word, shown, error);
        }
        if (name == "cb" || name == "clipboard")
        {
            // unnamed or unnamedplus, which are one clipboard here, among
            // what the value lists; none for the editor's own.
            typedef OptionSetting::Op Op;
            bool                      named = false;
            for (const std::string& part : LLStringUtil::getTokens(setting.value, ","))
            {
                named = named || part == "unnamed" || part == "unnamedplus";
            }
            switch (setting.op)
            {
                case Op::On:
                case Op::Query:
                    shown = std::string("  clipboard=") + (shared.unnamedClipboard ? "unnamed" : "");
                    break;
                case Op::Default:
                    shared.unnamedClipboard = false;
                    break;
                case Op::Assign:
                    if (named || setting.value.empty())
                    {
                        shared.unnamedClipboard = named;
                    }
                    else
                    {
                        error = badOption(word);
                    }
                    break;
                case Op::Add:
                case Op::Remove:
                    if (named)
                    {
                        shared.unnamedClipboard = setting.op == Op::Add;
                    }
                    break;
                default:
                    error = badOption(word);
                    break;
            }
            return true;
        }
        return false;
    }

    bool isViewOption(const std::string& name)
    {
        return name == "wrap" || name == "et" || name == "expandtab" || name == "ts" || name == "tabstop" || name == "sw" ||
               name == "shiftwidth" || name == "sts" || name == "softtabstop";
    }
}

namespace
{
    // A register's or a line's text as vim lists it: a line break as ^J, a
    // tab as ^I, cut to what fits a row.
    std::string listed(const std::string& text, size_t most)
    {
        std::string out;
        for (char c : text)
        {
            if (out.size() >= most)
            {
                break;
            }
            out += c == '\n' ? std::string("^J") : c == '\t' ? std::string("^I") : std::string(1, c);
        }
        return out;
    }

    // What follows :d, :y or :put, read as vim's do_one_cmd reads it: from a
    // " on, a comment; a register's name -- for :d and :y, `counted`, one a
    // delete or a yank writes, a digit being their count's; for :put any it
    // reads -- then, for :d and :y, a count of lines, more than none; and
    // nothing after. False where something is left over or the count is
    // none, what vim says of it in `error`.
    bool registerArgument(std::string args, bool counted, char& name, S32& count, std::string& error)
    {
        name  = 0;
        count = 0;
        if (const size_t comment = args.find('"'); comment != std::string::npos)
        {
            args.erase(comment);
        }
        LLStringUtil::trim(args);
        size_t     at     = 0;
        const auto blanks = [&args, &at]() {
            while (at < args.size() && (args[at] == ' ' || args[at] == '\t'))
            {
                ++at;
            }
        };
        if (!args.empty())
        {
            const char c       = args[0];
            const bool written = isNameChar(c) || c == '-' || c == '_' || c == '+' || c == '*';
            const bool read    = isDigit(c) || std::string_view(".:/%#~").find(c) != std::string_view::npos;
            if (written || (!counted && read))
            {
                name = c;
                at   = 1;
                blanks();
            }
        }
        if (counted && at < args.size() && isDigit(args[at]))
        {
            S64 lines = 0;
            while (at < args.size() && isDigit(args[at]))
            {
                lines = llmin(lines * 10 + (args[at++] - '0'), static_cast<S64>(S32_MAX));
            }
            if (lines == 0)
            {
                error = alSaid("VimPositiveCount", "E939: Positive count required");
                return false;
            }
            count = static_cast<S32>(lines);
            blanks();
        }
        if (at < args.size())
        {
            error = alSaid("VimTrailingCharacters", "E488: Trailing characters: [TEXT]", { { "[TEXT]", args.substr(at) } });
            return false;
        }
        return true;
    }
}

void ALVimExCommands::askNext(ALTextView& view)
{
    if (confirming.at >= confirming.edits.size())
    {
        endConfirming(view);
        return;
    }
    // The match shown as the selection, so that the question is plainly
    // about it; the ones still to come lit, as vim lights them: all of them
    // when the asking starts, and from then on those it has passed let go,
    // the lit ones sliding with the text as the edits go in.
    const ALTextRange asked = confirming.edits[confirming.at].first;
    view.setSelection(asked);
    if (ALVimHost* host = view.vimHost())
    {
        if (!confirming.lit)
        {
            std::vector<ALTextRange> left;
            left.reserve(confirming.edits.size() - confirming.at);
            for (size_t k = confirming.at; k < confirming.edits.size(); ++k)
            {
                left.push_back(confirming.edits[k].first);
            }
            host->setLayer(ALVimHost::Layer::Confirm, std::move(left));
            confirming.lit = true;
        }
        else
        {
            host->clearLayerBefore(ALVimHost::Layer::Confirm, asked.normalised().begin);
        }
    }
    mVim.bump();
}

void ALVimExCommands::applyConfirmed(ALTextView& view, size_t index)
{
    const ALTextRange r = confirming.edits[index].first.normalised();
    const std::string t = confirming.edits[index].second;
    ALTextUndo&       journal = view.undoJournal();
    journal.resumeGroup(confirming.undoStep);
    const bool made = view.replaceAll({ { r, t } });
    journal.endGroup();
    confirming.undoStep = journal.groupStep();
    if (!made)
    {
        return;
    }
    // The ones after it moved along as it went in (slideHeld).
    if (r.begin.line != confirming.lastLine)
    {
        ++confirming.lines;
    }
    confirming.lastLine = r.begin.line;
    ++confirming.made;
}

void ALVimExCommands::applyRest(ALTextView& view)
{
    if (confirming.at >= confirming.edits.size())
    {
        return;
    }
    std::vector<std::pair<ALTextRange, std::string>> rest(std::make_move_iterator(confirming.edits.begin() + static_cast<std::ptrdiff_t>(confirming.at)),
                                                          std::make_move_iterator(confirming.edits.end()));
    confirming.at = confirming.edits.size();
    for (auto& [range, text] : rest)
    {
        range = range.normalised();
    }
    // Counted before they go in, as one at a time counts them: each on the
    // line it lies on once those before it are made.
    S32 made      = confirming.made;
    S32 lines     = confirming.lines;
    S32 last_line = confirming.lastLine;
    S32 shift     = 0;
    for (const auto& [range, text] : rest)
    {
        const S32 line = range.begin.line + shift;
        if (line != last_line)
        {
            ++lines;
        }
        last_line = line;
        ++made;
        shift += static_cast<S32>(std::count(text.begin(), text.end(), '\n')) - (range.end.line - range.begin.line);
    }
    ALTextUndo& journal = view.undoJournal();
    journal.resumeGroup(confirming.undoStep);
    const bool done = view.replaceAll(std::move(rest));
    journal.endGroup();
    confirming.undoStep = journal.groupStep();
    if (!done)
    {
        return;
    }
    confirming.made     = made;
    confirming.lines    = lines;
    confirming.lastLine = last_line;
}

bool ALVimExCommands::confirmKey(ALTextView& view, const ALVimInput& input)
{
    if (!input.isChar)
    {
        if (input.key == KEY_ESCAPE)
        {
            endConfirming(view);
            return true;
        }
        // Control-E and Control-Y scroll while the question stands, as
        // they do in vim, the match staying where it is.
        if ((input.mask & ALVimInput::CONTROL) && (input.key == 'E' || input.key == 'Y'))
        {
            view.setScrollY(view.scrollY() + (input.key == 'E' ? 1 : -1) * view.layout().rowHeight());
            return true;
        }
        // A plain key's character follows; a chord or a Return is nobody's.
        return input.key == KEY_RETURN || (input.mask & (ALVimInput::CONTROL | MASK_CONTROL | MASK_ALT)) != 0;
    }
    switch (input.ch)
    {
        case 'y':
            applyConfirmed(view, confirming.at++);
            askNext(view);
            return true;
        case 'n':
            ++confirming.at;
            askNext(view);
            return true;
        case 'l':
            applyConfirmed(view, confirming.at++);
            endConfirming(view);
            return true;
        case 'a':
            applyRest(view);
            endConfirming(view);
            return true;
        case 'q':
            endConfirming(view);
            return true;
        default:
            return true;
    }
}

void ALVimExCommands::endConfirming(ALTextView& view)
{
    const ALTextDocument& d = view.document();
    mVim.setMode(view, ALVimKeymap::Mode::Normal);
    if (confirming.lastLine >= 0)
    {
        const S32 line = llclamp(confirming.lastLine, 0, d.lineCount() - 1);
        mVim.moveTo(view, ALTextPos(line, firstNonBlankColumn(d, line)));
    }
    else
    {
        mVim.moveTo(view, view.caret());
    }
    // As :s says it (substitute), typed where the answer that ended the
    // asking was.
    if (confirming.made > REPORT_THRESHOLD && (confirming.lines > 1 || mVim.keyTyped()))
    {
        mVim.say(ALVimKeymap::substitutionsSaid(confirming.made, confirming.lines));
    }
    const bool changed = confirming.made > 0;
    confirming        = Confirming();
    if (ALVimHost* host = view.vimHost())
    {
        host->clearLayer(ALVimHost::Layer::Confirm);
    }
    mVim.finishCommand(changed);
    mVim.bump();
}

bool ALVimExCommands::lineAddress(ALTextView& view, const std::string& line, size_t& at_, S32& out, bool* before_first) const
{
    const ALTextDocument& d = view.document();
    if (at_ >= line.size())
    {
        return false;
    }
    if (line[at_] == '.')
    {
        out = view.caret().line;
        ++at_;
    }
    else if (line[at_] == '+' || line[at_] == '-')
    {
        // An offset alone is from the caret's line: +1 is .+1.
        out = view.caret().line;
    }
    else if (line[at_] == '$')
    {
        // The last line: not the empty one after a final line break,
        // which vim does not count as a line.
        out = d.lineCount() - 1;
        if (out > 0 && d.lineLength(out) == 0)
        {
            --out;
        }
        ++at_;
    }
    else if (line.compare(at_, 2, "'<") == 0 || line.compare(at_, 2, "'>") == 0)
    {
        const ALTextPos a = std::min(mVim.mVisualLastAnchor, mVim.mVisualLastCaret);
        const ALTextPos b = std::max(mVim.mVisualLastAnchor, mVim.mVisualLastCaret);
        out               = line[at_ + 1] == '<' ? a.line : b.line;
        at_ += 2;
    }
    else if (line[at_] == '\'' && at_ + 1 < line.size() && line[at_ + 1] >= 'a' && line[at_ + 1] <= 'z')
    {
        // A mark's line.
        const auto mark = mVim.mMarks.find(line[at_ + 1]);
        if (mark == mVim.mMarks.end())
        {
            return false;
        }
        out = mark->second.line;
        at_ += 2;
    }
    else if (isDigit(line[at_]))
    {
        S32 n = 0;
        while (at_ < line.size() && isDigit(line[at_]))
        {
            n = n * 10 + (line[at_++] - '0');
        }
        // As the lines are numbered where they are shown.
        out = n - 1 - view.lineNumberBase();
    }
    else
    {
        return false;
    }
    // An offset: .+3, $-1.
    if (at_ < line.size() && (line[at_] == '+' || line[at_] == '-'))
    {
        const bool plus = line[at_++] == '+';
        S32        n    = 0;
        bool       any  = false;
        while (at_ < line.size() && isDigit(line[at_]))
        {
            n   = n * 10 + (line[at_++] - '0');
            any = true;
        }
        out += (plus ? 1 : -1) * (any ? n : 1);
    }
    if (before_first)
    {
        *before_first = out < 0;
    }
    out = llclamp(out, 0, d.lineCount() - 1);
    return true;
}

// static
bool ALVimExCommands::setViewOption(ALTextView& view, const std::string& word, std::string& shown, std::string& error)
{
    const OptionSetting setting = optionSetting(word);
    const std::string&  name    = setting.name;
    if (name == "wrap")
    {
        bool wrap = view.getWordWrap();
        setFlag(wrap, true, setting, "wrap", word, shown, error);
        view.setWordWrap(wrap);
        return true;
    }
    if (name == "et" || name == "expandtab")
    {
        bool spaces = view.getSoftTabs();
        setFlag(spaces, false, setting, "expandtab", word, shown, error);
        view.setSoftTabs(spaces);
        return true;
    }
    if (isViewOption(name))
    {
        // One width for a tab, an indent and a Tab typed, here; softtabstop
        // 0, vim's own, leaves it be.
        if ((name == "sts" || name == "softtabstop") && setting.op == OptionSetting::Op::Assign && setting.value == "0")
        {
            return true;
        }
        S32 width = view.getTabWidth();
        const char* whole = name.size() > 3 ? name.c_str() : name == "ts" ? "tabstop" : name == "sw" ? "shiftwidth" : "softtabstop";
        setNumber(width, 4, 1, 16, setting, whole, word, shown, error);
        view.setTabWidth(width);
        return true;
    }
    return false;
}

// static
void ALVimExCommands::source(ALVimKeymap::Shared& shared, std::string_view text, const std::function<bool(const std::string& option)>& host,
                         std::vector<std::string>& errors)
{
    shared.mappings.forgetVimrc();
    shared.viewOptions.clear();
    // The options as vim has them before any vimrc: one a line took out
    // of it no longer sets does not stay set. The lines typed are kept,
    // and so is clipboard, which the host has from a setting of its own.
    const ALVimKeymap::Shared vims;
    shared.ignoreCase        = vims.ignoreCase;
    shared.smartCase         = vims.smartCase;
    shared.highlightSearch   = vims.highlightSearch;
    shared.incrementalSearch = vims.incrementalSearch;
    shared.timeout           = vims.timeout;
    shared.timeoutLength     = vims.timeoutLength;
    // Its lines, with their numbers; one that starts with a backslash
    // goes on the end of the one before it.
    std::vector<std::pair<S32, std::string>> lines;
    S32                                      number = 0;
    for (size_t at = 0; at <= text.size();)
    {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
        {
            end = text.size();
        }
        std::string line(text.substr(at, end - at));
        at = end + 1;
        ++number;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '\\' && !lines.empty())
        {
            lines.back().second += line.substr(first + 1);
        }
        else
        {
            lines.emplace_back(number, std::move(line));
        }
    }
    for (auto& [line_number, line] : lines)
    {
        LLStringUtil::trim(line);
        while (!line.empty() && (line[0] == ':' || line[0] == ' ' || line[0] == '\t'))
        {
            line.erase(0, 1);
        }
        if (line.empty() || line[0] == '"')
        {
            continue;
        }
        bool quiet = false;
        for (const char* prefix : { "silent!", "sil!", "silent ", "sil " })
        {
            const size_t length = strlen(prefix);
            if (line.compare(0, length, prefix) == 0)
            {
                quiet = prefix[length - 1] == '!';
                line.erase(0, length);
                LLStringUtil::trim(line);
                break;
            }
        }
        size_t name_end = 0;
        while (name_end < line.size() && isNameChar(line[name_end]))
        {
            ++name_end;
        }
        std::string name = line.substr(0, name_end);
        std::string args = line.substr(name_end);
        if (!args.empty() && args[0] == '!')
        {
            name += '!';
            args.erase(0, 1);
        }
        LLStringUtil::trim(args);
        std::string error;
        std::string listing;
        if (!name.empty() && shared.mappings.command(name, args, true, listing, error))
        {
        }
        else if (name == "let")
        {
            if (!shared.mappings.let(args, error))
            {
                error = ALVimKeymap::said("VimLetOnly", "Only mapleader and maplocalleader are set with :let");
            }
        }
        else if (isSetCommand(name))
        {
            // The mode's own options here; the host's, which the window
            // they are shown in keeps; each editor's own, for the host to
            // set on each.
            for (const std::string& word : optionWords(args))
            {
                const OptionSetting setting = optionSetting(word);
                std::string         shown;
                if (setSharedOption(shared, setting, word, shown, error))
                {
                    if (!error.empty())
                    {
                        break;
                    }
                }
                else if (!(host && host(word)))
                {
                    if (!isViewOption(setting.name))
                    {
                        error = ALVimKeymap::said("VimUnknownOption", "E518: Unknown option: [OPTION]", { { "[OPTION]", word } });
                        break;
                    }
                    shared.viewOptions.push_back(word);
                }
            }
        }
        else if (!abbreviates(name, "noh", "nohlsearch"))
        {
            error = ALVimKeymap::said("VimNotACommand", "E492: Not an editor command: [LINE]", { { "[LINE]", line } });
        }
        if (!error.empty() && !quiet)
        {
            errors.push_back(
                ALVimKeymap::said("VimrcLine", "line [NUMBER]: [ERROR]", { { "[NUMBER]", std::to_string(line_number) }, { "[ERROR]", error } }));
        }
    }
}

void ALVimExCommands::runEntered(ALTextView& view, const std::string& line, bool typed)
{
    // As it was for a line this one runs inside -- a :normal's -- once
    // this one is done.
    const bool was = mLineTyped;
    mLineTyped     = typed;
    runCommand(view, line);
    mLineTyped = was;
}

void ALVimExCommands::runCommand(ALTextView& view, const std::string& line_in)
{
    const ALTextDocument& d       = view.document();
    const bool            editing = !view.isReadOnly();
    std::string           line = line_in;
    LLStringUtil::trim(line);
    if (line.empty())
    {
        return;
    }
    // A range first: %, '<,'>, a number, ., $, 'x, or two of those with
    // a comma; then the command.
    S32    first = view.caret().line;
    S32    last  = first;
    bool   ranged = false;
    // Whether the range's last address is a line before the first -- :0 --
    // which :put puts under, and so over the first line.
    bool   before_first = false;
    size_t at_   = 0;
    auto   lineNumber = [&](S32& out) { return lineAddress(view, line, at_, out, &before_first); };
    if (line[0] == '%')
    {
        // 1,$: the empty line after a final line break is no line of the
        // text's, as $ leaves it out.
        first  = 0;
        last   = d.lineCount() - 1;
        if (last > 0 && d.lineLength(last) == 0)
        {
            --last;
        }
        ranged = true;
        at_    = 1;
    }
    else if (lineNumber(first))
    {
        last   = first;
        ranged = true;
        if (at_ < line.size() && line[at_] == ',')
        {
            ++at_;
            if (!lineNumber(last))
            {
                last = first;
            }
        }
        if (last < first)
        {
            std::swap(first, last);
            before_first = false;
        }
    }
    std::string rest = line.substr(at_);
    LLStringUtil::trim(rest);
    if (rest.empty())
    {
        if (ranged)
        {
            // A line number alone goes there, a jump.
            const ALTextPos to(last, firstNonBlankColumn(d, last));
            if (to.line != view.caret().line)
            {
                mVim.noteJump(view, mVim.cursor(view));
            }
            mVim.moveTo(view, to);
        }
        return;
    }
    // The command's name: letters -- and after the first, underscores,
    // which a host's names have (go_to_line) and vim's do not -- or one
    // symbol.
    size_t name_end = 0;
    while (name_end < rest.size() && (isNameChar(rest[name_end]) || (name_end > 0 && rest[name_end] == '_')))
    {
        ++name_end;
    }
    if (name_end == 0)
    {
        name_end = 1;
    }
    std::string name = rest.substr(0, name_end);
    std::string args = rest.substr(name_end);
    // The bang belongs to the name: q!, w!.
    if (!args.empty() && args[0] == '!')
    {
        name += '!';
        args.erase(0, 1);
    }
    LLStringUtil::trim(args);
    // The keymap's own commands by any of their names vim takes, from the
    // least to the whole (ALVimText::abbreviates); those that take a bang
    // with one.
    const bool        bang = name.back() == '!';
    const std::string bare = bang ? name.substr(0, name.size() - 1) : name;
    const auto        is   = [&bare](std::string_view least, std::string_view whole) { return abbreviates(bare, least, whole); };

    const bool substituting = !bang && is("s", "substitute");
    if (substituting || name == "&" || name == "~")
    {
        // :& and :&& do the last one again; :~ likewise, on the last
        // pattern searched for, which here is the same one.
        if (!substitute(view, first, last, substituting ? args : "&" + args))
        {
            return;
        }
        mVim.finishCommand(true);
        return;
    }
    if (!bang && (is("d", "delete") || is("y", "yank")))
    {
        // Into the register named after it, or the unnamed one -- not one
        // named before the : -- and a count after that is how many lines
        // from the range's last, as many as there are: the empty line after
        // a final line break none of them, as $ leaves it out.
        const bool  yank  = is("y", "yank");
        char        named = 0;
        S32         lines = 0;
        std::string error;
        if (!registerArgument(args, true, named, lines, error))
        {
            mVim.say(error, true);
            return;
        }
        if (lines > 0)
        {
            S32 end = d.lineCount() - 1;
            if (end > 0 && d.lineLength(end) == 0)
            {
                --end;
            }
            first = last;
            last  = llmax(first, static_cast<S32>(llmin(static_cast<S64>(first) + lines - 1, static_cast<S64>(end))));
        }
        // The caret left where it is by :yank, as vim's leaves it.
        const ALTextPos   caret = view.caret();
        ALVimKeymap::Span span;
        span.linewise  = true;
        span.range     = ALTextRange(d.lineStart(first), d.lineEnd(last));
        const char was = std::exchange(mVim.mRegister, named);
        mVim.applyOperator(view, yank ? 'y' : 'd', span, 1);
        mVim.mRegister = was;
        if (yank)
        {
            mVim.moveTo(view, caret);
            return;
        }
        mVim.finishCommand(true);
        return;
    }
    if (name == ">" || name == "<")
    {
        // Doubled for two steps and so on; a count after it is how many
        // lines from the range's start.
        S32         steps = 1;
        std::string rest  = args;
        while (!rest.empty() && rest[0] == name[0])
        {
            ++steps;
            rest.erase(0, 1);
        }
        LLStringUtil::trim(rest);
        if (!rest.empty() && isDigit(rest[0]))
        {
            const S32 n = std::atoi(rest.c_str());
            first       = ranged ? last : first;
            last        = llmin(d.lineCount() - 1, first + llmax(1, n) - 1);
        }
        ALVimKeymap::Span span;
        span.linewise = true;
        span.range    = ALTextRange(d.lineStart(first), d.lineEnd(last));
        mVim.applyOperator(view, name[0], span, steps);
        mVim.finishCommand(true);
        return;
    }
    if (!bang && (is("m", "move") || bare == "t" || is("co", "copy")))
    {
        // The lines below the line the address names moved there, or
        // copied there; to the top for one before the first, as :0put
        // reads it -- 0 where lines are numbered from 1, .-1 from the first
        // line -- where 0 under lines numbered from 0 is the first line.
        if (view.isReadOnly())
        {
            return;
        }
        size_t at  = 0;
        S32    to  = -1;
        bool   top = false;
        if (!lineAddress(view, args, at, to, &top))
        {
            mVim.say(ALVimKeymap::said("VimInvalidAddress", "E14: Invalid address"), true);
            return;
        }
        if (top)
        {
            to = -1;
        }
        const bool move = name[0] == 'm';
        if (move && to >= first && to <= last)
        {
            if (to != last)
            {
                mVim.say(ALVimKeymap::said("VimMoveIntoItself", "E134: Cannot move a range of lines into itself"), true);
            }
            return;
        }
        // Under line `to`, -1 for the top, as one change (ALTextEditing):
        // the caret on the last line put there.
        const std::optional<ALTextEditing::Change> change =
            move ? ALTextEditing::moveLinesTo(d, first, last, to) : std::optional<ALTextEditing::Change>(ALTextEditing::copyLinesTo(d, first, last, to));
        if (change)
        {
            view.apply(*change);
        }
        const S32 landed = change ? change->caret.line : last;
        mVim.moveTo(view, ALTextPos(llclamp(landed, 0, d.lineCount() - 1), 0));
        mVim.moveTo(view, ALTextPos(view.caret().line, firstNonBlankColumn(d, view.caret().line)));
        // How many lines were copied, or moved -- not by a :g, which vim
        // says nothing of -- for more than vim's report.
        if (change && !move)
        {
            mVim.sayMoreLines(last - first + 1);
        }
        else if (change && !mInGlobal && last - first + 1 > REPORT_THRESHOLD)
        {
            mVim.say(alSaidCount("VimLinesMoved", last - first + 1, "1 line moved", "[COUNT] lines moved"));
        }
        mVim.finishCommand(change.has_value());
        return;
    }
    if (is("sor", "sort"))
    {
        // The lines of the range -- the whole text without one -- in
        // order: by their text, or by the first number in each with n;
        // without regard to case with i; each kept once with u; the
        // other way round with a bang.
        if (view.isReadOnly())
        {
            return;
        }
        if (!ranged)
        {
            first = 0;
            last  = d.lineCount() - 1;
        }
        // The empty line after a final line break is no line of the
        // text's, and does not sort.
        if (last == d.lineCount() - 1 && last > first && d.lineLength(last) == 0)
        {
            --last;
        }
        const bool reverse  = bang;
        const bool numeric  = args.find('n') != std::string::npos;
        const bool ignore   = args.find('i') != std::string::npos;
        const bool unique   = args.find('u') != std::string::npos;
        // Each line's key worked out once, not at every comparison: its
        // text as it is, or cased alike with i, or its first number with n.
        struct Line
        {
            std::string text;
            std::string folded;
            bool        numbered = false;
            S64         number   = 0;
        };
        auto number = [](const std::string& text, Line& line) {
            size_t at = text.find_first_of("0123456789");
            if (at == std::string::npos)
            {
                return;
            }
            const bool negative = at > 0 && text[at - 1] == '-';
            S64        n        = 0;
            while (at < text.size() && isDigit(text[at]))
            {
                n = n * 10 + (text[at++] - '0');
            }
            line.numbered = true;
            line.number   = negative ? -n : n;
        };
        std::vector<Line> lines;
        lines.reserve(static_cast<size_t>(last - first + 1));
        for (S32 l = first; l <= last; ++l)
        {
            Line& line = lines.emplace_back();
            line.text  = d.line(l);
            if (numeric)
            {
                number(line.text, line);
            }
            else if (ignore)
            {
                line.folded = alRecased(line.text, 'u');
            }
        }
        auto before = [&](const Line& a, const Line& b) {
            if (numeric)
            {
                // Lines with no number come first, in their order.
                if (a.numbered != b.numbered)
                {
                    return !a.numbered;
                }
                return a.number < b.number;
            }
            return ignore ? a.folded < b.folded : a.text < b.text;
        };
        std::stable_sort(lines.begin(), lines.end(), before);
        if (unique)
        {
            lines.erase(std::unique(lines.begin(), lines.end(), [&](const Line& a, const Line& b) { return !before(a, b) && !before(b, a); }), lines.end());
        }
        if (reverse)
        {
            std::reverse(lines.begin(), lines.end());
        }
        std::string sorted;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0)
            {
                sorted += '\n';
            }
            sorted += lines[i].text;
        }
        view.replaceAll({ { ALTextRange(d.lineStart(first), d.lineEnd(last)), sorted } });
        mVim.moveTo(view, ALTextPos(first, firstNonBlankColumn(d, first)));
        mVim.finishCommand(true);
        return;
    }
    if (is("g", "global") || (!bang && is("v", "vglobal")))
    {
        if (global(view, first, last, ranged, args, name[0] == 'v' || bang))
        {
            mVim.finishCommand(true);
        }
        return;
    }
    if (is("norm", "normal"))
    {
        // The keys as if typed in normal mode, on each line of the range
        // in turn, from the line's first character; whatever mode they
        // leave behind is left.
        // One step to undo over the whole range. A line whose keys fail --
        // f with no such character there -- stops only that line's, as in
        // vim; an error stops the range.
        const std::vector<ALVimInput> inputs = ALVimKeymap::decodeInputs(args);
        view.undoJournal().beginGroup();
        for (S32 line = first; line <= last && line < d.lineCount(); ++line)
        {
            if (mVim.mMode != ALVimKeymap::Mode::Normal)
            {
                ALVimInput escape;
                escape.key = KEY_ESCAPE;
                mVim.feed(view, escape);
            }
            view.setCaret(ALTextPos(line, 0));
            mVim.moveTo(view, view.caret());
            if (!mVim.play(view, inputs, !bang) && mVim.mMessageError)
            {
                break;
            }
        }
        if (mVim.mMode == ALVimKeymap::Mode::Insert || mVim.mMode == ALVimKeymap::Mode::Replace || mVim.isVisual())
        {
            ALVimInput escape;
            escape.key = KEY_ESCAPE;
            mVim.feed(view, escape);
        }
        view.undoJournal().endGroup();
        return;
    }
    if (is("j", "join"))
    {
        // The range's lines, the last and one more where it is one line;
        // or, with a count, that many from the range's last.
        S32 from  = first;
        S32 until = last > first ? last : first + 1;
        if (const S32 lines = args.empty() ? 0 : std::atoi(args.c_str()); lines > 0)
        {
            from  = last;
            until = last + lines - 1;
        }
        if (editing)
        {
            if (const std::optional<ALTextEditing::Change> join = ALTextEditing::joinLines(d, from, until, bang))
            {
                view.apply(*join);
                mVim.moveTo(view, join->caret);
            }
        }
        return;
    }
    if (is("ret", "retab"))
    {
        // Every line's leading blanks, or the range's, as the tabs are set:
        // spaces where they are, else tabs; a width given sets it, the
        // blanks measured as the tabs were.
        const S32 was = view.getTabWidth();
        if (const S32 width = args.empty() ? 0 : std::atoi(args.c_str()); width > 0)
        {
            view.setTabWidth(llclamp(width, 1, 16));
        }
        if (editing)
        {
            view.convertIndentation(ranged ? first : 0, ranged ? last : d.lineCount() - 1, view.getSoftTabs(), was);
        }
        return;
    }
    if (!bang && (is("u", "undo") || is("red", "redo")))
    {
        view.perform(name[0] == 'u' ? ALEditorCommand::Undo : ALEditorCommand::Redo);
        mVim.moveTo(view, view.caret());
        return;
    }
    if (is("pu", "put"))
    {
        // A register's text as lines, whatever it was taken as: under the
        // range's last line, or above it with !, as vim's [line] is the
        // range's last; above the first line for :0put, under the line
        // before it. Nothing is a register never set, as for p; one set to
        // no text, as yiw on an empty line sets one, is an empty line to
        // put, as an empty line is, and so is an empty last line of
        // several. What _ gives back is no text, which as a line is an
        // empty one. The register is the one named after it, as for :d
        // and :y, or the unnamed one.
        char        named = 0;
        S32         lines = 0;
        std::string error;
        if (!registerArgument(args, false, named, lines, error))
        {
            mVim.say(error, true);
            return;
        }
        if (!editing)
        {
            return;
        }
        const ALVimRegisters::Register reg = mVim.fetch(named);
        if (!reg.held && named != '_')
        {
            mVim.say(ALVimKeymap::said("VimNothingInRegister", "E353: Nothing in register [REGISTER]", { { "[REGISTER]", std::string(1, named ? named : '"') } }), true);
            return;
        }
        // Characters ending in a line break -- text copied from elsewhere,
        // as often as not -- are the lines before it. Lines and a block's
        // rows are kept with no break after the last: one there is before
        // an empty last line.
        std::string text = reg.text;
        if (!reg.linewise && !reg.block && !text.empty() && text.back() == '\n')
        {
            text.pop_back();
        }
        const bool above = bang || before_first;
        const S32  line  = before_first ? 0 : last;
        view.setCaret(above ? d.lineStart(line) : d.lineEnd(line));
        view.insertText(above ? text + "\n" : "\n" + text);
        // The caret on the last line put, at its first non-blank, as vim
        // leaves it.
        const S32 put_at   = above ? line : line + 1;
        const S32 put_last = put_at + static_cast<S32>(std::count(text.begin(), text.end(), '\n'));
        mVim.moveTo(view, ALTextPos(put_last, firstNonBlankColumn(d, put_last)));
        mVim.sayMoreLines(put_last - put_at + 1);
        return;
    }
    if (!bang && (is("ma", "mark") || bare == "k"))
    {
        // A mark at the range's last line, as m would set it there.
        if (args.size() == 1 && ((args[0] >= 'a' && args[0] <= 'z') || args[0] == '\'' || args[0] == '`'))
        {
            mVim.mMarks[args[0]] = ALTextPos(last, 0);
        }
        else
        {
            mVim.say(ALVimKeymap::said("VimBadMark", "E191: Argument must be a letter or forward/backward quote"), true);
        }
        return;
    }
    if (!bang && (is("le", "left") || is("ri", "right") || is("ce", "center")))
    {
        // The range's lines to the left with an indent, or to the right or
        // centred within a width: eighty, where none is given.
        if (!editing)
        {
            return;
        }
        const bool left   = name[0] == 'l';
        const S32  amount = args.empty() ? (left ? 0 : 80) : llmax(0, std::atoi(args.c_str()));
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (S32 line = first; line <= last; ++line)
        {
            std::string text = d.line(line);
            LLStringUtil::trim(text);
            if (text.empty())
            {
                continue;
            }
            const S32 size   = static_cast<S32>(text.size());
            const S32 indent = left ? amount : name[0] == 'r' ? llmax(0, amount - size) : llmax(0, (amount - size) / 2);
            edits.emplace_back(ALTextRange(d.lineStart(line), d.lineEnd(line)), std::string(static_cast<size_t>(indent), ' ') + text);
        }
        if (!edits.empty())
        {
            view.replaceAll(std::move(edits));
        }
        return;
    }
    if (!bang && (is("reg", "registers") || is("di", "display")))
    {
        listRegisters(view, args);
        return;
    }
    if (name == "marks")
    {
        listMarks(view, args);
        return;
    }
    if (name == "changes")
    {
        // As vim's :changes: each by how far back it is from where the caret
        // was last taken, with its line and column and the line's text; >
        // where that is.
        const std::vector<ALTextPos>& changes = view.changes();
        const S32                     at      = view.changeAt();
        std::string                   text    = "change line  col text";
        for (S32 i = 0; i < static_cast<S32>(changes.size()); ++i)
        {
            const ALTextPos p    = d.clamp(changes[static_cast<size_t>(i)]);
            std::string     line = d.line(p.line);
            LLStringUtil::trimHead(line);
            fmt::format_to(std::back_inserter(text), "\n{}{:5} {:5} {:4} {}", i == at ? '>' : ' ', std::abs(at - i), p.line + 1, p.column,
                           std::string_view(line).substr(0, 60));
        }
        if (at >= static_cast<S32>(changes.size()))
        {
            text += "\n>";
        }
        list(view, text);
        return;
    }
    if (!bang && is("noh", "nohlsearch"))
    {
        if (ALVimHost* host = view.vimHost())
        {
            host->clearLayer(ALVimHost::Layer::Search);
        }
        return;
    }
    {
        // The :map family, and the leaders by :let.
        std::string listing;
        std::string error;
        if (mVim.mShared->mappings.command(name, args, false, listing, error))
        {
            if (!error.empty())
            {
                mVim.say(error, true);
            }
            else if (listing.find('\n') == std::string::npos)
            {
                mVim.say(listing);
            }
            else
            {
                list(view, listing);
            }
            return;
        }
        if (name == "let")
        {
            if (!mVim.mShared->mappings.let(args, error))
            {
                error = ALVimKeymap::said("VimLetOnly", "Only mapleader and maplocalleader are set with :let");
            }
            if (!error.empty())
            {
                mVim.say(error, true);
            }
            return;
        }
    }
    if (isSetCommand(name))
    {
        // Each option in turn: the mode's own, then the view's, then the
        // host's; what a query shows, said together at the end.
        std::string shown_all;
        bool        search_lit = false;
        for (const std::string& word : optionWords(args))
        {
            std::string shown;
            std::string error;
            if (!setSharedOption(*mVim.mShared, optionSetting(word), word, shown, error) && !setViewOption(view, word, shown, error) &&
                !(mVim.mHooks.command && mVim.mHooks.command(view, "set", word)))
            {
                error = ALVimKeymap::said("VimUnknownOption", "E518: Unknown option: [OPTION]", { { "[OPTION]", word } });
            }
            if (!error.empty())
            {
                mVim.say(error, true);
                return;
            }
            if (!shown.empty())
            {
                shown_all += (shown_all.empty() ? "" : "  ") + shown;
            }
            const std::string option = optionSetting(word).name;
            search_lit               = search_lit || option == "hls" || option == "hlsearch";
        }
        if (search_lit && !mVim.mShared->highlightSearch)
        {
            if (ALVimHost* host = view.vimHost())
            {
                host->clearLayer(ALVimHost::Layer::Search);
            }
        }
        if (!shown_all.empty())
        {
            mVim.say(shown_all);
        }
        return;
    }
    if (mVim.mHooks.command && mVim.mHooks.command(view, name, args))
    {
        return;
    }
    mVim.say(ALVimKeymap::said("VimNotACommand", "E492: Not an editor command: [LINE]", { { "[LINE]", line } }), true);
}

// static
bool ALVimExCommands::globalBatches(const std::string& command)
{
    // No lines of its own -- no address before it, no count after it --
    // and it changes only the line it is put on.
    if (command.empty())
    {
        return false;
    }
    const char first = command[0];
    if (first == '>' || first == '<')
    {
        return command.find_first_not_of(first) == std::string::npos;
    }
    if (first == '&' || first == '~')
    {
        return true;
    }
    size_t name_end = 0;
    while (name_end < command.size() && isNameChar(command[name_end]))
    {
        ++name_end;
    }
    // By the names runCommand takes them by.
    const std::string name = command.substr(0, name_end);
    if (abbreviates(name, "s", "substitute"))
    {
        return true;
    }
    if (abbreviates(name, "d", "delete"))
    {
        // A register to put the line in, but no count, which would take
        // lines after it.
        char        named = 0;
        S32         lines = 0;
        std::string error;
        return registerArgument(command.substr(name_end), true, named, lines, error) && lines == 0;
    }
    return false;
}

void ALVimExCommands::applyGlobalBatch(ALTextView& view, GlobalBatch& batch)
{
    auto& edits = batch.edits;
    std::stable_sort(edits.begin(), edits.end(), [](const auto& a, const auto& b) { return a.first.begin < b.first.begin; });
    // Lines taken out one after another are one stretch: the last line's,
    // which goes with the break before it, reaches into the one above's.
    std::vector<std::pair<ALTextRange, std::string>> merged;
    merged.reserve(edits.size());
    for (auto& edit : edits)
    {
        if (!merged.empty() && edit.second.empty() && merged.back().second.empty() && !merged.back().first.empty() &&
            !(merged.back().first.end < edit.first.begin))
        {
            merged.back().first.end = std::max(merged.back().first.end, edit.first.end);
            continue;
        }
        merged.push_back(std::move(edit));
    }
    // Where the caret lands once they are in: moved by the lines those
    // before it added or took; inside one, where that one began.
    S32 landing = -1;
    if (batch.landed)
    {
        const ALTextPos at    = batch.landing;
        S32             shift = 0;
        S32             line  = at.line;
        for (const auto& [range, text] : merged)
        {
            if (range.end < at || (range.end == at && range.begin < at))
            {
                shift += static_cast<S32>(std::count(text.begin(), text.end(), '\n')) - (range.end.line - range.begin.line);
            }
            else
            {
                if (range.begin < at)
                {
                    line = range.begin.line;
                }
                break;
            }
        }
        landing = line + shift + batch.landingBelow;
    }
    if (!merged.empty() && !view.replaceAll(std::move(merged)))
    {
        return;
    }
    const ALTextDocument& d = view.document();
    if (landing >= 0)
    {
        landing = llclamp(landing, 0, d.lineCount() - 1);
        mVim.moveTo(view, ALTextPos(landing, firstNonBlankColumn(d, landing)));
    }
    // Substitutions said only over more than one line, as a :s untyped
    // says them (substitute): vim runs a :g's commands as none of them
    // typed.
    if (batch.substitutions > REPORT_THRESHOLD && batch.substitutedLines > 1)
    {
        mVim.say(ALVimKeymap::substitutionsSaid(batch.substitutions, batch.substitutedLines));
    }
    else if (batch.deletedLines > REPORT_THRESHOLD)
    {
        mVim.say(alSaidCount("VimFewerLines", batch.deletedLines, "1 fewer line", "[COUNT] fewer lines"));
    }
}

bool ALVimExCommands::global(ALTextView& view, S32 first, S32 last, bool ranged, const std::string& spec, bool invert)
{
    // g/pattern/command over the whole text unless a range was given;
    // the command the : line's own, run on each line the pattern picks
    // out, from the top down.
    if (spec.empty())
    {
        mVim.say(ALVimKeymap::said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
        return false;
    }
    if (mInGlobal)
    {
        mVim.say(ALVimKeymap::said("VimGlobalRecursive", "E147: Cannot do :global recursive"), true);
        return false;
    }
    const ALTextDocument& d   = view.document();
    const char            sep = spec[0];
    size_t                p1  = 1;
    std::string           pattern;
    while (p1 < spec.size() && spec[p1] != sep)
    {
        if (spec[p1] == '\\' && p1 + 1 < spec.size() && spec[p1 + 1] == sep)
        {
            pattern += sep;
            p1 += 2;
            continue;
        }
        pattern += spec[p1++];
    }
    std::string command = p1 < spec.size() ? spec.substr(p1 + 1) : std::string();
    LLStringUtil::trim(command);
    // The last search used again is matched as it was: without smartcase,
    // where * made it.
    const bool again = pattern.empty();
    if (again)
    {
        pattern = mVim.mSearch.pattern;
    }
    if (pattern.empty())
    {
        mVim.say(ALVimKeymap::said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
        return false;
    }
    mVim.mSearch.pattern     = pattern;
    mVim.mSearch.noSmartCase = again && mVim.mSearch.noSmartCase;
    if (!ranged)
    {
        // The whole text; the empty line after a final newline is no
        // line of the text's, as vim has none, so it is not picked out.
        first = 0;
        last  = d.lineCount() - 1;
        if (last > 0 && d.lineLength(last) == 0)
        {
            --last;
        }
    }
    ALTextSearchOptions options;
    options.regex         = true;
    const ALVimPattern pattern_in = mVim.mSearch.patternOf(pattern, mVim.mSearch.caseWithoutSmartCase(mVim.mSearch.noSmartCase));
    options.caseSensitive    = pattern_in.caseSensitive;
    const ALTextRange        scope(d.lineStart(first), d.lineEnd(last));
    std::string              error;
    std::vector<ALTextPos>   wholes;
    std::vector<ALTextRange> matches = mVim.mSearch.matchesOf(view, pattern_in, options, &scope, error, wholes);
    if (!error.empty())
    {
        mVim.say(ALVimKeymap::said("VimBadPattern", "E486: [ERROR]", { { "[ERROR]", error } }), true);
        return false;
    }
    // The matches are in order: the lines walked beside them.
    std::vector<S32> lines;
    size_t           next = 0;
    for (S32 line = first; line <= last; ++line)
    {
        while (next < matches.size() && matches[next].begin.line < line)
        {
            ++next;
        }
        const bool hit = next < matches.size() && matches[next].begin.line == line;
        if (hit != invert)
        {
            lines.push_back(line);
        }
    }
    if (lines.empty())
    {
        mVim.say(ALVimKeymap::said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", pattern } }), true);
        return false;
    }
    if (command.empty())
    {
        // Nothing to do to them: the last one is where the caret goes,
        // and how many there were is said.
        mVim.moveTo(view, ALTextPos(lines.back(), firstNonBlankColumn(d, lines.back())));
        mVim.say(alSaidCount("VimLinesPicked", static_cast<S32>(lines.size()), "1 line", "[COUNT] lines"));
        return true;
    }
    // One step to undo for the lot; an asking :s among the commands
    // gathers its edits here and the asking runs once, in order, after.
    view.undoJournal().beginGroup();
    confirming           = Confirming();
    confirming.gathering = true;
    mInGlobal             = true;
    if (globalBatches(command))
    {
        // Each line changed only where it is, in the text as it was: the
        // edits gathered from the top down and put in at once. Those of
        // the lines before an error go in, as they would have one by one.
        GlobalBatch batch;
        globalBatch = &batch;
        for (const S32 line : lines)
        {
            runCommand(view, std::to_string(line + 1 + view.lineNumberBase()) + command);
            if (mVim.mMessageError)
            {
                break;
            }
        }
        globalBatch = nullptr;
        applyGlobalBatch(view, batch);
    }
    else
    {
        // The lines marked, as vim marks them, and visited from the top
        // down, the topmost left each time: a mark moves with the text,
        // so that :g/^/m0 turns the lines over, and goes with its line --
        // taken out with its break, or joined onto the one above -- so
        // that :g/^/j joins them in pairs.
        std::vector<ALTextPos> starts;
        starts.reserve(lines.size());
        for (const S32 line : lines)
        {
            starts.emplace_back(line, 0);
        }
        ALAnchoredRanges<ALTextPos> marks;
        marks.assign(std::move(starts));
        // A command with an address of its own -- .,+1d, .m0, 'a,.d, 3d --
        // runs as it is written, from the line; one without is given the
        // line's number.
        const char lead      = command[0];
        const bool addressed = lead == '.' || lead == '$' || lead == '\'' || lead == '%' || lead == '+' || lead == '-' || isDigit(lead);
        const auto slide = [](ALTextPos& mark, const ALTextDocument::Edit& edit) {
            const ALTextPos start(mark.line, 0);
            const ALTextPos next(mark.line + 1, 0);
            const auto      takes = [&](const ALTextRange& removed) {
                return !removed.empty() && ((removed.begin <= start && next <= removed.end) ||
                                            (removed.begin < start && (start < removed.end || (start == removed.end && removed.begin.column > 0))));
            };
            if (edit.parts.empty())
            {
                if (takes(edit.range.normalised()))
                {
                    return false;
                }
            }
            else
            {
                for (auto it = std::lower_bound(edit.parts.begin(), edit.parts.end(), start,
                                                [](const ALTextDocument::Edit::Part& part, const ALTextPos& p) { return part.before.end < p; });
                     it != edit.parts.end() && it->before.begin <= start; ++it)
                {
                    if (takes(it->before))
                    {
                        return false;
                    }
                }
            }
            mark = edit.placed(mark);
            return true;
        };
        boost::signals2::scoped_connection following =
            view.document().onChanged([&marks, &slide](const ALTextDocument::Edit& edit) { marks.apply(edit, slide, [](ALTextPos&) {}); });
        while (!marks.empty())
        {
            const S32 line = marks.front().line;
            marks.erase(marks.begin());
            if (line >= d.lineCount())
            {
                continue;
            }
            // On the line, as vim puts the cursor there: `.` in the
            // command is the line.
            view.setCaret(ALTextPos(line, 0));
            runCommand(view, addressed ? command : std::to_string(line + 1 + view.lineNumberBase()) + command);
            if (mVim.mMessageError)
            {
                break;
            }
        }
    }
    mInGlobal             = false;
    confirming.gathering = false;
    if (mVim.mMessageError && !confirming.edits.empty())
    {
        // An error part way: what was gathered from the lines before it
        // is not asked about, as vim stops there too; said, since the
        // error alone would not say so.
        mVim.say(ALVimKeymap::said("VimNothingSubstituted", "[MESSAGE] -- nothing substituted", { { "[MESSAGE]", mVim.mMessage } }), true);
    }
    if (!confirming.edits.empty() && !mVim.mMessageError)
    {
        // The group closed while the asking waits; what is said yes to goes
        // on with the step the :g made, where it made one.
        std::sort(confirming.edits.begin(), confirming.edits.end(),
                  [](const std::pair<ALTextRange, std::string>& a, const std::pair<ALTextRange, std::string>& b) { return a.first.begin < b.first.begin; });
        view.undoJournal().endGroup();
        confirming.undoStep = view.undoJournal().groupStep();
        mVim.setMode(view, ALVimKeymap::Mode::Confirm);
        askNext(view);
        return false;
    }
    confirming = Confirming();
    view.undoJournal().endGroup();
    return !mVim.mMessageError;
}

std::string ALVimExCommands::replacementOf(const std::string& with) const
{
    return ALVimPattern::replacementOf(with);
}

bool ALVimExCommands::substitute(ALTextView& view, S32 first, S32 last, const std::string& spec)
{
    // s/pattern/replacement/flags, with whatever follows s as the
    // separator; g for every match on a line, i for any case, I for
    // case as written, n to count without changing, e to say nothing
    // where nothing matches, & first to keep the last flags. Nothing
    // after s, or & or &&, does the last one again -- on the last
    // pattern searched for, with the last replacement -- without its
    // flags, or with them after &&.
    std::string        pattern, with, flags;
    const std::string& rest = spec;
    // The last search used again is matched as it was: without smartcase,
    // where * made it.
    bool again = false;
    if (rest.empty() || rest[0] == '&')
    {
        if (mVim.mSearch.pattern.empty())
        {
            mVim.say(ALVimKeymap::said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
            return false;
        }
        pattern = mVim.mSearch.pattern;
        again   = true;
        with    = lastReplacement;
        flags   = rest.empty() ? std::string() : rest.substr(1);
    }
    else
    {
        const char sep  = rest[0];
        size_t     p1   = 1;
        auto       part = [&](std::string& out) {
            out.clear();
            while (p1 < rest.size() && rest[p1] != sep)
            {
                if (rest[p1] == '\\' && p1 + 1 < rest.size() && rest[p1 + 1] == sep)
                {
                    out += sep;
                    p1 += 2;
                    continue;
                }
                out += rest[p1++];
            }
            const bool closed = p1 < rest.size();
            if (closed)
            {
                ++p1;
            }
            return closed;
        };
        part(pattern);
        if (part(with))
        {
            flags = rest.substr(p1);
        }
        if (pattern.empty())
        {
            pattern = mVim.mSearch.pattern;
            again   = true;
        }
        if (pattern.empty())
        {
            mVim.say(ALVimKeymap::said("VimNoPreviousPattern", "E35: No previous regular expression"), true);
            return false;
        }
        // The replacement as it reads with ~ put in, which is what the
        // next ~ means.
        std::string expanded;
        for (size_t i = 0; i < with.size(); ++i)
        {
            if (with[i] == '\\' && i + 1 < with.size())
            {
                expanded += with[i];
                expanded += with[++i];
            }
            else if (with[i] == '~')
            {
                expanded += lastReplacement;
            }
            else
            {
                expanded += with[i];
            }
        }
        with = expanded;
    }
    LLStringUtil::trim(flags);
    if (!flags.empty() && flags[0] == '&')
    {
        flags = lastSubstituteFlags + flags.substr(1);
    }
    mVim.mSearch.pattern        = pattern;
    mVim.mSearch.noSmartCase    = again && mVim.mSearch.noSmartCase;
    lastReplacement      = with;
    lastSubstituteFlags  = flags;
    const bool every      = flags.find('g') != std::string::npos;
    const bool anycase    = flags.find('i') != std::string::npos;
    const bool exactcase  = flags.find('I') != std::string::npos;
    const bool count_only = flags.find('n') != std::string::npos;
    const bool quiet      = flags.find('e') != std::string::npos;
    const bool asking     = flags.find('c') != std::string::npos;
    const ALTextDocument& d = view.document();
    ALTextSearchOptions   options;
    options.regex         = true;
    const ALVimPattern pattern_in = mVim.mSearch.patternOf(pattern, exactcase ? std::optional<bool>(true)
                                                                    : anycase ? std::optional<bool>(false)
                                                                              : mVim.mSearch.caseWithoutSmartCase(mVim.mSearch.noSmartCase));
    options.caseSensitive    = pattern_in.caseSensitive;
    // Without g, the first match of each line alone, nothing made of what
    // would replace the rest; not where the pattern names places its
    // matches must stand, which may pass over a line's first and keep one
    // after it.
    options.firstPerLine     = !every && pattern_in.where.empty();
    const ALTextRange     scope(d.lineStart(first), d.lineEnd(last));
    std::string           error;
    std::vector<ALTextPos>   wholes;
    // What replaces each match, made as it is found rather than by the
    // pattern run again over each; none made for a count alone.
    const std::string        format = replacementOf(with);
    std::vector<std::string> replaced;
    std::vector<ALTextRange> matches = pattern_in.matchesIn(d, options, &scope, mVim.mSearch.placesOf(view), error, wholes, format, count_only ? nullptr : &replaced);
    if (!error.empty())
    {
        mVim.say(ALVimKeymap::said("VimBadPattern", "E486: [ERROR]", { { "[ERROR]", error } }), true);
        return false;
    }
    if (matches.empty())
    {
        if (!quiet)
        {
            mVim.say(ALVimKeymap::said("VimPatternNotFound", "E486: Pattern not found: [PATTERN]", { { "[PATTERN]", pattern } }), true);
        }
        return false;
    }
    std::vector<std::pair<ALTextRange, std::string>> edits;
    S32                                              seen_line = -1;
    S32                                              lines     = 0;
    for (size_t m = 0; m < matches.size(); ++m)
    {
        const ALTextRange& match = matches[m];
        if (!every && match.begin.line == seen_line)
        {
            continue;
        }
        if (match.begin.line != seen_line)
        {
            ++lines;
        }
        seen_line = match.begin.line;
        edits.emplace_back(match, count_only ? std::string() : std::move(replaced[m]));
    }
    const S32 count = static_cast<S32>(edits.size());
    if (count_only)
    {
        mVim.say(ALVimKeymap::matchesSaid(count, lines));
        return false;
    }
    if (asking && !view.isReadOnly())
    {
        if (confirming.gathering)
        {
            // A :g's line: the edits kept for the asking after the :g.
            for (auto& edit : edits)
            {
                confirming.edits.push_back(std::move(edit));
            }
            return false;
        }
        // Each match asked about in turn; the command finishes when the
        // asking ends, so nothing is done here.
        confirming       = Confirming();
        confirming.edits = std::move(edits);
        mVim.setMode(view, ALVimKeymap::Mode::Confirm);
        askNext(view);
        return false;
    }
    // The caret goes to the last line substituted on, as it will lie
    // once the edits are in: the lines the ones before it add or take.
    S32 landing = seen_line;
    for (size_t i = 0; i + 1 < edits.size(); ++i)
    {
        const ALTextRange match = edits[i].first.normalised();
        landing += static_cast<S32>(std::count(edits[i].second.begin(), edits[i].second.end(), '\n')) - (match.end.line - match.begin.line);
    }
    landing += static_cast<S32>(std::count(edits.back().second.begin(), edits.back().second.end(), '\n'));
    if (globalBatch && !view.isReadOnly())
    {
        globalBatch->landing      = edits.back().first.normalised().begin;
        globalBatch->landingBelow = static_cast<S32>(std::count(edits.back().second.begin(), edits.back().second.end(), '\n'));
        globalBatch->landed       = true;
        globalBatch->substitutions += count;
        globalBatch->substitutedLines += lines;
        for (auto& edit : edits)
        {
            globalBatch->edits.push_back(std::move(edit));
        }
        return true;
    }
    if (view.isReadOnly() || !view.replaceAll(std::move(edits)))
    {
        return false;
    }
    landing = llclamp(landing, 0, d.lineCount() - 1);
    mVim.moveTo(view, ALTextPos(landing, firstNonBlankColumn(d, landing)));
    // Said for more than vim's report, as vim's do_sub_msg says it: over
    // more than one line, or over one where the line was typed -- not
    // played, not run again by & or @:, and not a :g's command, which vim
    // runs as none of them typed.
    if (count > REPORT_THRESHOLD && (lines > 1 || (mLineTyped && mVim.keyTyped() && !mInGlobal)))
    {
        mVim.say(ALVimKeymap::substitutionsSaid(count, lines));
    }
    return true;
}

void ALVimExCommands::listRegisters(ALTextView& view, const std::string& names)
{
    // Each that holds anything, as vim's :registers has them: its kind --
    // c characters, l lines, b a block -- its name, and what it holds; the
    // last : line and the last search after them.
    std::string text = "Type Name Content";
    auto        row  = [&](char kind, char name, const std::string& held) {
        if (!held.empty() && (names.empty() || names.find(name) != std::string::npos))
        {
            fmt::format_to(std::back_inserter(text), "\n  {}  \"{}   {}", kind, name, listed(held, 70));
        }
    };
    for (const char* name = "\"0123456789abcdefghijklmnopqrstuvwxyz-"; *name; ++name)
    {
        // Lines are kept without the break that ends the last, which vim
        // shows.
        const ALVimRegisters::Register held = mVim.fetch(*name);
        row(held.block ? 'b' : held.linewise ? 'l' : 'c', *name, held.linewise && !held.text.empty() ? held.text + "\n" : held.text);
    }
    row('c', ':', mVim.mShared->command.empty() ? std::string() : mVim.mShared->command.back());
    row('c', '/', mVim.mSearch.pattern);
    list(view, text);
}

void ALVimExCommands::listMarks(ALTextView& view, const std::string& names)
{
    // Each set, as vim's :marks has them: the name, the line and column
    // it is at, and the text of that line.
    mVim.followDocument(view);
    const ALTextDocument& d    = view.document();
    std::string           text = "mark line  col file/text";
    for (const auto& [name, at] : mVim.mMarks)
    {
        if (!names.empty() && names.find(name) == std::string::npos)
        {
            continue;
        }
        const ALTextPos p = d.clamp(at);
        std::string     line = d.line(p.line);
        LLStringUtil::trimHead(line);
        fmt::format_to(std::back_inserter(text), "\n {} {:6} {:4} {}", name, p.line + 1, p.column, listed(line, 60));
    }
    list(view, text);
}

void ALVimExCommands::list(ALTextView& view, const std::string& text)
{
    if (mVim.mHooks.listing)
    {
        mVim.mHooks.listing(view, text);
    }
    else
    {
        mVim.say(text.substr(0, text.find('\n')));
    }
}
