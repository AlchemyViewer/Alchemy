/**
 * @file alquickopen.cpp
 * @brief One field, a ranked list, and the thing you were thinking of at the top.
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

#include "alquickopen.h"

#include "alsaid.h"
#include "alsurface.h"

#include "alchoicelist.h"
#include "llfontgl.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALQuickOpen> r("quick_open");

namespace
{
    constexpr S32 FIELD_HEIGHT = 24;
    constexpr S32 GAP = 4;
    // The inset from the frame, once there is one.
    constexpr S32 INSET = 6;

    // The list as every quick open has it: on the text engine, one line a
    // choice in the reading face, its detail after it.
    ALChoiceList* makeList(const LLRect& rect)
    {
        ALChoiceList::Params lp(LLUICtrlFactory::getDefaultParams<ALChoiceList>());
        lp.name = "matches";
        lp.rect = rect;
        lp.follows.flags(FOLLOWS_ALL);
        lp.mouse_opaque(true);
        lp.font(LLFontGL::getFontSansSerifSmall());
        lp.context_menu(std::string());
        lp.h_pad(4);
        lp.v_pad(2);
        return LLUICtrlFactory::create<ALChoiceList>(lp);
    }

    // What each degree of meaning it is worth. The gaps are wide because
    // these are kinds and not amounts: no number of scattered letters adds up
    // to a name that starts with what you typed.
    constexpr S32 SCORE_WHOLE       = 10000;
    constexpr S32 SCORE_PREFIX      = 5000;
    constexpr S32 SCORE_INITIALS    = 3000;
    constexpr S32 SCORE_WORD_START  = 2000;
    constexpr S32 SCORE_RUN         = 1000;
    constexpr S32 SCORE_SCATTERED   = 100;
    // The most scattered letters are worth: with the shortest label, the
    // soonest finish.
    constexpr S32 SCORE_SCATTERED_MOST = SCORE_SCATTERED + 200;
    // What the other words a candidate answers to score below what its
    // label would for the same match: a tier. A label that answers is
    // still what was typed, and the words are how else it is known.
    constexpr S32 SCORE_WORDS_BELOW = 1000;

    // A candidate's rank from how its label and its other words answer:
    // the other words a tier down.
    S32 rankOf(S32 by_label, S32 by_words)
    {
        return llmax(by_label, by_words > 0 ? llmax(1, by_words - SCORE_WORDS_BELOW) : 0);
    }

    std::vector<size_t> ordered(std::vector<std::pair<S32, size_t>>& scored)
    {
        // Best first; among equals, the order they were given, which is the
        // caller's own idea of which matters more.
        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<size_t> order;
        order.reserve(scored.size());
        for (const auto& [how, at] : scored)
        {
            order.push_back(at);
        }
        return order;
    }
}

// static
S32 ALQuickOpen::score(std::string_view label, std::string_view query)
{
    return score(ALFuzzyMatch::prepare(label), query);
}

// static
S32 ALQuickOpen::score(const ALFuzzyMatch::Target& label, std::string_view query)
{
    if (query.empty())
    {
        return 1;   // everything answers nothing, equally
    }
    if (label.text.empty() || query.size() > label.text.size())
    {
        return 0;
    }
    // A shorter label answering the same query answered it better: `panel`
    // beats `panel_preferences_advanced` for `pan`, and it is the one meant.
    const S32 brevity = (S32)llmax(0, 200 - (S32)label.text.size());

    bool whole = label.lowered.size() == query.size();
    for (size_t i = 0; whole && i < query.size(); ++i)
    {
        whole = label.lowered[i] == ALFuzzyMatch::lower(query[i]);
    }
    if (whole)
    {
        return SCORE_WHOLE + brevity;
    }
    const ALFuzzyMatch::Match match = ALFuzzyMatch::match(label, query);
    switch (match.tier)
    {
        case ALFuzzyMatch::Tier::Prefix:
        case ALFuzzyMatch::Tier::PrefixAnyCase:
            return SCORE_PREFIX + brevity;
        // The letters of its words: `fb` or `flbuy` for `floater_buy.xml`.
        case ALFuzzyMatch::Tier::Parts:
            return SCORE_INITIALS + brevity;
        // A word inside the name starting with it: `buy` in
        // `floater_buy.xml`.
        case ALFuzzyMatch::Tier::PartRun:
            return SCORE_WORD_START + brevity;
        // The two that are worth less the further in they are found keep
        // to their own tier however far that is: a run never sinks to
        // scattered letters, and scattered letters never to nothing -- a
        // long line of history is still an answer.
        case ALFuzzyMatch::Tier::Run:
            return llmax(SCORE_SCATTERED_MOST + 1, SCORE_RUN + brevity - (S32)llmin(match.at, (size_t)SCORE_RUN));
        case ALFuzzyMatch::Tier::Scattered:
            return llmax(1, SCORE_SCATTERED + brevity - (S32)llmin(match.at, (size_t)SCORE_SCATTERED_MOST));
        default:
            return 0;
    }
}

// static
std::vector<size_t> ALQuickOpen::rank(const std::vector<Candidate>& candidates,
                                      std::string_view query)
{
    // By the label, or by the other words a tier down: a candidate
    // answering both is scored by whichever answers better.
    std::vector<std::pair<S32, size_t> > scored;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        const S32 how = rankOf(score(candidates[i].label, query), score(candidates[i].also, query));
        if (how > 0)
        {
            scored.emplace_back(how, i);
        }
    }
    return ordered(scored);
}

std::vector<size_t> ALQuickOpen::ranked(std::string_view query) const
{
    std::vector<std::pair<S32, size_t> > scored;
    for (size_t i = 0; i < mLabels.size(); ++i)
    {
        const S32 how = rankOf(score(mLabels[i], query), score(mAlso[i], query));
        if (how > 0)
        {
            scored.emplace_back(how, i);
        }
    }
    return ordered(scored);
}

ALQuickOpen::Params::Params()
:   placeholder("placeholder"),
    rows("rows", 100)
{
}

ALQuickOpen::ALQuickOpen(const Params& p)
:   LLPanel(p),
    mPlaceholder(p.placeholder),
    mRows(p.rows)
{
    LLLineEditor::Params fp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
    fp.name = "query";
    fp.rect = LLRect(0, getRect().getHeight(), getRect().getWidth(),
                     getRect().getHeight() - FIELD_HEIGHT);
    fp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
    fp.label = mPlaceholder;
    // A choice is Return or a double-click, never the keyboard leaving. The
    // field's commit is the choice, and a line editor commits on losing focus
    // by default -- so clicking a row further down, which hands the keyboard
    // to the list before the click lands, chose the row that was selected
    // already, and clicking away after typing chose the top match. Return
    // still commits, through LLPanel::handleKeyHere.
    fp.commit_on_focus_lost = false;
    // The placeholder says what to type, and the field has the keyboard
    // from the start: it stays until something is typed.
    fp.show_label_focused = true;
    mField = LLUICtrlFactory::create<LLLineEditor>(fp);
    mField->setKeystrokeCallback([this](LLLineEditor* editor, void*)
    {
        setQuery(editor->getText());
    }, nullptr);
    mField->setCommitCallback([this](LLUICtrl*, const LLSD&) { chooseSelected(); });
    addChild(mField);

    mList = makeList(LLRect(0, getRect().getHeight() - FIELD_HEIGHT - GAP, getRect().getWidth(), 0));
    addChild(mList);
    mList->onPicked([this](S32) { chooseSelected(); });
    layout();
}

void ALQuickOpen::setCandidates(std::vector<Candidate> candidates)
{
    mCandidates = std::move(candidates);
    mLabels.clear();
    mAlso.clear();
    mLabels.reserve(mCandidates.size());
    mAlso.reserve(mCandidates.size());
    for (const Candidate& one : mCandidates)
    {
        mLabels.push_back(ALFuzzyMatch::prepare(one.label));
        mAlso.push_back(ALFuzzyMatch::prepare(one.also));
    }
    fill();
}

void ALQuickOpen::setQuery(const std::string& query)
{
    mQuery = query;
    if (mField && mField->getText() != query)
    {
        mField->setText(query);
    }
    fill();
    mQueryChanged(mQuery);
}

void ALQuickOpen::setPrefix(const std::string& prefix)
{
    mPrefix = prefix;
    fill();
}

std::string_view ALQuickOpen::matched() const
{
    std::string_view query(mQuery);
    if (!mPrefix.empty() && query.compare(0, mPrefix.size(), mPrefix) == 0)
    {
        query.remove_prefix(mPrefix.size());
        query.remove_prefix(std::min(query.size(), query.find_first_not_of(' ')));
    }
    return query;
}

void ALQuickOpen::setHint(const std::string& hint)
{
    mFreeform = true;
    mHint     = hint;
    fill();
}

void ALQuickOpen::fill()
{
    if (mFreeform)
    {
        // The one row, saying what return does; chosen, so return takes it.
        mRanked.clear();
        mListed = { mQuery };
        mList->setPlaceholder(LLStringUtil::null);
        ALChoiceList::Choice hint;
        hint.text = mHint;
        // Not chosen: return takes what was typed whatever the list says
        // (`chooseSelected`), and a chosen row draws a band inset within
        // the list, which is a box of its own width under the field's.
        mList->setChoices({ hint }, -1);
        return;
    }
    const std::string_view asked = matched();
    mRanked                      = ranked(asked);
    // As many as were asked for: the answer meant is at the top, and a
    // list of seven hundred rows made again on every letter typed is what
    // the ranking exists to spare. With nothing typed, the first so many
    // in the order given, to browse; a letter typed finds the rest.
    if (mRows > 0 && mRanked.size() > (size_t)mRows)
    {
        mRanked.resize((size_t)mRows);
    }
    // Nothing to choose says so, rather than being an empty box.
    mList->setPlaceholder(mRanked.empty() && !asked.empty() ? alSaid("QuickOpenNone", "Nothing matches") : LLStringUtil::null);
    std::vector<ALChoiceList::Choice> choices;
    mListed.clear();
    for (size_t at : mRanked)
    {
        ALChoiceList::Choice choice;
        choice.text = mCandidates[at].label;
        choice.note = mCandidates[at].detail;
        choices.push_back(std::move(choice));
        mListed.push_back(mCandidates[at].value);
    }
    // The best answer, chosen: return takes it without an arrow key first,
    // which is the whole gesture this widget is.
    mList->setChoices(std::move(choices), mRanked.empty() ? -1 : 0);
}

S32 ALQuickOpen::chosenRow() const
{
    return mList ? mList->chosen() : -1;
}

void ALQuickOpen::chooseSelected(bool hold)
{
    chose_signal_t& signal = hold && !mChoseToHold.empty() ? mChoseToHold : mChose;
    if (mFreeform)
    {
        signal(mQuery);
    }
    else if (const S32 at = chosenRow(); at >= 0 && at < static_cast<S32>(mListed.size()))
    {
        signal(mListed[at]);
    }
}

void ALQuickOpen::setColors(const LLColor4& background, const LLColor4& ink)
{
    auto between = [&](F32 how) { return ALSurface::shade(background, ink, how); };
    const LLColor4 ground = between(0.f);
    const LLColor4 faint  = between(0.45f);
    const LLColor4 lit    = between(0.2f);
    mThemed = true;
    mGround = ALSurface::ground(background, ink);
    mInk    = ink;
    mInk.mV[VALPHA] = 1.f;
    setBackgroundVisible(false);
    layout();
    mField->setBgColor(ground);
    mField->setFgColor(ink);
    mField->setCursorColor(ink);
    mField->setTentativeFgColor(faint);
    mField->setHighlightColor(lit);
    // The list on the same ground, in the same ink; its chosen row is the
    // band every list of ours chooses with, drawn from these two.
    mList->setBackgroundColor(LLUIColor(ground));
    mList->setTextColor(LLUIColor(ink));
    mList->setSelectionColor(LLUIColor(ALSurface::chosen(background, ink)));
    mList->setBorderColor(LLUIColor(ALSurface::frame(ink)));
}

void ALQuickOpen::takeFocus()
{
    if (mField)
    {
        mField->setFocus(true);
        mField->selectAll();
    }
}

// The arrows walk the list while the keyboard stays in the field, because
// taking a hand off the letters to point at the answer is the thing this
// exists to avoid.
bool ALQuickOpen::handleKeyHere(KEY key, MASK mask)
{
    // Escape is for whatever holds the list -- a popover, which puts back
    // what was previewed and gives the keyboard back where it was. The
    // panel's own Escape only lets go of the keyboard, which the popover
    // took for a look away, leaving the preview and the keyboard with
    // nothing: the arrows then walked the avatar.
    if (key == KEY_ESCAPE && mask == MASK_NONE)
    {
        return false;
    }
    if (key == KEY_RETURN && mask == MASK_SHIFT)
    {
        // The panel's own Return asks for no shift; this one the field
        // leaves alone.
        chooseSelected(true);
        return true;
    }
    if ((key == KEY_UP || key == KEY_DOWN) && mask == MASK_NONE && mList->count() > 0)
    {
        mList->moveChoice(key == KEY_DOWN ? 1 : -1, false);
        return true;
    }
    return LLPanel::handleKeyHere(key, mask);
}

// static
S32 ALQuickOpen::heightForRows(S32 rows)
{
    // What a list made as this one's is makes of so many rows: measured on
    // such a list each time, since the faces change size under the UI's
    // scale, so that whatever the skin gives the list -- its face, its
    // padding -- is what is counted. A list is cheap; a popover asks once.
    rows                = llmax(1, rows);
    ALChoiceList* list = makeList(LLRect(0, 100, 100, 0));
    list->setChoices(std::vector<ALChoiceList::Choice>(static_cast<size_t>(rows), ALChoiceList::Choice{ "Xg" }), 0);
    const S32 height = list->heightFor(rows);
    delete list;
    return 2 * INSET + FIELD_HEIGHT + GAP + height;
}

void ALQuickOpen::layout()
{
    if (!mField || !mList)
    {
        return;
    }
    const S32 width = getRect().getWidth();
    const S32 height = getRect().getHeight();
    const S32 in = mThemed ? INSET : 0;
    mField->setShape(LLRect(in, height - in, width - in, height - in - FIELD_HEIGHT));
    mList->setShape(LLRect(in, height - in - FIELD_HEIGHT - GAP, width - in, in));
}

void ALQuickOpen::draw()
{
    const F32    alpha = getDrawContext().mAlpha;
    const LLRect local = getLocalRect();
    if (mThemed)
    {
        // The card: its ground, and a frame in a quarter of the ink as
        // the find bar's.
        gl_rect_2d(local, mGround % alpha, true);
        ALSurface::outline(local, ALSurface::frame(mInk, alpha));
    }
    LLPanel::draw();
    if (mThemed)
    {
        // The boxes inside, drawn over their own grounds rather than
        // under them: a frame outside a child's rect loses the edge the
        // child paints over, and one inside its rect is the same width
        // as the box below it to the pixel, which two frames drawn a
        // pixel apart are not. The field's is the heavier of the two,
        // being where the typing goes, and is said as a multiple of the
        // one frame weight so that it cannot drift away from it.
        if (mField)
        {
            ALSurface::outline(mField->getRect(), mInk % (1.4f * ALSurface::FRAME * alpha));
        }
        if (mList)
        {
            ALSurface::outline(mList->getRect(), ALSurface::frame(mInk, alpha));
        }
    }
}

void ALQuickOpen::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}
