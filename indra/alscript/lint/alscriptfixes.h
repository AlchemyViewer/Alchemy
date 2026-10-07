/**
 * @file alscriptfixes.h
 * @brief What would put a problem right, made from what the analyzers say.
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
#include "alsourcemap.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The fixes the studio offers, made from a problem's words and the text it
// was said of, and the comments that say a warning is wanted. Text in,
// values out: the services call attach() as they make their problems, and
// a test may apply a fix and check the text again.
namespace ALScriptFixes
{
    // A text's lines, each without its break, and where each begins: found
    // once for all the fixes offered over one text -- a check's problems,
    // each offered its own -- rather than once for each.
    class Lines
    {
    public:
        explicit Lines(std::string_view text) : mText(text)
        {
            mStarts.push_back(0);
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '\n')
                {
                    mStarts.push_back(i + 1);
                }
            }
        }

        S32              count() const { return static_cast<S32>(mStarts.size()); }
        std::string_view text() const { return mText; }

        std::string_view line(S32 n) const
        {
            if (n < 0 || static_cast<size_t>(n) >= mStarts.size())
            {
                return {};
            }
            const size_t begin = mStarts[n];
            size_t       end   = static_cast<size_t>(n) + 1 < mStarts.size() ? mStarts[n + 1] - 1 : mText.size();
            if (end > begin && mText[end - 1] == '\r')
            {
                --end;
            }
            return mText.substr(begin, end - begin);
        }

        // Where a place is in the text, or nothing for a place past its
        // line's end or past the last line.
        std::optional<size_t> offsetOf(S32 line, S32 column) const
        {
            if (column < 0 || static_cast<size_t>(column) > this->line(line).size() || line < 0 || static_cast<size_t>(line) >= mStarts.size())
            {
                return std::nullopt;
            }
            return mStarts[line] + static_cast<size_t>(column);
        }
        // The place an offset is, the other way; false past the text.
        bool placeOf(size_t offset, S32& line, S32& column) const
        {
            if (offset > mText.size())
            {
                return false;
            }
            const auto after = std::upper_bound(mStarts.begin(), mStarts.end(), offset);
            line             = static_cast<S32>(after - mStarts.begin()) - 1;
            column           = static_cast<S32>(offset - mStarts[line]);
            return true;
        }

    private:
        std::string_view    mText;
        std::vector<size_t> mStarts;
    };

    // A fix's words, keyed as a problem's are: the English is what a skin
    // without the key shows, and what the tests read. What the services
    // title their own with.
    ALScriptFix titled(const char* key, const char* english, std::vector<std::string> args);

    // The event whose handler hears the answer to what a call asks for --
    // llListen's listen, llRequestPermissions' run_time_permissions -- by
    // the function's LSL name, or nothing.
    const char* eventAnswering(std::string_view function);

    // Each problem given the fixes its words and its place make plain --
    // a missing `;` put in, the name the analyzer suggests put in place of
    // the one it could not find, a deprecated call's replacement where the
    // definitions name one, an unused local marked unused -- in the places
    // of `text`, which is what the problems were said of.
    void attach(ALScriptProblems& problems, std::string_view text, bool lua);
    // The same over lines found once, passing over a problem wholly within
    // `passedOver` -- an include's lines, whose fixes nobody reads.
    void attach(ALScriptProblems& problems, const Lines& lines, bool lua, const std::vector<std::pair<S32, S32>>& passedOver);

    // How far apart two names are, in edits, case aside: a character put
    // in, taken out or changed, or two side by side swapped.
    size_t editDistance(std::string_view a, std::string_view b);
    // The names among `names` nearest `word`, each as near as the others:
    // the same in another case, else within an edit for a name of up to
    // five characters, two up to nine, and a third of a longer one's
    // length; none for a one-character name but the same in another case,
    // and none where nothing is near enough to be what was meant.
    std::vector<std::string> nearestNames(std::string_view word, const std::vector<std::string>& names);
    // The same among two lists, the second -- a language's builtins --
    // shared rather than copied into the first; none where null.
    std::vector<std::string> nearestAmong(std::string_view word, const std::vector<std::string>& names, const std::vector<std::string>* more);
    // The one name nearest `word`, where no other is as near; empty where
    // none is near enough, or several are.
    std::string nearest(std::string_view word, const std::vector<std::string>& names);
    // Whether `now` is sure enough to be what `word` meant for its change
    // to be the one preferred: the same in another case, or a guess at a
    // name longer than three characters. A guess at a shorter one is a
    // guess among too many.
    bool surelyMeant(std::string_view word, std::string_view now);
    // The fix that changes the name a problem is about, `was`, to `now`,
    // given to the problem where the name is found at its place in `text`:
    // what a service offers once it knows the names in scope there, which
    // attach() does not. Preferred only where asked.
    void offerName(ALScriptProblem& problem, std::string_view text, const std::string& was, const std::string& now, bool preferred = true);
    // A change for each of the names in scope nearest the one a problem
    // is about, preferred where there is one and it is surely meant.
    void offerNames(ALScriptProblem& problem, std::string_view text, const std::string& was, const std::vector<std::string>& names);
    void offerNames(ALScriptProblem& problem, const Lines& lines, const std::string& was, const std::vector<std::string>& names);
    // Among two lists of names, the second shared -- a language's builtins
    // -- rather than copied into the first for each name offered.
    void offerNames(ALScriptProblem& problem, const Lines& lines, const std::string& was, const std::vector<std::string>& names,
                    const std::vector<std::string>& more);
    // The fix that takes a declaration nothing uses out of `text`, given
    // where the service's tree says it stands: with the `;` after it and
    // the type word before its name, and the whole of its lines where
    // nothing else stands on them. Safe: what nobody uses changes nothing
    // by going, where the service has seen that what it is given does not.
    void offerRemoval(ALScriptProblem& problem, std::string_view text, S32 line, S32 column, S32 endLine, S32 endColumn, const std::string& name);
    // The same, titled as `fix` is, for what has no name of its own: code
    // that can never run, a statement that does nothing.
    void offerRemoval(ALScriptProblem& problem, std::string_view text, S32 line, S32 column, S32 endLine, S32 endColumn, ALScriptFix fix);
    // The same over lines found once, for many.
    void offerRemoval(ALScriptProblem& problem, const Lines& lines, S32 line, S32 column, S32 endLine, S32 endColumn, const std::string& name);
    void offerRemoval(ALScriptProblem& problem, const Lines& lines, S32 line, S32 column, S32 endLine, S32 endColumn, ALScriptFix fix);

    // What the optimizer did, offered as a change to the source where the
    // source says just what the optimizer read -- blanks aside -- and the
    // change is one expression or one thing removed: the notes of a run
    // over `text`, in its places.
    void attachOptimizer(ALScriptProblem& problem, std::string_view text);
    // The same over lines found once, for every note of a run.
    void attachOptimizer(ALScriptProblem& problem, const Lines& lines);

    // A problem's fixes taken through a map from the text they were made
    // over to the source, kept only where every edit lands, on one line,
    // in the script's own text as the map copied it -- not in an include,
    // nor in what a macro made.
    void mapThrough(const ALSourceMap& map, ALScriptProblem& problem);
    // The other way: a fix made over the source taken into the text a map
    // made of it -- to be weighed as the compiler would see it made, say.
    // Where every edit lands as mapThrough would bring it back: on one
    // line, on what the map copied from the script's own text; at the
    // start of the line a source line began; or over whole lines, the
    // script's own and one after another. False, and the fix as it was,
    // where one does not.
    bool intoExpansion(const ALSourceMap& map, ALScriptFix& fix);

    // The fix that gives a global a script does not know -- the problem's
    // one word -- what a module in reach gives: `local util =
    // require("util")` for the module itself, or `local greet =
    // require("util").greet` for a `field` of it, `module` being the name
    // a require finds it by. After the requires `text` opens with, else
    // after the comments it opens with, apart from the code after. Never
    // preferred nor safe here: which module is meant, and whether running
    // it is wanted, is the caller's to know.
    // An SLua script made --!strict: a --!nonstrict or --!nocheck at its
    // head changed to it, else it put first. Nothing where it is already.
    std::optional<ALScriptFix> strictFix(std::string_view slua);

    void offerRequire(ALScriptProblem& problem, std::string_view text, const std::string& module, bool field);
    void offerRequire(ALScriptProblem& problem, const Lines& lines, const std::string& module, bool field);
    // The same for LSL: `#include "include"` for a name an include
    // declares -- a function, a global, a macro -- after the directives
    // `text` opens with, else after its comments.
    void offerInclude(ALScriptProblem& problem, std::string_view text, const std::string& include);
    void offerInclude(ALScriptProblem& problem, const Lines& lines, const std::string& include);

    // A name for something a refactor makes in `text`: `base`, else
    // `base2`, `base3` and on -- one that stands nowhere in it as a word,
    // so that it can neither be taken already nor hide what is.
    std::string freshName(std::string_view text, std::string_view base);

    // `text` with a fix's edits made, or nothing where two of them overlap
    // or one lies outside the text.
    std::optional<std::string> apply(std::string_view text, const ALScriptFix& fix);

    // The name a lint may be suppressed by: a Luau lint's own name, an LSL
    // warning's key without its `LSL`, whatever level the scripter set it
    // at. Empty for what may not be suppressed: a parse or a type error,
    // which no comment makes right.
    std::string lintName(const ALScriptProblem& problem, bool lua);

    // Whether a comment says the problem is wanted, clang-tidy's way:
    // `NOLINT` on its line, or `NOLINTNEXTLINE` on the line before, each
    // either bare, for every warning, or with names -- `NOLINT(LocalUnused,
    // ShadowLocal)` -- for those alone. An LSL warning may be named by its
    // number as well. `line` is the problem's line, `before` the one above
    // it, or empty on the first.
    bool suppressed(const ALScriptProblem& problem, std::string_view line, std::string_view before, bool lua);

    // The fix that says so on the problem's line, `line` being its text:
    // the name added to a NOLINT the line has, else to the comment it ends
    // in, else in a comment of its own. Nothing for what cannot be
    // suppressed.
    std::optional<ALScriptFix> suppression(const ALScriptProblem& problem, std::string_view line, bool lua);
}
