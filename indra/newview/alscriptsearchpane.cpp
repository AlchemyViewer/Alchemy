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

#include "alscriptsearchpane.h"

#include "alcodeeditor.h"
#include "alpanefolds.h"
#include "alpanelist.h"
#include "alscopebar.h"
#include "alscriptmessages.h"
#include "alscriptstudioservices.h"
#include "llbutton.h"
#include "llfloater.h"
#include "llfontgl.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"
#include "lltimer.h"
#include "lluictrlfactory.h"

#include <algorithm>

namespace
{
    // How many places a find across scripts lists; the rest are counted
    // and not listed, since a common word in an object's scripts is
    // thousands of rows nobody reads.
    const S32 SEARCH_ROWS = 2000;
}

static LLPanelInjector<ALScriptSearchPane> t_script_studio_search("script_studio_search");

ALScriptSearchPane::ALScriptSearchPane(const LLPanel::Params& params) : LLPanel(params) {}

bool ALScriptSearchPane::postBuild()
{
    mBar         = getChild<ALScopeBar>("search_bar");
    mResults     = getChild<ALPaneList>("search_results");
    mReplacement = getChild<LLLineEditor>("search_replacement");
    mReplace     = getChild<LLButton>("search_replace");
    // The window this is a tab of, found through the view tree, as what
    // the tab asks of it.
    LLFloater* window = getParentByType<LLFloater>();
    mServices         = dynamic_cast<ALScriptStudioServices*>(window);
    mWindow           = dynamic_cast<Window*>(window);
    if (!mServices || !mWindow)
    {
        LL_WARNS() << "The Search tab is not in a Script Studio window" << LL_ENDL;
        return true;
    }
    buildSentence();
    // How many places, in the slot at the sentence's right end.
    LLTextBox::Params count(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    count.name         = "search_count";
    count.rect         = LLRect(0, 22, 170, 0);
    count.font_halign  = LLFontGL::RIGHT;
    count.use_ellipses = true;
    count.tool_tip     = mServices->words("SearchCountTip");
    mCount             = LLUICtrlFactory::create<LLTextBox>(count);
    mBar->setAdornment(mCount);
    mBar->onRun([this]() { run(); });
    // Asked about first, on the button or return in the box -- and never on
    // the keyboard leaving the box, which the skin says.
    mReplace->setCommitCallback([this](LLUICtrl*, const LLSD&) { askReplaceAll(); });
    mReplacement->setCommitCallback([this](LLUICtrl*, const LLSD&) { askReplaceAll(); });
    // Replace All as wide as its words, where the skin's are longer, and
    // the box beside it the rest: both follow the edges from here.
    {
        const LLRect button = mReplace->getRect();
        const LLRect box    = mReplacement->getRect();
        const S32    gap    = button.mLeft - box.mRight;
        const S32    width  = llmax(button.getWidth(), LLFontGL::getFontSansSerifSmall()->getWidth(mReplace->getLabelUnselected()) + 20);
        mReplace->setShape(LLRect(button.mRight - width, button.mTop, button.mRight, button.mBottom));
        mReplacement->setShape(LLRect(box.mLeft, box.mTop, llmax(box.mLeft + 60, button.mRight - width - gap), box.mBottom));
    }
    mBar->onChanged([this]() { onChanged(); });
    mResults->setCommitCallback([this](LLUICtrl*, const LLSD&) { choose(false); });
    // Return and a double-click go to the place chosen, to type there, and
    // escape goes back to the script without going anywhere.
    mResults->setGo([this]() { choose(true); });
    mResults->setBack([this]() { mServices->revealed(mResults, true); });
    mResults->setCopyable(true);
    // Sorted by a column's title: a script's places by where they are, and
    // the scripts in the order they were found.
    mResults->setComparison([this](S32 column, const LLScrollListItem* a, const LLScrollListItem* b) {
        const size_t fa = static_cast<size_t>(a->getValue()["found"].asInteger()), pa = static_cast<size_t>(a->getValue()["place"].asInteger());
        const size_t fb = static_cast<size_t>(b->getValue()["found"].asInteger()), pb = static_cast<size_t>(b->getValue()["place"].asInteger());
        if (fa >= mSearch.found().size() || fb >= mSearch.found().size() || pa >= mSearch.found()[fa].places.size() || pb >= mSearch.found()[fb].places.size())
        {
            return 0;
        }
        const ALTextPos x = mSearch.found()[fa].places[pa].begin;
        const ALTextPos y = mSearch.found()[fb].places[pb].begin;
        S32             said = 0;
        switch (column)
        {
            case 0: said = LLStringUtil::compareDict(mSearch.found()[fa].where, mSearch.found()[fb].where); break;
            case 2: said = LLStringUtil::compareDict(mSearch.found()[fa].lines[pa], mSearch.found()[fb].lines[pb]); break;
            default: break;
        }
        if (said == 0 && column == 0 && fa != fb)
        {
            said = fa < fb ? -1 : 1;
        }
        return said != 0 ? said : x < y ? -1 : y < x ? 1 : fa < fb ? -1 : fa > fb ? 1 : 0;
    });
    return true;
}



// Said as a sentence rather than as a form:
//
//     Find `timer` as [text] [ignoring case] in [open scripts]
void ALScriptSearchPane::buildSentence()
{
    std::vector<ALScopeBar::Segment> said;

    ALScopeBar::Segment find;
    find.kind = ALScopeBar::Segment::Kind::Word;
    find.text = mServices->words("SearchFind");
    said.push_back(find);

    ALScopeBar::Segment query;
    query.kind    = ALScopeBar::Segment::Kind::Field;
    query.name    = "query";
    query.text    = mServices->words("SearchPlaceholder");
    query.toolTip = mServices->words("SearchQueryTip");
    said.push_back(query);

    ALScopeBar::Segment as;
    as.kind = ALScopeBar::Segment::Kind::Word;
    as.text = mServices->words("SearchAs");
    said.push_back(as);

    ALScopeBar::Segment how;
    how.kind    = ALScopeBar::Segment::Kind::Choice;
    how.name    = "how";
    how.toolTip = mServices->words("SearchHowTip");
    how.choices = { { mServices->words("SearchText"), "text" }, { mServices->words("SearchWord"), "word" }, { mServices->words("SearchPattern"), "pattern" } };
    said.push_back(how);

    ALScopeBar::Segment letters;
    letters.kind    = ALScopeBar::Segment::Kind::Choice;
    letters.name    = "case";
    letters.toolTip = mServices->words("SearchCaseTip");
    letters.choices = { { mServices->words("SearchAnyCase"), "any" }, { mServices->words("SearchThisCase"), "exact" } };
    said.push_back(letters);

    ALScopeBar::Segment in;
    in.kind = ALScopeBar::Segment::Kind::Word;
    in.text = mServices->words("SearchIn");
    said.push_back(in);

    ALScopeBar::Segment where;
    where.kind    = ALScopeBar::Segment::Kind::Choice;
    where.name    = "scope";
    where.toolTip = mServices->words("SearchScopeTip");
    where.choices = { { mServices->words("SearchOpen"), "open" },
                      { mServices->words("SearchThisObject"), "object" },
                      { mServices->words("SearchListed"), "listed" },
                      { mServices->words("SearchInventory"), "inventory" } };
    said.push_back(where);

    // And what they include: the text an #include or a require brings in,
    // each file once, as its own.
    ALScopeBar::Segment includes;
    includes.kind    = ALScopeBar::Segment::Kind::Choice;
    includes.name    = "includes";
    includes.toolTip = mServices->words("SearchIncludesTip");
    includes.choices = { { mServices->words("SearchAlone"), "no" }, { mServices->words("SearchWithIncludes"), "yes" } };
    said.push_back(includes);

    mBar->setSentence(std::move(said));
}

void ALScriptSearchPane::focusQuery(const Doc* front)
{
    if (LLLineEditor* field = mBar->findChild<LLLineEditor>("query"))
    {
        // What is selected in the view in front is what is most likely
        // meant: a word read in the expansion is sought as much as one in
        // the source.
        if (front)
        {
            const ALCodeEditor& shown     = *front->shownText();
            const ALTextRange   selection = shown.selection();
            if (!selection.empty() && selection.begin.line == selection.end.line)
            {
                const ALTextRange ordered(std::min(selection.begin, selection.end), std::max(selection.begin, selection.end));
                mBar->setValue("query", shown.document().text(ordered));
            }
        }
        field->setFocus(true);
        field->selectAll();
    }
}

// A dropdown moved: the same words asked about again. Typing waits for
// return, since a search over an object's contents fetches what it has
// not got.
void ALScriptSearchPane::onChanged()
{
    const std::string query = mBar->valueOf("query");
    if (!query.empty() && query == mSearch.query())
    {
        run();
    }
}

void ALScriptSearchPane::searchOpen(const Doc& doc)
{
    if (!doc.loaded)
    {
        return;
    }
    searched(doc.ref, doc.name, mWindow->whereIs(doc), doc.editor->document(), doc.editor->document().version(), doc.id);
    if (!doc.notecard)
    {
        searchIncludes(doc.ref, doc.file, doc.name, doc.editor->text(), doc.language.lua);
    }
}

void ALScriptSearchPane::searchIncludes(const ALScriptRef& ref, const std::string& file, const std::string& name, const std::string& text, bool lua)
{
    if (mBar->valueOf("includes") != "yes")
    {
        return;
    }
    for (const Window::Included& one : mWindow->includesOf(ref, file, name, text, lua))
    {
        if (mIncludesSearched.insert(one.path).second)
        {
            const ALScriptSearch::Kept kept = mSearch.search(ALScriptRef(), one.name, mServices->words("SearchIncluded"), ALTextDocument(one.text), 0,
                                                             std::string(), false, false, one.path);
            if (kept == ALScriptSearch::Kept::Added)
            {
                addRows(mSearch.found().size() - 1);
            }
        }
    }
}

void ALScriptSearchPane::run()
{
    ALTextSearchOptions options;
    options.caseSensitive = mBar->valueOf("case") == "exact";
    options.wholeWord     = mBar->valueOf("how") == "word";
    options.regex         = mBar->valueOf("how") == "pattern";
    mSearch.begin(mBar->valueOf("query"), options);
    mIncludesSearched.clear();
    mResults->deleteAllItems();
    if (mSearch.query().empty())
    {
        mCount->setText(LLStringUtil::null);
        return;
    }
    const std::string scope = mBar->valueOf("scope");
    // The scripts open are searched as they stand, wherever they are; an
    // object's contents as the region has them, but for a script open
    // from it, which is searched as it stands too.
    if (scope == "open")
    {
        for (const Doc* doc : mServices->openDocs())
        {
            searchOpen(*doc);
        }
        settled();
        return;
    }
    // The inventory's scripts and notecards: one open here searched as it
    // stands, one open in another window as it stands there, the rest as
    // the asset server has them -- or as read for an earlier search.
    if (scope == "inventory")
    {
        const U32         generation = mSearch.generation();
        const std::string where      = mServices->words("SearchInventoryWhere");
        for (const Window::InventoryItem& one : mWindow->inventoryItems())
        {
            if (const Doc* doc = mServices->findDoc(one.ref))
            {
                searchOpen(*doc);
                continue;
            }
            if (const Doc* there = mWindow->openElsewhere(one.ref); there && there->loaded)
            {
                searched(one.ref, there->name, where, there->editor->document(), 0, std::string(), true, there->notecard);
                continue;
            }
            mSearch.asked();
            mWindow->fetchForSearch(one.ref, generation, where);
        }
        settled();
        return;
    }
    // This object: the active script's, else the one chosen in the
    // explorer; with neither, no object is guessed at, and the count says
    // what would give it one.
    LLUUID only;
    if (scope == "object")
    {
        only = mWindow->objectInHand();
        if (only.isNull())
        {
            mCount->setText(mServices->words("SearchNoObject"));
            mCount->setToolTip(mServices->words("SearchNoObjectTip"));
            return;
        }
        mSearch.setObject(only);
    }
    // Every prim of each object asked what it holds first, which is waited
    // for as a script's text is.
    mSearch.asked();
    const U32               generation = mSearch.generation();
    const LLHandle<LLPanel> handle     = getHandle();
    mWindow->listObjects(only, [handle, generation](std::vector<Window::Object> objects) {
        if (ALScriptSearchPane* pane = ALViewType::as<ALScriptSearchPane>(handle.get()))
        {
            pane->searchObjects(generation, objects);
        }
    });
    settled();
}

void ALScriptSearchPane::searchObjects(U32 generation, const std::vector<Window::Object>& objects)
{
    if (!mSearch.answered(generation))
    {
        return;
    }
    for (const Window::Object& object : objects)
    {
        mSearch.over(object.root);
        mSearch.notListed(object.unlisted);
        for (const ALScriptRef& ref : object.items)
        {
            if (const Doc* doc = mServices->findDoc(ref))
            {
                searchOpen(*doc);
                continue;
            }
            // Open in another window: searched as it stands there, and
            // what it held kept, for a replace to find it still holds.
            if (const Doc* there = mWindow->openElsewhere(ref))
            {
                if (there->loaded)
                {
                    searched(ref, there->name, object.name, there->editor->document(), 0, std::string(), true, there->notecard);
                    if (!there->notecard)
                    {
                        searchIncludes(ref, there->file, there->name, there->editor->text(), there->language.lua);
                    }
                    continue;
                }
            }
            mSearch.asked();
            mWindow->fetchForSearch(ref, generation, object.name);
        }
    }
    settled();
}

void ALScriptSearchPane::fetched(U32 generation, const std::string& where, const ALScriptRef& ref, const std::string& name,
                                 std::shared_ptr<const std::string> text, bool notecard)
{
    if (generation != mSearch.generation())
    {
        return;
    }
    if (!text)
    {
        // Not searched, and counted so: a count that read as complete over
        // a script not read would pass for having found nothing in it.
        mSearch.answered(generation);
        mSearch.notRead();
        settled();
        return;
    }
    // Sought away from the main thread, still counted as to come until
    // the answer is back.
    const LLHandle<LLPanel> handle = getHandle();
    mWindow->matchApart(text, mSearch.query(), mSearch.options(),
                        [handle, generation, where, ref, name, text, notecard](ALScriptSearch::Matched found) {
                            if (ALScriptSearchPane* pane = ALViewType::as<ALScriptSearchPane>(handle.get()))
                            {
                                pane->matched(generation, where, ref, name, *text, notecard, std::move(found));
                            }
                        });
}

void ALScriptSearchPane::matched(U32 generation, const std::string& where, const ALScriptRef& ref, const std::string& name, const std::string& text,
                                 bool notecard, ALScriptSearch::Matched found)
{
    if (!mSearch.answered(generation))
    {
        return;
    }
    // A wrapped script is searched as its author wrote it, and that text
    // kept for a replace to work over.
    const ALScriptSearch::Kept kept = mSearch.keep(ref, name, where, std::move(found), 0, std::string(), &text, notecard);
    if (mSearch.badPattern())
    {
        mCount->setToolTip(mSearch.patternError());
    }
    else if (kept == ALScriptSearch::Kept::Added)
    {
        addRows(mSearch.found().size() - 1);
    }
    if (!notecard)
    {
        searchIncludes(ref, std::string(), name, text, ALScriptMessages::looksLikeLua(text));
    }
    settled();
}

void ALScriptSearchPane::searched(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text, U32 version,
                                  const std::string& doc_id, bool keep_text, bool notecard)
{
    const ALScriptSearch::Kept kept = mSearch.search(ref, name, where, text, version, doc_id, keep_text, notecard);
    if (mSearch.badPattern())
    {
        // Said by the count, which says it until the next search.
        mCount->setToolTip(mSearch.patternError());
        return;
    }
    if (kept == ALScriptSearch::Kept::Added)
    {
        addRows(mSearch.found().size() - 1);
    }
}

void ALScriptSearchPane::addRows(size_t index)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    const ALScriptSearch::Found& found = mSearch.found()[index];
    for (size_t i = 0; i < found.places.size(); ++i)
    {
        if (mResults->getItemCount() >= SEARCH_ROWS)
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
        row["columns"][1]["value"]  = llformat("%d:%d", mServices->shownLine(match.begin.line, found.notecard && found.file.empty() && !found.ref.isNull()),
                                               match.begin.column + 1);
        row["columns"][2]["column"] = "text";
        row["columns"][2]["value"]  = found.lines[i];
        LLScrollListItem* item      = mResults->addElement(row);
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

void ALScriptSearchPane::refill()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    // The row chosen and the scroll kept, by where the row's place is.
    const S32 scrolled = mResults->getScrollPos();
    // The row chosen, by whose place it is: the numbers a row carries are
    // where its script stood in the list, which the scripts that found
    // nothing any more, going, move along.
    std::string chosen_doc;
    ALScriptRef chosen_ref;
    S32         chosen_place = -1;
    if (LLScrollListItem* item = mResults->getFirstSelected())
    {
        const size_t found = static_cast<size_t>(item->getValue()["found"].asInteger());
        if (found < mSearch.found().size())
        {
            chosen_doc   = mSearch.found()[found].doc;
            chosen_ref   = mSearch.found()[found].ref;
            chosen_place = item->getValue()["place"].asInteger();
        }
    }
    mResults->deleteAllItems();
    // What found nothing any more goes; what did is listed again, the hits
    // counted afresh.
    mSearch.dropEmpty();
    S32 chosen_found = -1;
    for (size_t i = 0; i < mSearch.found().size() && chosen_place >= 0; ++i)
    {
        const ALScriptSearch::Found& one = mSearch.found()[i];
        if (!chosen_doc.empty() ? one.doc == chosen_doc : one.doc.empty() && one.ref == chosen_ref)
        {
            chosen_found = static_cast<S32>(i);
            break;
        }
    }
    mSearch.recount();
    for (size_t i = 0; i < mSearch.found().size(); ++i)
    {
        addRows(i);
    }
    if (chosen_found >= 0)
    {
        // By the row's own numbers: a list compares a map it is given as
        // text, and every map reads as the same text.
        mResults->updateSort();
        const std::vector<LLScrollListItem*> rows = mResults->getAllData();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const LLSD& value = rows[i]->getValue();
            if (value["found"].asInteger() == chosen_found && value["place"].asInteger() == chosen_place)
            {
                mResults->selectNthItem(static_cast<S32>(i));
                break;
            }
        }
    }
    mResults->setScrollPos(scrolled);
    settled();
}

