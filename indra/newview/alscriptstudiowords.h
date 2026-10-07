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
#include "allsltraits.h"
#include "alquickopen.h"
#include "alscriptsnippets.h"
#include "alscriptsymbol.h"
#include "llsd.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

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
        // Where the offline reference files it, beside its kind: a
        // function's first category in the definitions, in words ("Avatar
        // communication"), an SLua ll function its LSL twin's, any other
        // SLua function its library's; a constant by the family its name
        // begins with ("PRIM_..."); nothing for an event.
        std::string                        group;
    };
    typedef ALScriptSnippets::Snippet Snippet;

    // Where the words come from -- the grid's definitions, the
    // preprocessor's words, the LSL wiki -- is the viewer's to say
    // (ALScriptStudioViewer).

    // The words of a language, sorted, built on the first ask after the
    // definitions change; and built again on the next ask.
    static const std::vector<Vocab>& vocabulary(bool lua);
    static void                      forget();
    // A word by its name, or none.
    static const Vocab* word(bool lua, std::string_view name);
    // Each function the YAML definitions list under `functions:`, by the
    // first of the categories it lists, as the file has it -- no more of
    // YAML than that shape; a function listing none is not there.
    typedef boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> Categories;
    static void readCategories(std::string_view yaml, Categories& out);
    // What the definitions say of a word beyond its declaration, a line
    // each: its documentation, its forced delay, that only a god may call
    // it, that it is deprecated. Nothing where they say none of it.
    static std::string notesOf(const Vocab& word);

    // The words put in an editor's tables, so that they colour: the
    // studio's editors and the preferences' previews alike. The tables are
    // a language's, made once from the vocabulary and the preprocessor's
    // words and shared by every editor of it, not copied into each.
    static void                                 teach(ALCodeEditor& editor, bool lua);
    static std::shared_ptr<const ALSyntaxWords> tables(bool lua);
    // What the hover card says of the word under the mouse, where the
    // vocabulary knows it: `Say` in `ll.Say` asked about as `ll.Say`, `pi`
    // in `math.pi` as `math.pi`. False where it is a word of the script's.
    static bool hoverText(bool lua, const ALTextDocument& text, const ALTextPos& at, std::string_view word, std::string& out);
    // What completes a word begun at a place: the vocabulary's words that
    // begin with it -- an LSL event's handler only straight inside a
    // state -- then the snippets by their prefix, said as `snippet_word`,
    // where a bare word is being typed. An SLua handler's parameters typed
    // where `typed` (completionFor).
    static void complete(bool lua, ALCodeEditor& editor, const ALTextPos& at, std::string_view prefix, const std::vector<Snippet>& snippets,
                         const std::string& snippet_word, std::vector<ALCodeEditor::Completion>& out, bool typed = false);
    // Whether a position of an LSL script is straight inside a state,
    // where an event's handler goes.
    static bool inStateBody(ALCodeEditor& editor, const ALTextPos& at);
    // Whether a function's argument, from 0, is a link number, as the
    // definitions name its parameter -- Link, LinkNumber -- where they
    // know the function.
    static bool linkArgument(bool lua, std::string_view function, S32 argument);
    // The call a place is in an argument of: the function by its name
    // before the call's bracket, with the library it is in --
    // `llSetLinkAlpha`, `ll.MessageLinked` -- and which argument from
    // nought. False where it is in none.
    static bool callAt(ALCodeEditor& editor, const ALTextPos& at, std::string& callee, S32& argument);
    // The kind of item of the object's the string at a place names, where
    // a call wants one there by its name (ALLSLTraits::itemArg): a string
    // that begins its argument, whatever blanks, lines and comments come
    // between it and the call's bracket or comma, or in SLua a call's one
    // string written with no brackets, `ll.PlaySound "door"`. And the
    // string's quote. None anywhere else.
    static ALLSLTraits::Item itemStringAt(ALCodeEditor& editor, const ALTextPos& at, bool lua, char& quote);
    // A word as a completion: a function with its call, an event as a
    // handler to fill in -- LSL's as the detail reads, SLua's a function
    // of LLEvents', its parameters typed where `typed`, as the scripter's
    // choice of Luau types for what the studio writes has it -- a constant
    // as itself.
    static ALCodeEditor::Completion completionFor(const Vocab& word, bool lua, bool typed = false);
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
