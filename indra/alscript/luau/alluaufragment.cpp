/**
 * @file alluaufragment.cpp
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

#include "linden_common.h"

#include "alluaufragment.h"

#include "alluaufrontend.h"
#include "alscriptlexicon.h"

#include "Luau/Allocator.h"
#include "Luau/Ast.h"
#include "Luau/AstQuery.h"
#include "Luau/Cancellation.h"
#include "Luau/Error.h"
#include "Luau/FragmentAutocomplete.h"
#include "Luau/Parser.h"
#include "Luau/RequireTracer.h"

#include <algorithm>
#include <cctype>

namespace
{
    // Luau's waypoints in a fragment's work, as moments on the profiler's
    // timeline inside the question's zone.
    class Waypoints final : public Luau::IFragmentAutocompleteReporter
    {
    public:
        void reportWaypoint(Luau::FragmentAutocompleteWaypoint waypoint) override
        {
            using Waypoint = Luau::FragmentAutocompleteWaypoint;
            switch (waypoint)
            {
                case Waypoint::ParseFragmentEnd:         LL_PROFILE_MESSAGE("fragment: parsed"); break;
                case Waypoint::CloneModuleStart:         LL_PROFILE_MESSAGE("fragment: cloning the module"); break;
                case Waypoint::CloneModuleEnd:           LL_PROFILE_MESSAGE("fragment: module cloned"); break;
                case Waypoint::DfgBuildEnd:              LL_PROFILE_MESSAGE("fragment: data flow built"); break;
                case Waypoint::CloneAndSquashScopeStart: LL_PROFILE_MESSAGE("fragment: cloning the scope"); break;
                case Waypoint::CloneAndSquashScopeEnd:   LL_PROFILE_MESSAGE("fragment: scope cloned"); break;
                case Waypoint::ConstraintSolverStart:    LL_PROFILE_MESSAGE("fragment: solving"); break;
                case Waypoint::ConstraintSolverEnd:      LL_PROFILE_MESSAGE("fragment: solved"); break;
                case Waypoint::TypecheckFragmentEnd:     LL_PROFILE_MESSAGE("fragment: checked"); break;
                case Waypoint::AutocompleteEnd:          LL_PROFILE_MESSAGE("fragment: completed"); break;
                default:                                 break;
            }
        }
        void reportFragmentString(std::string_view) override {}
    };

    // What Luau changes of the front end for a fragment, put back as it was
    // when this goes. The time out and the stop it marks on the module it
    // patches (typecheckFragment_), which would cost the next question a
    // whole check: cleared while the fragment is checked, so that what is
    // marked then is the fragment's own. And the script's requires, which
    // it traces for the fragment under the script's name and then forgets:
    // a check that did not parse the script again would find none of them.
    class Restored
    {
    public:
        Restored(Luau::Frontend& frontend, const std::string& name, Luau::Module& base)
        :   mFrontend(frontend)
        ,   mName(name)
        ,   mBase(base)
        ,   mTimeout(base.timeout)
        ,   mCancelled(base.cancelled)
        {
            const auto traced = frontend.requireTrace.find(name);
            if (traced != frontend.requireTrace.end())
            {
                mTrace = traced->second;
            }
            mBase.timeout   = false;
            mBase.cancelled = false;
        }
        // Whether the fragment ran out of time: Luau's solver stops part
        // way, marks it on the module and answers what it found by then,
        // which is no answer to trust.
        bool timedOut() const { return mBase.timeout; }
        ~Restored()
        {
            mBase.timeout   = mTimeout;
            mBase.cancelled = mCancelled;
            if (mTrace)
            {
                mFrontend.requireTrace[mName] = std::move(*mTrace);
            }
            else
            {
                mFrontend.requireTrace.erase(mName);
            }
        }
        Restored(const Restored&)            = delete;
        Restored& operator=(const Restored&) = delete;

    private:
        Luau::Frontend&                         mFrontend;
        const std::string&                      mName;
        Luau::Module&                           mBase;
        bool                                    mTimeout;
        bool                                    mCancelled;
        std::optional<Luau::RequireTraceResult> mTrace;
    };

    // Where the word that begins at `at`, `offset` into `text`, ends: `at`
    // where none does. The studio asks at the start of what is being typed,
    // and the fragment runs through it, as the text has it, rather than
    // stopping at the position as Luau's does -- where a fragment holds no
    // word, nothing in it has the type the word is wanted to have.
    Luau::Position wordEnd(std::string_view text, size_t offset, Luau::Position at)
    {
        unsigned length = 0;
        while (offset + length < text.size() && ALScriptLexicon::isNameByte(text[offset + length]))
        {
            ++length;
        }
        return Luau::Position(at.line, at.column + length);
    }

    // A statement with no block of its own, whose whole a fragment may
    // run to: one that holds a block starts its fragment part way in, in
    // its head, and its end would close what the fragment never opened.
    bool simple(const Luau::AstStat* statement)
    {
        return statement->is<Luau::AstStatExpr>() || statement->is<Luau::AstStatLocal>() || statement->is<Luau::AstStatAssign>() ||
               statement->is<Luau::AstStatCompoundAssign>() || statement->is<Luau::AstStatReturn>();
    }
}

ALLuauFragment::ALLuauFragment(ALLuauFrontend& front)
:   mFront(front)
{
    mParse.root = nullptr;
}

ALLuauFragment::~ALLuauFragment() = default;

bool ALLuauFragment::ready(std::string_view source)
{
    mFront.wasStopped = false;
    mFront.sync(source);
    if (!mFront.useFragments || mFront.baseCurrent())
    {
        return false;
    }
    Luau::ModulePtr base = mFront.base();
    if (!base || !base->root || !base->names)
    {
        return false;
    }
    // The text the base was checked from not known -- its check stopped,
    // say -- and nothing tells what was typed since, however much of this
    // text was parsed before: no fragment is set against it.
    if (!mFront.baseText())
    {
        return false;
    }
    // A module the script requires changed since: the whole script, whose
    // check checks it again. Luau's own test sees it only once it has been.
    if (const auto required = mFront.requiredBy.find(mFront.moduleName); required != mFront.requiredBy.end())
    {
        const bool forAutocomplete = mFront.solver == Luau::SolverMode::Old;
        for (const std::string& module : required->second)
        {
            if (mFront.frontend->isDirty(module, forAutocomplete))
            {
                return false;
            }
        }
    }
    if (base != mBase || source != mText)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_SCRIPTDEV("fragment: the text parsed");
        // Not the front end's parse, which would put the text's tree in
        // place of the one the last check was made from, and which a
        // hover reads beside that check. The names are the check's, as
        // the fragment's are.
        Luau::ParseOptions options = mFront.configs.getConfig(mFront.moduleName, {}).parseOptions;
        options.captureComments    = true;
        mParse.root                = nullptr;
        mAllocator                 = std::make_unique<Luau::Allocator>();
        mParse                     = Luau::Parser::parse(source.data(), source.size(), *base->names, *mAllocator, options);
        mText.assign(source);
        mBase = std::move(base);
        mLines.assign(1, 0);
        for (size_t i = 0; i < source.size(); ++i)
        {
            if (source[i] == '\n')
            {
                mLines.push_back(i + 1);
            }
        }
        // Where what was typed since the base was checked begins: the first
        // byte that differs, but for spaces. Nothing known where the base's
        // text is not.
        mTypedFrom.reset();
        if (const std::string* before = mFront.baseText())
        {
            const size_t most = std::min(before->size(), source.size());
            size_t       head = 0;
            while (head < most && (*before)[head] == source[head])
            {
                ++head;
            }
            while (head < source.size() && std::isspace(static_cast<unsigned char>(source[head])))
            {
                ++head;
            }
            mTypedFrom = head;
        }
    }
    return mParse.root != nullptr;
}

size_t ALLuauFragment::offsetOf(Luau::Position at) const
{
    return at.line < mLines.size() ? std::min(mLines[at.line] + at.column, mText.size()) : mText.size();
}

std::optional<Luau::Position> ALLuauFragment::reach(Luau::Position at, Luau::Position least) const
{
    // Where Luau will start the fragment: the statement `at` is in, or the
    // first of its block that differs from the last check's, which after an
    // edit elsewhere since that check can be far above.
    const Luau::FragmentAutocompleteAncestryResult found = Luau::findAncestryForFragmentParse(mBase->root, at, mParse.root);
    const Luau::Position                           from  = found.fragmentSelectionRegion.begin;
    // Luau finds that start in the text by counting lines and columns to
    // it (getDocumentOffsets), and where the text has no such place -- the
    // statement began further along a line since emptied, a line opened
    // above it -- starts at the text's first byte instead: the fragment is
    // all that is above, read as though it began at `from`, and nothing in
    // it stands where it is.
    const size_t past = from.line + 1 < mLines.size() ? mLines[from.line + 1] : mText.size();
    if (from.line >= mLines.size() || mLines[from.line] + from.column >= past)
    {
        return std::nullopt;
    }
    // The statement `at` is in, where one holds it.
    const Luau::AstStat* statement = found.nearestStatement && found.nearestStatement->location.containsClosed(at) ? found.nearestStatement : nullptr;
    // Where it ends: the end of that statement as the text parses it,
    // where it is one with no block of its own. A line typed part way
    // leaves the text's parse carrying its statement on into the lines
    // after -- `ll.` then `print(x)` below it reads `ll.print(x)` -- and a
    // fragment that stops sooner is another tree, set beside the text's
    // to find what is at `at`, and nothing is found.
    Luau::Position end = least;
    if (statement && simple(statement))
    {
        // Luau's start is where the last check's first differing statement
        // began, read in the text as it is now. Where the line typed has
        // taken in the statement after it, that is part way into the
        // statement, or past the position: no fragment holds what is there.
        if (statement->location.begin < from)
        {
            return std::nullopt;
        }
        end = std::max(end, statement->location.end);
    }
    if (at < from)
    {
        return std::nullopt;
    }
    // Nothing typed since the last check above the statement the fragment
    // is in, or the fragment is checked against what is no longer so: an
    // edit made above -- a function's parameters, an include's text -- is
    // no part of the scope it is checked in. Luau sets beside the last
    // check only the block the position is in.
    const Luau::Position first = statement ? std::min(from, statement->location.begin) : from;
    if (!mTypedFrom || offsetOf(first) > *mTypedFrom)
    {
        return std::nullopt;
    }
    // And the end of the outermost expression around `at`, short of a
    // function's body: in a block's head -- an `if`'s condition, say --
    // what follows the position decides as much, a call's own brackets
    // already there among it.
    const std::vector<Luau::AstNode*> ancestry  = Luau::findAncestryAtPositionForAutocomplete(mParse.root, at);
    const Luau::AstExpr*              outermost = nullptr;
    for (auto it = ancestry.rbegin(); it != ancestry.rend() && (*it)->asExpr() && !(*it)->is<Luau::AstExprFunction>(); ++it)
    {
        outermost = (*it)->asExpr();
    }
    if (outermost)
    {
        // Luau starts some heads' fragments at the position -- a `while`'s
        // condition -- which holds part of the expression and not the call
        // it is in.
        if (outermost->location.begin < from)
        {
            return std::nullopt;
        }
        end = std::max(end, outermost->location.end);
    }
    // A line of fragment costs ten times a line of a whole check and more
    // -- the type of each local it names cloned out of the check's, then
    // solved apart -- and more the longer it is: past a sixteenth of the
    // script, or a couple of hundred lines, the whole check is the cheaper.
    const unsigned lines = mParse.root->location.end.line + 1;
    if (end.line > from.line && end.line - from.line > std::max(FEW_LINES, std::min(MANY_LINES, lines / 16)))
    {
        return std::nullopt;
    }
    return end;
}

void ALLuauFragment::failedInside()
{
    if (!mToldIce)
    {
        mToldIce = true;
        LL_WARNS("ScriptAnalysis") << "Luau failed inside a fragment; the script is checked whole instead" << LL_ENDL;
    }
}

ALLuauFragment::Completion ALLuauFragment::complete(std::string_view source, Luau::Position at, Luau::StringCompletionCallback callback)
{
    Completion answer;
    if (!ready(source))
    {
        return answer;
    }
    const std::optional<Luau::Position> end = reach(at, wordEnd(mText, offsetOf(at), at));
    if (!end)
    {
        return answer;
    }
    Waypoints                   waypoints;
    const Luau::FragmentContext context{ source, mParse, mFront.baseOptions(), *end, &waypoints };
    Luau::FragmentAutocompleteStatusResult made{ Luau::FragmentAutocompleteStatus::Success, std::nullopt };
    bool                                   timedOut = false;
    {
        const Restored restored(*mFront.frontend, mFront.moduleName, *mBase);
        made     = Luau::tryFragmentAutocomplete(*mFront.frontend, mFront.moduleName, at, context, std::move(callback));
        timedOut = restored.timedOut();
    }
    // Stopped: nothing, as a check stopped answers nothing. Luau says a
    // stop outside its solver as a failure inside, its error being one.
    if (mFront.stopRequested())
    {
        mFront.wasStopped = true;
        answer.outcome    = Outcome::Nothing;
        return answer;
    }
    if (made.status == Luau::FragmentAutocompleteStatus::InternalIce)
    {
        failedInside();
        return answer;
    }
    // In a comment, where nothing is offered.
    if (!made.result)
    {
        answer.outcome = Outcome::Nothing;
        return answer;
    }
    // Luau declined: a module the script requires changed, or the fragment
    // did not parse. Or it ran out of time, part way: the whole script, as
    // a check that ran out of time is asked again.
    if (!made.result->incrementalModule || timedOut)
    {
        return answer;
    }
    // Nothing found and nothing said of the place after a `.`: Luau found
    // no type for what is indexed, as where the fragment was read from
    // other than where it stands in the text, which an answer of nothing
    // would hide. The whole script answers. Not where nothing is what
    // there is -- a name being bound, a number -- which costs a whole
    // check at every key for the same nothing.
    const Luau::AutocompleteResult& found = made.result->acResults;
    if (found.entryMap.empty() && found.context == Luau::AutocompleteContext::Unknown && !found.ancestry.empty() &&
        found.ancestry.back()->is<Luau::AstExprIndexName>())
    {
        return answer;
    }
    ++mFront.fragments;
    answer.outcome = Outcome::Answered;
    answer.found   = std::move(made.result->acResults);
    answer.module  = std::move(made.result->incrementalModule);
    return answer;
}

ALLuauFragment::Typed ALLuauFragment::typecheck(std::string_view source, Luau::Position at)
{
    Typed answer;
    if (!ready(source))
    {
        return answer;
    }
    // The call there in the text as it is, found as the whole script's
    // would be; none, and there is nothing to check.
    const Luau::AstExprCall*          call     = nullptr;
    const std::vector<Luau::AstNode*> ancestry = Luau::findAstAncestryOfPosition(mParse.root, at);
    for (auto it = ancestry.rbegin(); it != ancestry.rend() && !call; ++it)
    {
        const Luau::AstExprCall* candidate = (*it)->as<Luau::AstExprCall>();
        call                               = candidate && candidate->argLocation.containsClosed(at) ? candidate : nullptr;
    }
    if (!call)
    {
        answer.outcome = Outcome::Nothing;
        return answer;
    }
    const std::optional<Luau::Position> end = reach(at, call->location.end);
    if (!end)
    {
        return answer;
    }
    Waypoints waypoints;
    std::pair<Luau::FragmentTypeCheckStatus, Luau::FragmentTypeCheckResult> made{ Luau::FragmentTypeCheckStatus::SkipAutocomplete, {} };
    bool timedOut = false;
    {
        const Restored restored(*mFront.frontend, mFront.moduleName, *mBase);
        try
        {
            // To the end of the call at the least, rather than Luau's own
            // end at the position: a call cut there has no closing bracket,
            // and the tree of what is cut is no longer the text's tree where
            // the two are set side by side to find the call.
            made = Luau::typecheckFragment(*mFront.frontend, mFront.moduleName, at, mFront.baseOptions(), source, *end, mParse.root,
                                           &waypoints);
            timedOut = restored.timedOut();
        }
        catch (const Luau::UserCancelError&)
        {
        }
        catch (const Luau::TimeLimitError&)
        {
            return answer;
        }
        catch (const Luau::InternalCompilerError&)
        {
            failedInside();
            return answer;
        }
    }
    if (mFront.stopRequested())
    {
        mFront.wasStopped = true;
        answer.outcome    = Outcome::Nothing;
        return answer;
    }
    // Declined, or out of time part way, its types half solved.
    if (made.first != Luau::FragmentTypeCheckStatus::Success || !made.second.incrementalModule || timedOut)
    {
        return answer;
    }
    // The fragment's own node for the call, where its callee was typed:
    // the same place in the same text. Not there, and the whole script
    // answers.
    for (const Luau::AstNode* node : made.second.ancestry)
    {
        const Luau::AstExprCall* fragment = node->as<Luau::AstExprCall>();
        if (fragment && fragment != call && fragment->location == call->location &&
            made.second.incrementalModule->astTypes.find(fragment->func))
        {
            ++mFront.fragments;
            answer.outcome = Outcome::Answered;
            answer.module  = std::move(made.second.incrementalModule);
            answer.call    = fragment;
            break;
        }
    }
    return answer;
}