void ALScriptSearchPane::typedIn(const Doc& doc)
{
    // Only a script this search is over: any open one, for a search of
    // the scripts open; one of the object searched, or of any listed.
    if (mSearch.query().empty() || !doc.loaded)
    {
        return;
    }
    const std::string scope = mBar->valueOf("scope");
    if (scope == "inventory")
    {
        // One of the inventory's, which the search was over.
        if (!doc.ref.inInventory() || doc.ref.isNull())
        {
            return;
        }
    }
    else if (scope != "open")
    {
        // One of the objects the search was over: the one searched, or
        // those listed when it began -- not any open script's.
        if (doc.ref.inInventory() || doc.ref.isNull() || !mSearch.isOver(mWindow->rootOf(doc.ref)))
        {
            return;
        }
    }
    mSearch.typedIn(doc.id, LLTimer::getTotalSeconds());
}

void ALScriptSearchPane::pump()
{
    // Searched again only while the results can be seen, and once they
    // are: what was typed in meanwhile waits.
    if (!ALPaneFolds::inSight(this) || !mSearch.due(LLTimer::getTotalSeconds()))
    {
        return;
    }
    // Each script searched again, and its own rows told what it holds
    // now, where they are; the list made again only where a script holds
    // more places or fewer, or none any more.
    bool rebuild = false;
    for (const std::string& id : mSearch.takeTyped())
    {
        const Doc* doc = mServices->findDoc(id);
        if (!doc)
        {
            continue;
        }
        size_t at  = std::string::npos;
        size_t had = 0;
        for (size_t i = 0; i < mSearch.found().size(); ++i)
        {
            if (mSearch.found()[i].doc == doc->id)
            {
                at  = i;
                had = mSearch.found()[i].places.size();
                break;
            }
        }
        searched(doc->ref, doc->name, mWindow->whereIs(*doc), doc->editor->document(), doc->editor->document().version(), doc->id);
        if (at != std::string::npos && (mSearch.found()[at].places.size() != had || !refreshRows(at)))
        {
            rebuild = true;
        }
    }
    if (rebuild)
    {
        refill();
        return;
    }
    mSearch.recount();
    settled();
}

