/**
 * @file alluaufrontend.h
 * @brief What the SLua analyzer keeps between questions: Luau's front end and all it is given.
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

#include "alluautaskpool.h"
#include "alscriptlintpass.h"

#include "llstl.h"

#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Luau
{
    struct FrontendCancellationToken;
}

// What the SLua analyzer keeps between questions: Luau's front end, the
// texts and configurations it reads them with, the scripts kept checked,
// the definitions and their documentation, and what stops a check. Every
// question ALLuauService answers is asked of one of these, which keeps
// each script checked at most once for its text however many questions
// come; the work that answers a question is the service's.
//
// Luau's headers come with this one, so only alscript's SLua sources
// include it: ALLuauService's own header names it and no more. Not
// thread-safe: it belongs to the thread its service belongs to.
struct ALLuauFrontend
{
    // The module a script is checked as where it is not named: a test's,
    // a bench's, a lookup through another object's script. A named one is
    // "script:" and its name, a few of them kept at once.
    static constexpr const char* SCRIPT_MODULE = "script";

    // The package the constants the grid's VM sets and the definitions
    // leave out are declared under, beside the definitions, so that the
    // symbol each is documented by begins with it. The checker knows them;
    // completion does not offer them, as no list of the grid's offers them.
    static constexpr const char* UNDOCUMENTED_PACKAGE = "@sl-slua-undocumented";
    static bool undocumented(const std::optional<std::string>& symbol)
    {
        return symbol && symbol->rfind(UNDOCUMENTED_PACKAGE, 0) == 0;
    }

    // The scripts kept, each served from memory by its module's name. What
    // one requires is already in it, put there by the preprocessor.
    struct ScriptResolver final : public Luau::FileResolver
    {
        boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> texts;
        // Which module a require in a module names: by the requiring
        // module's name and the name it says, joined by a unit separator.
        boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> leadsTo;

        std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* expr,
                                                      const Luau::TypeCheckLimits&) override;
        std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override;
    };

    // Each script's configuration: what its `.luaurc` said. A question
    // that wants every type strict reads autocomplete's module, which Luau
    // checks strict whatever this says.
    struct ModeResolver final : public Luau::ConfigResolver
    {
        // Each kept script's own, by its module's name; and what one with
        // none yet reads.
        boost::unordered_flat_map<std::string, Luau::Config, ll::string_hash, std::equal_to<>> configs;
        Luau::Config                                                                          fallback;

        const Luau::Config& getConfig(const Luau::ModuleName& name, const Luau::TypeCheckLimits&) const override;
    };

    // What takeChecked takes, as the check's hook fills it: by the script
    // whose check it was, each module once.
    mutable std::mutex checkedMutex;
    mutable boost::unordered_flat_map<std::string, std::vector<std::string>, ll::string_hash, std::equal_to<>> checkedNow;

    ScriptResolver                  files;
    ModeResolver                    configs;
    std::unique_ptr<Luau::Frontend> frontend;
    // The module questions are asked of now; and the named ones kept, the
    // one asked of last first. A tab's script is its own module, so that
    // moving between tabs finds each checked as it was left; a few are
    // kept, and the one asked of longest ago let go of past that.
    std::string                     moduleName = SCRIPT_MODULE;
    // The lines nobody reads the names, hints and fixes of (setPassedOver).
    std::vector<std::pair<S32, S32>> passedOver;
    // The studio's own lints on and fatal, as the last configuration said.
    uint64_t                        slLints      = ALScriptLintPass::defaults();
    uint64_t                        slFatalLints = 0;
    // The mode each kept script's configuration asked for, where it asked
    // for one; the mode it is checked in follows (checkedIn).
    boost::unordered_flat_map<std::string, std::optional<Luau::Mode>, ll::string_hash, std::equal_to<>> askedModes;

    // What a configuration asked for, as asked -- a scripter's choice, or a
    // .luaurc's -- and where it asked for none, the solver's own default:
    // nonstrict for the old one, as the grid compiles, and strict for the
    // new, whose nonstrict says only what is sure to fail as the script
    // runs, far less than the old one's.
    Luau::Mode checkedIn(std::optional<Luau::Mode> asked) const;
    // Every kept script's mode, and the one a script with no configuration
    // yet reads, again for the solver now in use.
    void remode();
    // The modules each kept script requires, by its module's name: what
    // lets a module go once no kept script requires it.
    boost::unordered_flat_map<std::string, std::vector<std::string>, ll::string_hash, std::equal_to<>> requiredBy;
    static std::string moduleOf(std::string_view key) { return "module:" + std::string(key); }
    std::vector<std::string>        kept;
    static constexpr size_t         KEPT = 4;
    bool                            definitions = false;
    // The solver the front end was built for, and the definitions it was
    // given, which a front end built for the other is given again.
    Luau::SolverMode                solver = Luau::SolverMode::Old;
    std::string                     definitionsSource;
    // How long a type check may take, 0 for as long as it takes; what
    // stops one early; and whether the last question was stopped.
    double                                            timeLimit = 0.0;
    std::shared_ptr<Luau::FrontendCancellationToken> stop;
    bool                                              wasStopped = false;

    struct Doc
    {
        std::string documentation;
        std::string link;
    };
    boost::unordered_flat_map<std::string, Doc, ll::string_hash, std::equal_to<>> docs;
    // What the docs were loaded from, by length and hash.
    std::pair<size_t, size_t> docsHash{ 0, 0 };

    // How many times a script has been type checked, for a test that
    // says a question asked again is not; and how many questions were
    // answered over a fragment instead (ALLuauFragment).
    size_t  checks    = 0;
    size_t  fragments = 0;

    // Whether a question asked as a script is typed -- a completion,
    // signature help -- is answered over the part of the text that changed
    // since its last check (ALLuauFragment), rather than after checking it
    // all again.
    bool    useFragments = false;

    // A check's options, held to the time limit and watching the stop.
    Luau::FrontendOptions limited() const;
    // The same for autocomplete's module, which Luau checks strict and
    // without lints.
    Luau::FrontendOptions autocompleteOptions() const;
    // What a fragment is checked against: autocomplete's module under the
    // old solver, the one module under the new; none before a first check.
    // Whether it is the text's, nothing changed since it was checked; and
    // the options its check had, which a fragment's follow.
    Luau::ModulePtr       base() const;
    bool                  baseCurrent() const;
    Luau::FrontendOptions baseOptions() const;
    // The text each kept script's base was checked from, where its check
    // finished: what a fragment tells the edits since by. None where a
    // check of it was stopped.
    boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<>> baseTexts;
    const std::string*    baseText() const;

    bool stopRequested() const;

    // The text the front end reads, and it told only when that changes.
    // Each of Luau's two modules -- the script's, checked in its own mode
    // with its lints, and autocomplete's, which Luau always checks strict
    // -- then stays the text's until it does, and is checked at most once
    // for it, whatever is asked in whatever order. A change of
    // configuration is told as it is made.
    void sync(std::string_view source);

    // A check that ran out of time answers what it found by then, and is
    // not kept as the text's: asked again, perhaps with longer, it runs
    // again.
    void timedOut(const Luau::ModulePtr& module);

    // A check stopped part way: Luau keeps none of what it found, and
    // neither module is the text's. One not made, its module kept from
    // before, was not stopped.
    bool stoppedIn(bool checked);

    // The script's own module, checked where it is not the text's yet:
    // what a check reports, and what a question reads where it will do.
    // Whether it was checked now, and so could have been stopped.
    bool checkScript(Luau::CheckResult* result = nullptr);

    // The script checked as Luau's check does, what it reports the same --
    // each module checked now, the script's lints -- but where it needs
    // several of its modules checked, each on a thread of the pool's as
    // soon as the modules it requires are: a first look at a script that
    // requires several large modules waits for the longest of them, not
    // for all of them one after another.
    Luau::CheckResult checkWithModules(const Luau::FrontendOptions& options);
    std::unique_ptr<ALLuauTaskPool> modulePool;

    // The modules the script's checks checked, as Luau checked each -- on
    // whichever thread it was -- for what is told of them beyond their
    // errors: their lints. Every check of the script's own module, a
    // question's as much as a check's, and one stopped part way: kept
    // until taken, so that the check that tells them tells each, however
    // the questions before it went. Taken, which empties the script's;
    // forgotten with a script let go of.
    std::vector<std::string> takeChecked();
    void                     forgetChecked(const std::string& name);

    // The module a question reads, checked where it is not the text's
    // yet; none where the check was stopped. Under the new solver there is
    // the one, in whatever mode the script is: its types are solved in
    // either, and the mode says only which mistakes are told. Under the
    // old, a nonstrict check leaves a local's type unknown, so the
    // script's own module serves only where the script is strict -- by its
    // configuration or its own --!strict -- and autocomplete's otherwise,
    // and always for a completion, which reads nothing else.
    Luau::ModulePtr queried(std::string_view source, bool completion = false);

    const Doc* docFor(const std::optional<std::string>& symbol) const;

    // A front end with Luau's own globals in it, ready for the definitions
    // or for a script. One is built for each set of definitions, because
    // the global type arena is frozen once they are in and a frozen arena
    // takes nothing more.
    // Autocomplete type-checks against a global scope of its own, so the
    // builtins and the definitions go into both.
    static std::unique_ptr<Luau::Frontend> plainFrontend(ScriptResolver& files, ModeResolver& configs, Luau::SolverMode solver);
};
