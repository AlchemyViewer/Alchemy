/**
 * @file allsltoslua.h
 * @brief An LSL script written again as SLua, where the two languages differ noted rather than guessed.
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

#include "alscriptproblem.h"
#include "stdtypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// An LSL script written again as SLua, from Tailslide's tree of it: its
// globals and functions as locals, its states as tables of handlers put on
// LLEvents as each is entered, its expressions in Luau's operators and
// precedence, and each library call to SLua's `ll`, or to `llcompat` where
// `ll` means something else -- an index from one, a boolean for 1 or 0 --
// or lacks the function, so that what the script does stays what it did.
//
// Where the two languages mean different things and the text cannot say the
// same -- integer division rounding down, a jump SLua has no goto for,
// LSL's lists compared by their lengths -- the place is noted rather than
// guessed: a `-- LSL:` comment over the line, and a note of the same words
// at the LSL's place. Where SLua has a way of its own -- LLTimers, the
// detected table, indexing -- the note says so too.
class ALLSLToSLua
{
public:
    // How far the SLua goes from LSL's ways to SLua's own, each only where
    // it means exactly what the LSL did; what each leaves as LSL had it is
    // noted, with SLua's way. As SLua has it, by default; closeToLSL() has
    // every one off.
    struct Options
    {
        // A handler put on with LLEvents:on, which lets others listen too;
        // or assigned, LLEvents.touch_start = ..., one for each event.
        enum class Handlers : U8
        {
            On,
            Field
        };
        Handlers handlers = Handlers::On;
        // llSetTimerEvent's timer on LLTimers, calling the state's timer
        // handler, rather than the timer event llcompat sets going.
        bool llTimers = true;
        // What was detected read from the handler's own detected table --
        // detected[i + 1]:getKey() -- where the call stands in the handler,
        // rather than through llcompat.Detected*.
        bool detectedTable = true;
        // SLua's ll rather than llcompat where it means the same: a boolean
        // answer, a constant index moved on by one, a find's answer read
        // against nil.
        bool sluaCalls = true;
        // What SLua has in a call's stead where it means the same: math,
        // vector and quaternion's functions, ^, print, os.time, table.find.
        bool idioms = true;
        // Luau types on locals, parameters and what functions return, from
        // LSL's types.
        bool types = false;
        // Each place noted as a "-- LSL:" comment over its line, as well as
        // in the notes.
        bool comments = true;
        // What a note says, by its key -- the studio's own words for it,
        // in its skin's language -- where given; else the English, its
        // marks filled from the args.
        std::function<std::string(const std::string& key, const std::vector<std::string>& args, const std::string& english)> words;

        static Options closeToLSL()
        {
            Options o;
            o.llTimers      = false;
            o.detectedTable = false;
            o.sluaCalls     = false;
            o.idioms        = false;
            return o;
        }
    };

    struct Result
    {
        bool             converted = false;
        std::string      text;
        // Where the two languages differ, each where it stands in the LSL:
        // its key and the words it was said with, and as its code the lint
        // that finds the same in SLua, where one does (lintOf).
        ALScriptProblems notes;
        // Why nothing was converted: the LSL does not parse, or the
        // definitions are not loaded.
        ALScriptProblems problems;
    };

    static Result convert(std::string_view lsl, const Options& options);
    // The studio's own lint that finds in SLua what a note says of the LSL,
    // by the note's key -- SlCompatCall for an llcompat call ll's could
    // make -- or null where none does. A note carries it as its code, so
    // that its fix may be offered where the note is.
    static const char* lintOf(std::string_view note);

    // Words by their key, as Options::words gives them.
    typedef std::function<std::string(const std::string& key, const std::vector<std::string>& args, const std::string& english)> Words;
    // Each "-- LSL:" comment in SLua, as a note to see to: a note-level
    // problem over the comment, keyed SluaNote, saying its words, with Done
    // -- the comment and its line taken out -- as its fix. Where its words
    // are a note a lint finds the same as (read as `words` says them, or
    // as English does), that lint as its code.
    static ALScriptProblems notesIn(std::string_view slua, const Words& words);
    // Each such note's lint fix, where the lint said something of the line
    // the note stands over: that fix, with the note taken out too, offered
    // first.
    static void linkNotes(ALScriptProblems& notes, const ALScriptProblems& found, std::string_view slua);
    // As SLua has it (Options' defaults).
    static Result convert(std::string_view lsl);
};