bool ALScriptSearchPane::refreshRows(size_t index)
{
    if (index >= mSearch.found().size())
    {
        return false;
    }
    const ALScriptSearch::Found& found = mSearch.found()[index];
    for (LLScrollListItem* item : mResults->getAllData())
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
        line->setValue(llformat("%d:%d", mServices->shownLine(match.begin.line, found.notecard && found.file.empty() && !found.ref.isNull()),
                                match.begin.column + 1));
        text->setValue(found.lines[place]);
        const S32 at     = found.at[place];
        const S32 length = at < 0 || at >= static_cast<S32>(found.lines[place].size())
                               ? 0
                               : match.end.line == match.begin.line ? match.end.column - match.begin.column : static_cast<S32>(found.lines[place].size()) - at;
        text->highlightText(llmax(0, at), llmin(length, static_cast<S32>(found.lines[place].size()) - llmax(0, at)));
    }
    return true;
}

void ALScriptSearchPane::settled()
{
    // How many places in how many files, each counted in its own form.
    LLStringUtil::format_map_t args;
    args["[HITS]"]  = mServices->counted("Matches", mSearch.hits());
    args["[FILES]"] = mServices->counted("Files", mSearch.files());
    args["[SHOWN]"] = std::to_string(SEARCH_ROWS);
    if (mSearch.badPattern())
    {
        mCount->setText(mServices->words("SearchBadPattern"));
        return;
    }
    std::string said = mSearch.pending() > 0         ? mServices->words("SearchCounting", args)
                       : mSearch.hits() > SEARCH_ROWS ? mServices->words("SearchCountCapped", args)
                       : mSearch.hits() > 0           ? mServices->words("SearchCount", args)
                                                      : mServices->words("SearchNone", args);
    // What it could not look through, after what it found.
    std::string missed;
    if (mSearch.unlisted() > 0)
    {
        missed = mServices->counted("SearchUnlisted", mSearch.unlisted());
    }
    if (mSearch.unread() > 0)
    {
        const std::string unread = mServices->counted("SearchUnread", mSearch.unread());
        missed = missed.empty() ? unread : mServices->words("SearchBoth", { { "[FIRST]", missed }, { "[SECOND]", unread } });
    }
    if (!missed.empty())
    {
        said = mServices->words("SearchPassedOver", { { "[SAID]", said }, { "[MISSED]", missed } });
    }
    mCount->setText(said);
    // The whole of it on the tip, where the slot cuts it; and which object,
    // for a search of one -- the rows say it too, but a search that found
    // nothing has no rows to say it.
    std::string tip = said + "\n" + mServices->words("SearchCountTip");
    if (mSearch.object().notNull())
    {
        LLStringUtil::format_map_t with = args;
        with["[OBJECT]"]                = mWindow->objectName(mSearch.object());
        tip                             = mServices->words("SearchObjectTip", with);
    }
    mCount->setToolTip(tip);
}

