/**
 * @file alluaufragment.h
 * @brief The part of an SLua script typed since its last check, checked alone against that check.
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

#include "Luau/Autocomplete.h"
#include "Luau/Location.h"
#include "Luau/Module.h"
#include "Luau/ParseResult.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Luau
{
    class Allocator;
}

struct ALLuauFrontend;

// What a question asked as a script is typed -- a completion, signature
// help -- is answered over: not the whole script checked again at each
// keystroke, but the statement being typed, from the first that differs
// from what was checked, parsed and checked alone in the scope the last
// check left there. Luau's fragment engine (Luau/FragmentAutocomplete.h).
//
// Each answer comes with whether to trust it: answered; nothing to answer,
// the place being in a comment or the question stopped; or ask of the
// whole script -- there is no check to patch yet, the last one is the
// text's already, something was typed since above the fragment, Luau would
// start the fragment at a place the text no longer has, the fragment would
// be long enough to cost more than the whole check, a module the script
// requires changed since, the fragment did not parse, Luau ran out of time
// on it or failed inside, or it found nothing and could not say what the
// place is.
//
// What Luau changes of the front end for a fragment is put back: the time
// out and the stop it marks on the module it patches, which would cost the
// next question a whole check, and the script's requires, which it forgets.
//
// Over the service's front end, which it asks and does not keep; one
// thread's, as the front end is. What it answers points into the text it
// parsed, which is kept until another is asked about.
class ALLuauFragment
{
public:
    explicit ALLuauFragment(ALLuauFrontend& front);
    ~ALLuauFragment();
    ALLuauFragment(const ALLuauFragment&)            = delete;
    ALLuauFragment& operator=(const ALLuauFragment&) = delete;

    enum class Outcome
    {
        Answered,
        Nothing,
        Whole
    };

    // What could go at `at`, as Luau's autocomplete says over the
    // fragment, `callback` answering inside a string as it does for a
    // whole script; `module` the fragment's, whose types the entries are.
    struct Completion
    {
        Outcome                  outcome = Outcome::Whole;
        Luau::AutocompleteResult found;
        Luau::ModulePtr          module;
    };
    Completion complete(std::string_view source, Luau::Position at, Luau::StringCompletionCallback callback);

    // The innermost call whose brackets hold `at`, found as the whole
    // script's is (findAstAncestryOfPosition), and the fragment's types,
    // its callee's among them: what signature help reads. The fragment
    // runs to the end of the call at the least, so that the call is whole
    // however far the caret is into it. Nothing where no call holds `at`.
    struct Typed
    {
        Outcome                  outcome = Outcome::Whole;
        Luau::ModulePtr          module;
        const Luau::AstExprCall* call = nullptr;
    };
    Typed typecheck(std::string_view source, Luau::Position at);

private:
    // The text synced, and parsed for the engine to set against the last
    // check; false where there is no fragment to check -- no check yet, the
    // last is the text's, or the text it was checked from is not known.
    bool ready(std::string_view source);
    // Luau failed inside: said once, and the question asked whole.
    void failedInside();
    // Where the fragment for `at` ends: `least` at the least, or the end
    // of the statement `at` is in as the text parses it, where that has no
    // block of its own. None where it would be too long to be cheaper than
    // checking the whole script: past a sixteenth of it or a couple of
    // hundred lines, and a few lines of any script.
    std::optional<Luau::Position> reach(Luau::Position at, Luau::Position least) const;
    size_t                        offsetOf(Luau::Position at) const;
    static constexpr unsigned FEW_LINES  = 16;
    static constexpr unsigned MANY_LINES = 200;

    ALLuauFrontend& mFront;
    // The text parsed, and the module whose names it was parsed with, kept
    // so that the names outlive it; parsed again for another text or
    // another check.
    std::string                      mText;
    Luau::ModulePtr                  mBase;
    std::unique_ptr<Luau::Allocator> mAllocator;
    Luau::ParseResult                mParse;
    // Where its lines start, and where what was typed since the base was
    // checked begins in it, where the base's text is known.
    std::vector<size_t>   mLines;
    std::optional<size_t> mTypedFrom;
    // Whether Luau failing inside has been said yet: once is enough.
    bool                             mToldIce = false;
};
