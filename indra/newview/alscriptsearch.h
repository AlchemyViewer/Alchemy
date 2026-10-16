/**
 * @file alscriptsearch.h
 * @brief What a search across Script Studio's scripts found, and what Replace All would make of it.
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

#include "alscriptstudiodoc.h"
#include "alscripttypes.h"
#include "altextsearch.h"
#include "lluuid.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

// A search for words across scripts: what was asked, which search it is,
// how many scripts are still to answer, what each script searched was
// found to hold and in which text, and the scripts typed in since, to be
// searched again. And Replace All's plan: what it does with each script
// found, and the edits it makes. Nothing here draws or asks anything of
// the viewer.
class ALScriptSearch
{
public:
    // A script searched, and its places.
    struct Found
    {
        ALScriptRef              ref;
        // The tab it was searched in, where it was open.
        std::string              doc;
        std::string              name;
        // What the row says it is in: the object and the script.
        std::string              where;
        // The version of the tab's text it was searched at; zero for a
        // text searched as the region has it.
        U32                      version = 0;
        std::vector<ALTextRange> places;
        // A script that was not open: the text it was searched in, which a
        // replace works out its replacements over; and whether it is a
        // notecard, or, with no item, a text file, which a replace leaves
        // alone.
        std::string              text;
        bool                     notecard = false;
        // Each place's line as listed, trimmed, and where the words start
        // in it.
        std::vector<std::string> lines;
        std::vector<S32>         at;
        // A file a script includes or requires, searched with it: its
        // identity (ALScriptPreprocessor's), which it is gone to by.
        std::string              file;
    };

    // --- a search ------------------------------------------------------------------

    // A search begun: every answer to an earlier one dropped from here on.
    void                       begin(const std::string& query, const ALTextSearchOptions& options);
    const std::string&         query() const { return mQuery; }
    const ALTextSearchOptions& options() const { return mOptions; }
    U32                        generation() const { return mGeneration; }

    // A script's text asked for, which answers later; and its answer:
    // false where it is an earlier search's, which is dropped.
    void asked() { ++mPending; }
    bool answered(U32 generation);
    S32  pending() const { return mPending; }
    // What the search could not look through, said with what it found: a
    // script whose text could not be read, and prims that did not say what
    // they hold.
    void notRead() { ++mUnread; }
    void notListed(S32 prims) { mUnlisted += prims; }
    S32  unread() const { return mUnread; }
    S32  unlisted() const { return mUnlisted; }

    // A script's text searched: its places kept where it has any, with
    // the text where `keep_text`, for a replace to work over; in its place
    // among the rest where it was searched before, whatever it holds now.
    // A new script's places, that script searched again, or nothing kept.
    enum class Kept : U8
    {
        Nothing,
        Added,
        Again
    };
    Kept search(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text, U32 version,
                const std::string& doc_id, bool keep_text = false, bool notecard = false, const std::string& file = std::string());
    // The same in two halves, for a text searched off the main thread:
    // where the words are in it, and each place's line, which asks
    // nothing of the search but its words; and what was found kept, as
    // search keeps it -- with the text it was found in, where given.
    struct Matched
    {
        std::vector<ALTextRange> places;
        std::vector<std::string> lines;
        std::vector<S32>         at;
        // Why the pattern did not read, where it did not.
        std::string              error;
    };
    static Matched match(const ALTextDocument& text, const std::string& query, const ALTextSearchOptions& options);
    Kept           keep(const ALScriptRef& ref, const std::string& name, const std::string& where, Matched matched, U32 version,
                        const std::string& doc_id, const std::string* text = nullptr, bool notecard = false, const std::string& file = std::string());

    // The texts of scripts not open read for a search, kept for the
    // session by what each is and which text it is: searched again -- for
    // other words, the case turned -- nothing saved since is fetched
    // again. No more than MOST bytes of them; past that, those used
    // longest ago go.
    class Sources
    {
    public:
        struct Source
        {
            std::shared_ptr<const std::string> text;
            std::string                        name;
            bool                               notecard = false;
        };
        static constexpr size_t MOST = 32 * 1024 * 1024;
        static Sources&         instance();
        // A script's text, where the one kept is of the asset it has now.
        const Source* find(const ALScriptRef& ref, const LLUUID& asset);
        void          keep(const ALScriptRef& ref, const LLUUID& asset, Source source);
        size_t        bytes() const { return mBytes; }
        void          clear();

    private:
        struct Kept
        {
            LLUUID asset;
            Source source;
            U64    used = 0;
        };
        boost::unordered_flat_map<ALScriptRef, Kept> mKept;
        size_t                                       mBytes = 0;
        U64                                          mClock = 0;
    };
    // The pattern did not read, and why: said until the next search.
    bool               badPattern() const { return mBadPattern; }
    const std::string& patternError() const { return mPatternError; }

    const std::vector<Found>& found() const { return mFound; }
    // What found nothing any more let go of; and every script's places
    // counted again.
    void dropEmpty();
    void recount();
    S32  hits() const { return mHits; }
    S32  files() const { return mFiles; }

    // The object searched, for a search of one; null otherwise. And every
    // object the search was over, which what is typed later is searched
    // again within.
    void          setObject(const LLUUID& root) { mObject = root; }
    const LLUUID& object() const { return mObject; }
    void          over(const LLUUID& root) { mRoots.push_back(root); }
    bool          isOver(const LLUUID& root) const;

    // A tab typed in since the search, searched again a moment after the
    // last keystroke; whether that moment has come -- not while scripts are
    // still to answer, which puts it off -- and which tabs.
    void                     typedIn(const std::string& id, F64 now);
    bool                     due(F64 now);
    std::vector<std::string> takeTyped();
    // A tab called something else from here on.
    void rekey(const std::string& from, const std::string& to);

    // --- Replace All ---------------------------------------------------------------

    // Where a script found is now, as the window finds it: closed, open in
    // a tab here, or open in another window; and that tab's facts.
    struct Now
    {
        enum class At : U8
        {
            Closed,
            Here,
            Elsewhere
        };
        At                    at         = At::Closed;
        // A text file in its tab: neither a script nor a notecard in the
        // world, which Replace All leaves alone.
        bool                  plainText  = false;
        bool                  loaded     = false;
        bool                  modifiable = false;
        const ALTextDocument* text       = nullptr;
    };
    // What Replace All does with it: nothing, a text file's places or none;
    // its places replaced in its tab, which reads as it did when it was
    // searched; the script opened with the change unsaved; or left alone,
    // and said so -- typed in since, not to be changed, a file on disk
    // closed since, which has no item to open it by, or a file included,
    // which is every script's that includes it.
    enum class Step : U8
    {
        Skip,
        Replace,
        Open,
        Leave
    };
    static Step step(const Found& one, const Now& now);
    // The edits, each a place and what goes there, over the text as it
    // stands; and as edits to make once the script is open, over the text
    // it was searched in.
    std::vector<std::pair<ALTextRange, std::string>> replacements(const Found& one, const ALTextDocument& text, const std::string& with) const;
    std::vector<ALScriptStudioDoc::PendingEdit>      pendingEdits(const Found& one, const std::string& with) const;

private:
    std::string              mQuery;
    ALTextSearchOptions      mOptions;
    U32                      mGeneration = 0;
    S32                      mPending    = 0;
    S32                      mUnread     = 0;
    S32                      mUnlisted   = 0;
    S32                      mHits       = 0;
    S32                      mFiles      = 0;
    bool                     mBadPattern = false;
    std::string              mPatternError;
    std::vector<Found>       mFound;
    LLUUID                   mObject;
    std::vector<LLUUID>      mRoots;
    F64                      mDue = 0.0;
    std::vector<std::string> mTyped;
};