ALScriptSearchPane::Doc* ALScriptSearchPane::tabOf(const ALScriptSearch::Found& one)
{
    // By the tab it was searched in, else by its item: closed and opened
    // again since, it is the same script in another tab.
    Doc* doc = !one.doc.empty() ? mServices->findDoc(one.doc) : nullptr;
    if (!doc && !one.ref.isNull())
    {
        doc = mServices->findDoc(one.ref);
    }
    return doc;
}

bool ALScriptSearchPane::replaceable(const ALScriptSearch::Found& one)
{
    // Places found, in a script -- a notecard is left as it is -- that may
    // be changed where it is open.
    if (one.places.empty() || one.notecard)
    {
        return false;
    }
    const Doc* doc = tabOf(one);
    return !doc || (!doc->notecard && doc->modifiable);
}

void ALScriptSearchPane::askReplaceAll()
{
    // What would change, said before anything does: every place found, in
    // every script found that may be changed.
    S32 places = 0, scripts = 0;
    for (const ALScriptSearch::Found& one : mSearch.found())
    {
        if (replaceable(one))
        {
            places += static_cast<S32>(one.places.size());
            ++scripts;
        }
    }
    if (mSearch.query().empty() || places == 0)
    {
        mServices->setStatus(mServices->words("SearchReplaceNothing"), true);
        return;
    }
    LLSD args;
    args["PLACES"]  = mServices->counted("Places", places);
    args["SCRIPTS"] = mServices->counted("Scripts", scripts);
    args["WITH"]    = mReplacement->getText();
    mWindow->confirmReplaceAll(args, [this]() { replaceAll(); });
}

