/**
 * @file alscriptsearchpane.cpp
 * @brief Script Studio's Search tab: the scripts open, or an object's, searched and replaced across.
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterscriptstudio.h"

#include "alcodeeditor.h"
#include "alpanelist.h"
#include "alscopebar.h"
#include "altextsearch.h"
#include "llbutton.h"
#include "lllineeditor.h"
#include "llnotificationsutil.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <algorithm>

namespace
{
    // How many places a find across scripts lists; the rest are counted
    // and not listed, since a common word in an object's scripts is
    // thousands of rows nobody reads.
    const S32 SEARCH_ROWS = 2000;
}

// --- find in files -------------------------------------------------------------------

// Said as a sentence rather than as a form:
//
//     Find `timer` as [text] [ignoring case] in [open scripts]
void ALFloaterScriptStudio::buildSearchBar()
{
    std::vector<ALScopeBar::Segment> said;

    ALScopeBar::Segment find;
    find.kind = ALScopeBar::Segment::Kind::Word;
    find.text = getString("SearchFind");
    said.push_back(find);

    ALScopeBar::Segment query;
    query.kind    = ALScopeBar::Segment::Kind::Field;
    query.name    = "query";
    query.text    = getString("SearchPlaceholder");
    query.toolTip = getString("SearchQueryTip");
    said.push_back(query);

    ALScopeBar::Segment as;
    as.kind = ALScopeBar::Segment::Kind::Word;
    as.text = getString("SearchAs");
    said.push_back(as);

    ALScopeBar::Segment how;
    how.kind    = ALScopeBar::Segment::Kind::Choice;
    how.name    = "how";
    how.toolTip = getString("SearchHowTip");
    how.choices = { { getString("SearchText"), "text" }, { getString("SearchWord"), "word" }, { getString("SearchPattern"), "pattern" } };
    said.push_back(how);

    ALScopeBar::Segment letters;
    letters.kind    = ALScopeBar::Segment::Kind::Choice;
    letters.name    = "case";
    letters.toolTip = getString("SearchCaseTip");
    letters.choices = { { getString("SearchAnyCase"), "any" }, { getString("SearchThisCase"), "exact" } };
    said.push_back(letters);

    ALScopeBar::Segment in;
    in.kind = ALScopeBar::Segment::Kind::Word;
    in.text = getString("SearchIn");
    said.push_back(in);

    ALScopeBar::Segment where;
    where.kind    = ALScopeBar::Segment::Kind::Choice;
    where.name    = "scope";
    where.toolTip = getString("SearchScopeTip");
    where.choices = { { getString("SearchOpen"), "open" }, { getString("SearchThisObject"), "object" }, { getString("SearchListed"), "listed" } };
    said.push_back(where);

    mSearchBar->setSentence(std::move(said));
}

void ALFloaterScriptStudio::findInFiles()
{
    showBottom("search_tab");
    if (LLLineEditor* field = mSearchBar->findChild<LLLineEditor>("query"))
    {
        // What is selected in the view in front is what is most likely
        // meant: a word read in the expansion is sought as much as one in
        // the source.
        if (Doc* doc = active())
        {
            const ALCodeEditor& shown     = *doc->shownText();
            const ALTextRange   selection = shown.selection();
            if (!selection.empty() && selection.begin.line == selection.end.line)
            {
                const ALTextRange ordered(std::min(selection.begin, selection.end), std::max(selection.begin, selection.end));
                mSearchBar->setValue("query", shown.document().text(ordered));
            }
        }
        field->setFocus(true);
        field->selectAll();
    }
}

// A dropdown moved: the same words asked about again. Typing waits for
// return, since a search over an object's contents fetches what it has
// not got.
void ALFloaterScriptStudio::onSearchChanged()
{
    const std::string query = mSearchBar->valueOf("query");
    if (!query.empty() && query == mSearchQuery)
    {
        search();
    }
}

void ALFloaterScriptStudio::search()
{
    ++mSearchGeneration;
    mSearchPending = 0;
    mSearchHits    = 0;
    mSearchFiles   = 0;
    mSearchQuery   = mSearchBar->valueOf("query");
    mSearchBadPattern = false;
    mSearchFound.clear();
    mSearchStale.clear();
    mSearchDue     = 0.0;
    mSearchRoot.setNull();
    mSearchRoots.clear();
    mSearchResults->deleteAllItems();
    if (mSearchQuery.empty())
    {
        mSearchCount->setText(LLStringUtil::null);
        return;
    }
    const std::string scope = mSearchBar->valueOf("scope");
    // The scripts open are searched as they stand, wherever they are; an
    // object's contents as the region has them, but for a script open
    // from it, which is searched as it stands too.
    auto searchOpen = [this](const Doc& doc) {
        if (!doc.loaded)
        {
            return;
        }
        searchDocument(doc.ref, doc.name, searchWhere(doc), doc.editor->document(), doc.editor->document().version(), doc.id);
    };
    if (scope == "open")
    {
        for (const std::unique_ptr<Doc>& doc : mDocs)
        {
            searchOpen(*doc);
        }
        searchSettled();
        return;
    }
    // This object: the active script's, else the one chosen in the
    // explorer; with neither, no object is guessed at, and the count says
    // what would give it one.
    LLUUID only;
    if (scope == "object")
    {
        if (Doc* doc = active(); doc && !doc->ref.inInventory())
        {
            if (LLViewerObject* object = gObjectList.findObject(doc->ref.object))
            {
                only = object->getRootEdit() ? object->getRootEdit()->getID() : object->getID();
            }
        }
        if (only.isNull())
        {
            const std::vector<ExplorerRow> rows = explorerChoice();
            if (!rows.empty())
            {
                only = rows.front().root;
            }
        }
        if (only.isNull())
        {
            mSearchCount->setText(getString("SearchNoObject"));
            mSearchCount->setToolTip(getString("SearchNoObjectTip"));
            return;
        }
        mSearchRoot = only;
    }
    const U32                 generation = mSearchGeneration;
    const LLHandle<LLFloater> handle     = getHandle();
    for (const ExplorerObject& object : mExplorerModel)
    {
        if (!object.present || (only.notNull() && object.root != only))
        {
            continue;
        }
        mSearchRoots.push_back(object.root);
        for (const ExplorerPrim& prim : object.prims)
        {
            for (const ALScriptWorkspace::Item& item : prim.items)
            {
                const ALScriptRef ref(prim.id, item.id);
                if (const size_t index = indexOf(ref); index != NONE)
                {
                    searchOpen(*mDocs[index]);
                    continue;
                }
                // Open in another window: searched as it stands there, and
                // what it held kept, for a replace to find it still holds.
                if (ALFloaterScriptStudio* holder = holderOf(ref, std::string()); holder && holder != this)
                {
                    const Doc& there = *holder->mDocs[holder->indexOf(ref)];
                    if (there.loaded)
                    {
                        searchDocument(ref, there.name, object.name, there.editor->document(), 0, std::string(), true, there.notecard);
                        continue;
                    }
                }
                ++mSearchPending;
                ALScriptWorkspace::instance().load(ref, [handle, generation, where = object.name](const ALScriptWorkspace::Loaded& loaded) {
                    if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
                    {
                        studio->searchLoaded(generation, where, loaded);
                    }
                });
            }
        }
    }
    searchSettled();
}

void ALFloaterScriptStudio::searchDocument(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text, U32 version,
                                           const std::string& doc_id, bool keep_text, bool notecard)
{
    ALTextSearchOptions options;
    options.caseSensitive = mSearchBar->valueOf("case") == "exact";
    options.wholeWord     = mSearchBar->valueOf("how") == "word";
    options.regex         = mSearchBar->valueOf("how") == "pattern";
    std::string                    error;
    const std::vector<ALTextRange> matches = ALTextSearch::matches(text, mSearchQuery, options, nullptr, &error);
    if (!error.empty())
    {
        // Said by the count, which says it until the next search.
        mSearchBadPattern = true;
        mSearchCount->setToolTip(error);
        return;
    }
    // The places kept, with the text they were found in, for a replace,
    // and each one's line as the row lists it.
    Found found;
    found.ref     = ref;
    found.doc     = doc_id;
    found.name    = name;
    found.where   = where.empty() ? name : where + ": " + name;
    found.version = version;
    found.places  = matches;
    found.notecard = notecard;
    if (keep_text && !matches.empty())
    {
        found.text = text.text();
    }
    for (const ALTextRange& match : matches)
    {
        const std::string& line  = text.line(match.begin.line);
        const size_t       first = line.find_first_not_of(" \t");
        const size_t       last  = line.find_last_not_of(" \t\r");
        found.lines.push_back(first == std::string::npos ? std::string() : line.substr(first, last - first + 1));
        found.at.push_back(first == std::string::npos ? -1 : match.begin.column - static_cast<S32>(first));
    }
    // A script searched again as it stands: in its place among the rest.
    for (size_t i = 0; i < mSearchFound.size(); ++i)
    {
        if ((!doc_id.empty() && mSearchFound[i].doc == doc_id) || (doc_id.empty() && !ref.isNull() && mSearchFound[i].ref == ref))
        {
            mSearchFound[i] = std::move(found);
            return;
        }
    }
    if (matches.empty())
    {
        return;
    }
    ++mSearchFiles;
    mSearchHits += static_cast<S32>(matches.size());
    mSearchFound.push_back(std::move(found));
    addSearchRows(mSearchFound.size() - 1);
}

void ALFloaterScriptStudio::addSearchRows(size_t index)
{
    const Found& found = mSearchFound[index];
    for (size_t i = 0; i < found.places.size(); ++i)
    {
        if (mSearchResults->getItemCount() >= SEARCH_ROWS)
        {
            // Past what a list is any use as: counted, not listed.
            return;
        }
        const ALTextRange& match = found.places[i];
        LLSD               value;
        value["found"] = static_cast<S32>(index);
        value["place"] = static_cast<S32>(i);
        LLSD row;
        row["value"]                = value;
        row["columns"][0]["column"] = "where";
        row["columns"][0]["value"]  = found.where;
        row["columns"][1]["column"] = "line";
        row["columns"][1]["value"]  = llformat("%d:%d", match.begin.line + 1, match.begin.column + 1);
        row["columns"][2]["column"] = "text";
        row["columns"][2]["value"]  = found.lines[i];
        LLScrollListItem* item      = mSearchResults->addElement(row);
        // The words found, lit in the line: as far as the line goes, for
        // a pattern that runs on past it.
        const S32 at = found.at[i];
        if (item && at >= 0 && at < static_cast<S32>(found.lines[i].size()))
        {
            const S32 length = match.end.line == match.begin.line ? match.end.column - match.begin.column : static_cast<S32>(found.lines[i].size()) - at;
            if (LLScrollListCell* text = item->getColumn(2))
            {
                text->highlightText(at, llmin(length, static_cast<S32>(found.lines[i].size()) - at));
            }
        }
    }
}

void ALFloaterScriptStudio::fillSearchResults()
{
    // The row chosen and the scroll kept, by where the row's place is.
    const S32 scrolled = mSearchResults->getScrollPos();
    // The row chosen, by whose place it is: the numbers a row carries are
    // where its script stood in the list, which the scripts that found
    // nothing any more, going, move along.
    std::string chosen_doc;
    ALScriptRef chosen_ref;
    S32         chosen_place = -1;
    if (LLScrollListItem* item = mSearchResults->getFirstSelected())
    {
        const size_t found = static_cast<size_t>(item->getValue()["found"].asInteger());
        if (found < mSearchFound.size())
        {
            chosen_doc   = mSearchFound[found].doc;
            chosen_ref   = mSearchFound[found].ref;
            chosen_place = item->getValue()["place"].asInteger();
        }
    }
    mSearchResults->deleteAllItems();
    // What found nothing any more goes; what did is listed again, the hits
    // counted afresh.
    mSearchFound.erase(std::remove_if(mSearchFound.begin(), mSearchFound.end(), [](const Found& one) { return one.places.empty(); }),
                       mSearchFound.end());
    S32 chosen_found = -1;
    for (size_t i = 0; i < mSearchFound.size() && chosen_place >= 0; ++i)
    {
        const Found& one = mSearchFound[i];
        if (!chosen_doc.empty() ? one.doc == chosen_doc : one.doc.empty() && one.ref == chosen_ref)
        {
            chosen_found = static_cast<S32>(i);
            break;
        }
    }
    mSearchHits  = 0;
    mSearchFiles = static_cast<S32>(mSearchFound.size());
    for (size_t i = 0; i < mSearchFound.size(); ++i)
    {
        mSearchHits += static_cast<S32>(mSearchFound[i].places.size());
        addSearchRows(i);
    }
    if (chosen_found >= 0)
    {
        // By the row's own numbers: a list compares a map it is given as
        // text, and every map reads as the same text.
        mSearchResults->updateSort();
        const std::vector<LLScrollListItem*> rows = mSearchResults->getAllData();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const LLSD& value = rows[i]->getValue();
            if (value["found"].asInteger() == chosen_found && value["place"].asInteger() == chosen_place)
            {
                mSearchResults->selectNthItem(static_cast<S32>(i));
                break;
            }
        }
    }
    mSearchResults->setScrollPos(scrolled);
    searchSettled();
}

void ALFloaterScriptStudio::researchOpen(Doc& doc)
{
    // Only a script this search is over: any open one, for a search of
    // the scripts open; one of the object searched, or of any listed.
    if (mSearchQuery.empty() || !doc.loaded)
    {
        return;
    }
    const std::string scope = mSearchBar->valueOf("scope");
    if (scope != "open")
    {
        if (doc.ref.inInventory() || doc.ref.isNull())
        {
            return;
        }
        // One of the objects the search was over: the one searched, or
        // those listed when it began -- not any open script's.
        LLViewerObject* object = gObjectList.findObject(doc.ref.object);
        const LLUUID    root   = object ? (object->getRootEdit() ? object->getRootEdit()->getID() : object->getID()) : LLUUID::null;
        if (root.isNull() || std::find(mSearchRoots.begin(), mSearchRoots.end(), root) == mSearchRoots.end())
        {
            return;
        }
    }
    if (std::find(mSearchStale.begin(), mSearchStale.end(), doc.id) == mSearchStale.end())
    {
        mSearchStale.push_back(doc.id);
    }
    constexpr F64 SEARCH_AGAIN = 0.6;
    mSearchDue                 = LLTimer::getTotalSeconds() + SEARCH_AGAIN;
}

void ALFloaterScriptStudio::pumpSearch()
{
    if (mSearchDue <= 0.0 || LLTimer::getTotalSeconds() < mSearchDue)
    {
        return;
    }
    if (mSearchPending > 0)
    {
        // Still fetching the rest: once they are in.
        mSearchDue = LLTimer::getTotalSeconds() + 0.5;
        return;
    }
    mSearchDue = 0.0;
    const std::vector<std::string> stale = std::move(mSearchStale);
    mSearchStale.clear();
    // Each script searched again, and its own rows told what it holds
    // now, where they are; the list made again only where a script holds
    // more places or fewer, or none any more.
    bool rebuild = false;
    for (const std::string& id : stale)
    {
        const size_t index = indexOf(id);
        if (index == NONE)
        {
            continue;
        }
        const Doc& doc = *mDocs[index];
        size_t     at  = NONE;
        size_t     had = 0;
        for (size_t i = 0; i < mSearchFound.size(); ++i)
        {
            if (mSearchFound[i].doc == doc.id)
            {
                at  = i;
                had = mSearchFound[i].places.size();
                break;
            }
        }
        searchDocument(doc.ref, doc.name, searchWhere(doc), doc.editor->document(), doc.editor->document().version(), doc.id);
        if (at != NONE && (mSearchFound[at].places.size() != had || !refreshSearchRows(at)))
        {
            rebuild = true;
        }
    }
    if (rebuild)
    {
        fillSearchResults();
        return;
    }
    mSearchHits  = 0;
    mSearchFiles = static_cast<S32>(mSearchFound.size());
    for (const Found& one : mSearchFound)
    {
        mSearchHits += static_cast<S32>(one.places.size());
    }
    searchSettled();
}

bool ALFloaterScriptStudio::refreshSearchRows(size_t index)
{
    if (index >= mSearchFound.size())
    {
        return false;
    }
    const Found& found = mSearchFound[index];
    for (LLScrollListItem* item : mSearchResults->getAllData())
    {
        const LLSD& value = item->getValue();
        if (static_cast<size_t>(value["found"].asInteger()) != index)
        {
            continue;
        }
        const size_t place = static_cast<size_t>(value["place"].asInteger());
        if (place >= found.places.size())
        {
            return false;
        }
        const ALTextRange& match = found.places[place];
        LLScrollListCell*  line  = item->getColumn(1);
        LLScrollListCell*  text  = item->getColumn(2);
        if (!line || !text)
        {
            return false;
        }
        line->setValue(llformat("%d:%d", match.begin.line + 1, match.begin.column + 1));
        text->setValue(found.lines[place]);
        const S32 at = found.at[place];
        const S32 length = at < 0 || at >= static_cast<S32>(found.lines[place].size())
                               ? 0
                               : match.end.line == match.begin.line ? match.end.column - match.begin.column : static_cast<S32>(found.lines[place].size()) - at;
        text->highlightText(llmax(0, at), llmin(length, static_cast<S32>(found.lines[place].size()) - llmax(0, at)));
    }
    return true;
}

void ALFloaterScriptStudio::searchSettled()
{
    // How many places in how many files, each counted in its own form.
    LLStringUtil::format_map_t args;
    args["[HITS]"]  = counted("Matches", mSearchHits);
    args["[FILES]"] = counted("Files", mSearchFiles);
    args["[SHOWN]"] = std::to_string(SEARCH_ROWS);
    if (mSearchBadPattern)
    {
        mSearchCount->setText(getString("SearchBadPattern"));
        return;
    }
    const std::string said = mSearchPending > 0       ? getString("SearchCounting", args)
                             : mSearchHits > SEARCH_ROWS ? getString("SearchCountCapped", args)
                             : mSearchHits > 0           ? getString("SearchCount", args)
                                                         : getString("SearchNone", args);
    mSearchCount->setText(said);
    // The whole of it on the tip, where the slot cuts it; and which object,
    // for a search of one -- the rows say it too, but a search that found
    // nothing has no rows to say it.
    std::string tip = said + "\n" + getString("SearchCountTip");
    if (mSearchRoot.notNull())
    {
        std::string object;
        for (const ExplorerObject& one : mExplorerModel)
        {
            if (one.root == mSearchRoot)
            {
                object = one.name;
            }
        }
        LLStringUtil::format_map_t with = args;
        with["[OBJECT]"]                = object;
        tip                             = getString("SearchObjectTip", with);
    }
    mSearchCount->setToolTip(tip);
}

void ALFloaterScriptStudio::askReplaceAll()
{
    // What would change, said before anything does: every place found, in
    // every script found that may be changed.
    S32 places = 0, scripts = 0;
    for (const Found& one : mSearchFound)
    {
        if (replaceable(one))
        {
            places += static_cast<S32>(one.places.size());
            ++scripts;
        }
    }
    if (mSearchQuery.empty() || places == 0)
    {
        setStatus(getString("SearchReplaceNothing"), true);
        return;
    }
    LLSD args;
    args["PLACES"]                   = counted("Places", places);
    args["SCRIPTS"]                  = counted("Scripts", scripts);
    args["WITH"]                     = mSearchReplacement ? mSearchReplacement->getText() : std::string();
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioReplaceAll", args, LLSD(), [handle](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        if (studio && LLNotificationsUtil::getSelectedOption(notification, response) == 0)
        {
            studio->replaceAllFound();
        }
    });
}

size_t ALFloaterScriptStudio::foundIndex(const Found& one) const
{
    // By the tab it was searched in, else by its item: closed and opened
    // again since, it is the same script in another tab.
    size_t index = !one.doc.empty() ? indexOf(one.doc) : NONE;
    if (index == NONE && !one.ref.isNull())
    {
        index = indexOf(one.ref);
    }
    return index;
}

bool ALFloaterScriptStudio::replaceable(const Found& one) const
{
    // Places found, in a script -- a notecard is left as it is -- that may
    // be changed where it is open.
    if (one.places.empty() || one.notecard)
    {
        return false;
    }
    const size_t index = foundIndex(one);
    return index == NONE || (!mDocs[index]->notecard && mDocs[index]->modifiable);
}

void ALFloaterScriptStudio::replaceAllFound()
{
    if (mSearchFound.empty() || mSearchQuery.empty())
    {
        setStatus(getString("SearchReplaceNothing"), true);
        return;
    }
    ALTextSearchOptions options;
    options.caseSensitive = mSearchBar->valueOf("case") == "exact";
    options.wholeWord     = mSearchBar->valueOf("how") == "word";
    options.regex         = mSearchBar->valueOf("how") == "pattern";
    const std::string with = mSearchReplacement ? mSearchReplacement->getText() : std::string();

    // The scripts as they were found, every place in each replaced as one
    // step: in a tab where the script is open and reads as it did when it
    // was searched; in a tab opened for it, once its text is in, where it
    // was not open, the change left unsaved. One that has been typed in
    // since the search is left alone, and said so.
    const std::vector<Found> found = mSearchFound;
    S32                      places = 0, scripts = 0, opened = 0, left = 0;
    for (const Found& one : found)
    {
        if (one.places.empty() || one.notecard)
        {
            continue;
        }
        size_t index = foundIndex(one);
        // Open in another window: changed there, as a tab here would be,
        // where it reads as it did when it was searched.
        if (ALFloaterScriptStudio* holder = index == NONE && !one.ref.isNull() ? holderOf(one.ref, std::string()) : nullptr; holder && holder != this)
        {
            Doc& there = *holder->mDocs[holder->indexOf(one.ref)];
            if (there.notecard)
            {
                continue;
            }
            if (!there.loaded || !there.modifiable || one.text.empty() || there.editor->text() != one.text)
            {
                ++left;
                continue;
            }
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (const ALTextRange& place : one.places)
            {
                edits.emplace_back(place, ALTextSearch::replacement(there.editor->document(), place, mSearchQuery, options, with));
            }
            if (there.editor->replaceAll(std::move(edits)))
            {
                there.editor->undoJournal().label("replace");
                places += static_cast<S32>(one.places.size());
                ++scripts;
            }
            continue;
        }
        if (index == NONE && one.ref.isNull())
        {
            // A file on disk, closed since it was searched: it has no item
            // to open it by.
            ++left;
            continue;
        }
        if (index != NONE)
        {
            Doc& doc = *mDocs[index];
            if (doc.notecard)
            {
                continue;
            }
            if (!doc.modifiable)
            {
                // May be read and not changed: said among what was left.
                ++left;
                continue;
            }
            // Searched as it stood in its tab, by the version it was at;
            // searched as the region had it and opened since, by its text.
            const bool same = doc.loaded && (one.version != 0 ? doc.editor->document().version() == one.version : doc.editor->text() == one.text);
            if (!same)
            {
                ++left;
                continue;
            }
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (const ALTextRange& place : one.places)
            {
                edits.emplace_back(place, ALTextSearch::replacement(doc.editor->document(), place, mSearchQuery, options, with));
            }
            if (doc.editor->replaceAll(std::move(edits)))
            {
                doc.editor->undoJournal().label("replace");
                places += static_cast<S32>(one.places.size());
                ++scripts;
            }
            continue;
        }
        // Not open: the replacements worked out over the text it was
        // searched in, and made once it has loaded where each place still
        // reads as it did.
        if (one.text.empty())
        {
            ++left;
            continue;
        }
        const ALTextDocument     searched(one.text);
        std::vector<Doc::PendingEdit> edits;
        for (const ALTextRange& place : one.places)
        {
            Doc::PendingEdit edit;
            edit.span.line      = place.begin.line;
            edit.span.column    = place.begin.column;
            edit.span.endLine   = place.end.line;
            edit.span.endColumn = place.end.column;
            edit.was            = searched.text(place);
            edit.now            = ALTextSearch::replacement(searched, place, mSearchQuery, options, with);
            edit.replace        = true;
            edits.push_back(std::move(edit));
        }
        openScript(one.ref, one.name);
        index = indexOf(one.ref);
        if (index == NONE)
        {
            ++left;
            continue;
        }
        Doc& doc = *mDocs[index];
        doc.pendingEdits.insert(doc.pendingEdits.end(), edits.begin(), edits.end());
        applyPendingEdits(doc);
        places += static_cast<S32>(one.places.size());
        ++scripts;
        ++opened;
    }
    if (places == 0 && left == 0)
    {
        setStatus(getString("SearchReplaceNothing"), true);
        return;
    }
    // What was done, a clause for each thing there is to say.
    LLStringUtil::format_map_t args;
    args["[PLACES]"]  = counted("Places", places);
    args["[SCRIPTS]"] = counted("Scripts", scripts);
    std::string said  = getString("SearchReplaced", args);
    if (opened > 0)
    {
        args["[SCRIPTS]"] = counted("Scripts", opened);
        said += "; " + getString("SearchReplacedOpened", args);
    }
    if (left > 0)
    {
        args["[SCRIPTS]"] = counted("Scripts", left);
        said += "; " + getString("SearchReplacedLeft", args);
    }
    report(said + ".", left > 0);
    // Whatever stood before, the places have moved: looked for again.
    search();
}

void ALFloaterScriptStudio::onSearchResult(bool to_editor)
{
    const LLScrollListItem* item = mSearchResults->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD&  value = item->getValue();
    const size_t found = static_cast<size_t>(value["found"].asInteger());
    const size_t place = static_cast<size_t>(value["place"].asInteger());
    if (found >= mSearchFound.size() || place >= mSearchFound[found].places.size())
    {
        return;
    }
    const Found&      one   = mSearchFound[found];
    const ALTextRange match = one.places[place];
    const bool gone = !one.doc.empty() && indexOf(one.doc) == NONE;
    if (gone && one.ref.isNull())
    {
        // A file on disk, closed since it was searched: its tab is how it
        // was gone to, and it has no item to open it by.
        return;
    }
    // Open in another window: gone to there when asked, and not while the
    // list is walked, which would take the keyboard from the list.
    if (!to_editor && one.doc.empty() && !one.ref.isNull() && indexOf(one.ref) == NONE)
    {
        if (ALFloaterScriptStudio* holder = holderOf(one.ref, std::string()); holder && holder != this)
        {
            return;
        }
    }
    if (!to_editor && (one.doc.empty() || gone) && deferOpen(mSearchResults, ALScriptPreprocessor::pathOf(one.ref)))
    {
        return;
    }
    noteJump(!to_editor);
    ++mHoldPanes;
    // A script open when it was searched -- a file on disk among them,
    // which has no item to open it by -- is gone to in its tab.
    const size_t open = one.doc.empty() ? NONE : indexOf(one.doc);
    if (open != NONE)
    {
        if (open != mActive)
        {
            activate(open);
        }
        sourceInFront(*mDocs[open]).goTo(match);
    }
    else
    {
        const S32 length = match.end.line == match.begin.line ? match.end.column - match.begin.column : 0;
        goToPlace(one.ref, one.name, match.begin.line, match.begin.column, length);
    }
    --mHoldPanes;
    revealed(mSearchResults, to_editor);
}
