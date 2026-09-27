/**
 * @file alscriptstudiowords.h
 * @brief Script Studio's words of each language: the vocabulary, editors taught it, completions, hovers and the reference's addresses.
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

#include "alcodeeditor.h"
#include "alquickopen.h"
#include "alscriptsnippets.h"
#include "alscriptsymbol.h"
#include "llsd.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct ALScriptStudioDoc;

// The words of each language, as the grid defines them -- what colours,
// completes and is explained -- which every Script Studio window, and the
// preferences' previews, share: built once from the definitions and kept
// until they change. And what is made of them: an editor taught them, a
// word's hover, the completions at a place, the Insert menu's list, and a
// word's page in the reference. All of it is the process's, not a
// window's, so it is static.
class ALScriptStudioWords
{
public:
    // A word of the language, as the region defines it.
    struct Vocab
    {
        std::string                        text;
        std::string                        detail;
        std::string                        tooltip;
        // The tooltip as a completion carries it (ALCompletion::shared).
        std::shared_ptr<const std::string> documentation;
        ALSyntaxKind                       kind       = ALSyntaxKind::Text;
        bool                               deprecated = false;
        // A function's forced delay after it is called, in seconds as the
        // definitions say it, or nothing; and whether only a god may call
        // it.
        std::string                        sleep;
        bool                               godMode = false;
    };
    typedef ALScriptSnippets::Snippet Snippet;

    // Where the words come from: the grid's definitions of a language;
    // the preprocessor's own words while its transforms are on, which the
    // definitions do not list; and the LSL wiki's address for a page, with
    // [LSL_STRING] for its name. The viewer sets them once; a test sets
    // its own.
    struct Sources
    {
        std::function<LLSD(bool lua)>             keywords;
        std::function<std::vector<std::string>()> preprocessorWords;
        std::function<std::string()>              lslHelpUrl;
    };
    static Sources& sources();

    // The words of a language, sorted, built on the first ask after the
    // definitions change; and built again on the next ask.
    static const std::vector<Vocab>& vocabulary(bool lua);
    static void                      forget();
    // A word by its name, or none.
    static const Vocab* word(bool lua, std::string_view name);
    // What the definitions say of a word beyond its declaration, a line
    // each: its documentation, its forced delay, that only a god may call
    // it, that it is deprecated. Nothing where they say none of it.
    static std::string notesOf(const Vocab& word);

    // The words put in an editor's tables, so that they colour: the
    // studio's editors and the preferences' previews alike.
    static void teach(ALCodeEditor& editor, bool lua);
    // What the hover card says of the word under the mouse, where the
    // vocabulary knows it: `Say` in `ll.Say` asked about as `ll.Say`, `pi`
    // in `math.pi` as `math.pi`. False where it is a word of the script's.
    static bool hoverText(bool lua, const ALTextDocument& text, const ALTextPos& at, std::string_view word, std::string& out);
    // What completes a word begun at a place: the vocabulary's words that
    // begin with it -- an LSL event's handler only straight inside a
    // state -- then the snippets by their prefix, said as `snippet_word`,
    // where a bare word is being typed.
    static void complete(bool lua, ALCodeEditor& editor, const ALTextPos& at, std::string_view prefix, const std::vector<Snippet>& snippets,
                         const std::string& snippet_word, std::vector<ALCodeEditor::Completion>& out);
    // Whether a position of an LSL script is straight inside a state,
    // where an event's handler goes.
    static bool inStateBody(ALCodeEditor& editor, const ALTextPos& at);
    // A word as a completion: a function with its call, an event as a
    // handler to fill in -- LSL's as the detail reads, SLua's set on
    // LLEvents -- a constant as itself.
    static ALCodeEditor::Completion completionFor(const Vocab& word, bool lua);
    // The Insert menu's list: the snippets, or the functions, events or
    // constants, as `what` says; a deprecated word said as
    // `deprecated_word`.
    static std::vector<ALQuickOpen::Candidate> library(bool lua, const std::string& what, const std::vector<Snippet>& snippets,
                                                       const std::string& deprecated_word);

    // A word's page: the wiki's for an LSL name, which SLua's ll.Name
    // shares; the Luau library's for its libraries; the SLua portal else.
    static std::string helpUrl(bool lua, const std::string& word);
    // What the reference says of a word: its declaration, whether it is
    // deprecated, what it does, and its page.
    static std::string referenceText(const Vocab& word, bool lua);

    // The marks: what a tab is, on its tab; what a symbol's kind is, in
    // the outline -- by texture name, as a scroll list wants it.
    static const char* imageNameOf(const ALScriptStudioDoc& doc);
    static const char* imageNameOf(ALScriptSymbolKind kind);
    // What a symbol's kind is called: the name of the window's word for
    // it, or none.
    static const char* kindWordOf(ALScriptSymbolKind kind);
};