void ALScriptSearchPane::replaceAll()
{
    if (mSearch.found().empty() || mSearch.query().empty())
    {
        mServices->setStatus(mServices->words("SearchReplaceNothing"), true);
        return;
    }
    const std::string with = mReplacement->getText();

    // The scripts as they were found, every place in each replaced as one
    // step: in a tab where the script is open and reads as it did when it
    // was searched; in a tab opened for it, once its text is in, where it
    // was not open, the change left unsaved. One that has been typed in
    // since the search is left alone, and said so.
    const std::vector<ALScriptSearch::Found> found = mSearch.found();
    S32                                      places = 0, scripts = 0, opened = 0, left = 0;
    for (const ALScriptSearch::Found& one : found)
    {
        Doc*                here  = tabOf(one);
        Doc*                there = !here && !one.ref.isNull() ? mWindow->openElsewhere(one.ref) : nullptr;
        Doc*                tab   = here ? here : there;
        ALScriptSearch::Now now;
        now.at = here ? ALScriptSearch::Now::At::Here : there ? ALScriptSearch::Now::At::Elsewhere : ALScriptSearch::Now::At::Closed;
        if (tab)
        {
            now.notecard   = tab->notecard;
            now.loaded     = tab->loaded;
            now.modifiable = tab->modifiable;
            now.text       = &tab->editor->document();
        }
        switch (ALScriptSearch::step(one, now))
        {
            case ALScriptSearch::Step::Skip:
                break;
            case ALScriptSearch::Step::Leave:
                ++left;
                break;
            case ALScriptSearch::Step::Replace:
                if (tab->editor->replaceAll(mSearch.replacements(one, tab->editor->document(), with)))
                {
                    tab->editor->undoJournal().label("replace");
                    places += static_cast<S32>(one.places.size());
                    ++scripts;
                }
                break;
            case ALScriptSearch::Step::Open:
            {
                // Not open: the replacements worked out over the text it was
                // searched in, and made once it has loaded where each place
                // still reads as it did.
                const std::vector<Doc::PendingEdit> edits = mSearch.pendingEdits(one, with);
                mServices->openScript(one.ref, one.name);
                Doc* doc = mServices->findDoc(one.ref);
                if (!doc)
                {
                    ++left;
                    break;
                }
                doc->pendingEdits.insert(doc->pendingEdits.end(), edits.begin(), edits.end());
                mWindow->applyPendingEdits(*doc);
                places += static_cast<S32>(one.places.size());
                ++scripts;
                ++opened;
                break;
            }
        }
    }
    if (places == 0 && left == 0)
    {
        mServices->setStatus(mServices->words("SearchReplaceNothing"), true);
        return;
    }
    // What was done, a clause for each thing there is to say.
    LLStringUtil::format_map_t args;
    args["[PLACES]"]  = mServices->counted("Places", places);
    args["[SCRIPTS]"] = mServices->counted("Scripts", scripts);
    std::string said  = mServices->words("SearchReplaced", args);
    if (opened > 0)
    {
        args["[SCRIPTS]"] = mServices->counted("Scripts", opened);
        said = mServices->clauses(said, mServices->words("SearchReplacedOpened", args));
    }
    if (left > 0)
    {
        args["[SCRIPTS]"] = mServices->counted("Scripts", left);
        said = mServices->clauses(said, mServices->words("SearchReplacedLeft", args));
    }
    mServices->report(mServices->sentence(said), left > 0);
    // Whatever stood before, the places have moved: looked for again.
    run();
}

void ALScriptSearchPane::choose(bool to_editor)
{
    const LLScrollListItem* item = mResults->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD&  value = item->getValue();
    const size_t found = static_cast<size_t>(value["found"].asInteger());
    const size_t place = static_cast<size_t>(value["place"].asInteger());
    if (found >= mSearch.found().size() || place >= mSearch.found()[found].places.size())
    {
        return;
    }
    // A copy: going there can open a tab, which searches again.
    const ALScriptSearch::Found one   = mSearch.found()[found];
    const ALTextRange           match = one.places[place];
    mWindow->searchResultChosen(one, match, to_editor);
}

void ALScriptSearchPane::rekey(const std::string& from, const std::string& to)
{
    mSearch.rekey(from, to);
}

void ALScriptSearchPane::saveState(LLSD& state) const
{
    for (const char* choice : { "how", "case", "scope" })
    {
        state["search"][choice] = mBar->valueOf(choice);
    }
}

void ALScriptSearchPane::readState(const LLSD& state)
{
    for (const char* choice : { "how", "case", "scope" })
    {
        if (state["search"].has(choice))
        {
            mBar->setValue(choice, state["search"][choice].asString());
        }
    }
}
