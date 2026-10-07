/**
 * @file alsnippetsession.h
 * @brief A snippet or a call being filled in, in a code editor: its stops, their mirrors, and where the caret lands.
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

#include "altextdocument.h"

#include <string>
#include <string_view>
#include <vector>

// The stops of a snippet, or of a call just completed, being filled in:
// stretches to type over in order, the one being typed over, the mirrors
// of a stop that repeat what it holds, and where the caret lands past the
// last. The stops and the mirrors move with the text as it is edited; the
// caret put on them, and the mirrors brought up, are the editor's.
class ALSnippetSession
{
public:
    // A stop's mirror: the stretch that shows what the stop, by its index
    // among the stops, holds.
    struct Mirror
    {
        S32         of = -1;
        ALTextRange range;
    };

    // A snippet's body read, to go in at `at`: its lines after the first
    // indented by `indent`, and their own indentation -- four spaces or a
    // tab a level, as bodies are written -- made again of `unit` a level,
    // where one is given: the text's own. `${1:text}`, `${1}` and `$1` its
    // placeholders, in order of their numbers -- one inside another's text
    // as well, `${1:a ${2:b}}` -- and `$0`, or `${0:text}`, where the caret
    // lands past the last. A number that comes again is a mirror of the
    // first, written as it holds. `$$` or `\$` a dollar, `\}` a brace.
    struct Expansion
    {
        // The text as it will stand.
        std::string              text;
        // Each number's first place, in order of the numbers; the places
        // after the first, its mirrors.
        std::vector<ALTextRange> stops;
        std::vector<Mirror>      mirrors;
        // Where $0 is, with its text; else the end of the text.
        ALTextRange              landing;
    };
    static Expansion expand(std::string_view body, const ALTextPos& at, const std::string& indent, std::string_view unit = {});
    static constexpr S32 BODY_LEVEL = 4;

    // The names of a signature's parameters, from how a completion's
    // detail reads: "integer llSay(integer channel, string msg)" or
    // "(channel: number, msg: string) -> ()". The list is the bracket after
    // the name where the detail has the name -- a return type may have
    // brackets of its own before it -- else the first.
    static std::vector<std::string> parameterNames(std::string_view detail, std::string_view name = std::string_view());
    // And each with its type, as the detail types it -- empty where it
    // says none -- for what writes a parameter list out again, in either
    // language's way: a variadic's name `...` however it is written
    // ("...: any", "any ...", "...any"), its type what is left. A type's
    // own brackets, `<>` among them, and the commas and colons inside
    // them, are the type's.
    struct Parameter
    {
        std::string name;
        std::string type;
    };
    static std::vector<Parameter> parameters(std::string_view detail, std::string_view name = std::string_view());
    // Where that list opens in the detail, or npos.
    static size_t parameterListAt(std::string_view detail, std::string_view name);

    // Stops to fill in, the first being typed over, the caret landing at
    // `after` past the last with `landing` bytes of it chosen.
    void start(std::vector<ALTextRange> stops, const ALTextPos& after, std::vector<Mirror> mirrors = {}, S32 landing = 0);
    void clear();
    bool active() const { return !mStops.empty(); }

    const std::vector<ALTextRange>& stops() const { return mStops; }
    // The stop being typed over, by its index, or -1.
    S32                             at() const { return mAt; }
    void                            moveTo(S32 index) { mAt = index; }
    const ALTextPos&                after() const { return mAfter; }
    S32                             landing() const { return mLanding; }
    const std::vector<Mirror>&      mirrors() const { return mMirrors; }

    // An edit heard: the stop being typed over becomes what was typed, the
    // others move with the text, and one the edit cut into goes, with its
    // mirrors; the mirror being brought up is what the edit put in. With no
    // stop left, or none being typed over, it is over.
    void slide(const ALTextDocument::Edit& edit);
    // Whether a line is one the session is still being filled in on: from
    // the first stop's line to where the call or the snippet ends.
    bool reaches(S32 line) const;
    // The mirrors of a stop that no longer read as it does, the last in
    // the text first, so that bringing each up moves none still to do; and
    // what the stop holds.
    std::vector<S32> staleMirrors(S32 index, const ALTextDocument& text, std::string& wanted) const;
    // The mirror being brought up, which the edit that does it puts in
    // whole; -1 for none.
    void syncing(S32 mirror) { mSyncing = mirror; }
    // Every mirror being made again at once, as one batch (ALTextView::
    // editMany): each a stretch replaced is grows to what went in.
    void syncingAll() { mSyncing = SYNCING_ALL; }
    bool syncing() const { return mSyncing != -1; }
    static constexpr S32 SYNCING_ALL = -2;

private:
    std::vector<ALTextRange> mStops;
    S32                      mAt = -1;
    ALTextPos                mAfter;
    std::vector<Mirror>      mMirrors;
    S32                      mSyncing = -1;
    // How long the text `${0:text}` put where the caret lands is, to be
    // chosen as it lands; nothing for a bare $0.
    S32                      mLanding = 0;
};
