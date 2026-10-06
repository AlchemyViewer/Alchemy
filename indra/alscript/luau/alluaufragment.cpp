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
    // whole check. And the script's requires, which it traces for the
    // fragment under the script's name and then forgets: a check that did
    // not parse the script again would find none of them.
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
        }
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

    // Where the word that begins at `at` ends, where one does: the
    // studio asks at the start of what is being typed, and the fragment
    // runs through it, as the whole text has it, rather than stopping
    // where Luau stops, at the position -- where a fragment holds no word,
    // nothing in it has the type the word is wanted to have.
    std::optional<Luau::Position> wordEnd(std::string_view source, Luau::Position at)
    {
        size_t offset = 0;
        for (unsigned line = 0; line < at.line; ++line)
        {
            offset = source.find('\n', offset);
            if (offset == std::string_view::npos)
            {
                return std::nullopt;
            }
            ++offset;
        }
        offset += at.column;
        unsigned length = 0;
        while (offset + length < source.size() &&
               (std::isalnum(static_cast<unsigned char>(source[offset + length])) || source[offset + length] == '_'))
        {
            ++length;
        }
        if (length == 0)
        {
            return std::nullopt;
        }
        return Luau::Position(at.line, at.column + length);
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
    }
    return mParse.root != nullptr;
}

bool ALLuauFragment::narrow(Luau::Position at) const
{
    // Where Luau will start the fragment: the statement `at` is in, or the
    // first of its block that differs from the last check's, which after an
    // edit elsewhere since that check can be far above. A line of fragment
    // costs ten times a line of a whole check and more -- the type of each
    // local it names cloned out of the check's, then solved apart -- and
    // more the longer it is: past a sixteenth of the script, or a couple of
    // hundred lines, the whole check is the cheaper.
    const Luau::Position from  = Luau::findAncestryForFragmentParse(mBase->root, at, mParse.root).fragmentSelectionRegion.begin;
    const unsigned       lines = mParse.root->location.end.line + 1;
    return at.line < from.line || at.line - from.line <= std::max(FEW_LINES, std::min(MANY_LINES, lines / 16));
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
    if (!ready(source) || !narrow(at))
    {
        return answer;
    }
    Waypoints                   waypoints;
    const Luau::FragmentContext context{ source, mParse, mFront.baseOptions(), wordEnd(source, at), &waypoints };
    Luau::FragmentAutocompleteStatusResult made{ Luau::FragmentAutocompleteStatus::Success, std::nullopt };
    {
        const Restored restored(*mFront.frontend, mFront.moduleName, *mBase);
        made = Luau::tryFragmentAutocomplete(*mFront.frontend, mFront.moduleName, at, context, std::move(callback));
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
    // did not parse.
    if (!made.result->incrementalModule)
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
    if (!narrow(at))
    {
        return answer;
    }
    Waypoints waypoints;
    std::pair<Luau::FragmentTypeCheckStatus, Luau::FragmentTypeCheckResult> made{ Luau::FragmentTypeCheckStatus::SkipAutocomplete, {} };
    {
        const Restored restored(*mFront.frontend, mFront.moduleName, *mBase);
        try
        {
            // To the end of the call, rather than Luau's own end at the
            // position: a call cut there has no closing bracket, and the
            // tree of what is cut is no longer the text's tree where the
            // two are set side by side to find the call.
            made = Luau::typecheckFragment(*mFront.frontend, mFront.moduleName, at, mFront.baseOptions(), source, call->location.end,
                                           mParse.root, &waypoints);
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
    if (made.first != Luau::FragmentTypeCheckStatus::Success || !made.second.incrementalModule)
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
