/**
 * @file aloutputview.h
 * @brief A log a pane shows as text: what things said, when, in order, with links in it.
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

#include "altextview.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>

// A log a pane shows: the last so many things something said, each with
// when, who and what kind, kept in the order they came and shown through
// a filter, as text -- one entry a line, or more where what was said
// runs to more, wrapped where it is long, selected and copied like any
// text. The view follows its tail while the tail is what is being looked
// at, and lets go of the oldest once it holds its fill, which is what a
// log that runs for a session has to do.
//
// What was said is read for links: a URL is one, shown by the label the
// registry gives it and told its name when a name arrives, without the
// text moving under it; and an entry's source is one, where the entry
// says so, for whoever asked to hear of it.
//
// What was said hangs under where it began: an entry's first line wraps
// under the message's start, and the lines it runs to after the first
// start there, rather than under the time stamp.
//
// The view is the text view's first client that is not an editor:
// streaming append, head trimming, substitutions, links. It is read-only.
class ALOutputView : public ALTextView
{
public:
    AL_VIEW_TYPE(ALOutputView, ALTextView);

    struct Params : public LLInitParam::Block<Params, ALTextView::Params>
    {
        // How many entries are kept; the oldest go past it.
        Optional<S32>       capacity;
        // The time stamps, dimmer than the text and in a smaller face
        // unless a skin says; the source -- who said it -- in the ink
        // that stands out, bold; what kind of thing it was, in the time
        // stamps' ink, or the entry's own where it has one.
        Optional<LLUIColor>       time_color;
        Optional<const LLFontGL*> time_font;
        Optional<LLUIColor>       source_color;
        Optional<LLUIColor>       kind_color;
        // How many digits of the text's face fit across a pane before it
        // is wide enough for what was said to hang under its own start;
        // narrower, and it hangs under the source. In the face's own
        // measure, so that a larger UI scale narrows nothing.
        Optional<S32>             narrow_columns;
        Params();
    };

    // Where an entry's lines after the first start: under what was said
    // -- or under the source, in a pane too narrow for that -- as a
    // message reads best; under the source come what may; or at the left
    // edge, as a listing does, whose rows are a block of their own.
    enum class Hang : U8
    {
        Text,
        Source,
        None
    };

    struct Entry
    {
        std::string             time;
        std::string             source;
        // What kind of thing was said, after the source in brackets;
        // nothing for the ordinary kind.
        std::string             kind;
        std::string             text;
        // The entry's ink, where it is not the view's own.
        std::optional<LLColor4> color;
        // Where its lines after the first start.
        Hang                    hang = Hang::Text;
        // Whether the source is a link, and what it says when the mouse
        // rests on it.
        bool                    link = false;
        std::string             tooltip;
        // What the caller wants back when the source is followed.
        LLSD                    value;
        // What a filter may go by beside the words: the caller's own
        // key, an object's id say.
        LLSD                    key;
    };

    void                     append(Entry entry);
    void                     clearEntries();
    const std::deque<Entry>& entries() const { return mEntries; }
    S32                      capacity() const { return mCapacity; }
    void                     setCapacity(S32 capacity);

    // Which entries are shown: all of them, or the ones the filter takes.
    typedef std::function<bool(const Entry&)> filter_t;
    void setFilter(filter_t filter);

    // An entry's source followed.
    typedef boost::signals2::signal<void(const Entry&)> entry_signal_t;
    boost::signals2::connection onEntryChosen(const entry_signal_t::slot_type& slot) { return mEntryChosen.connect(slot); }
    // A URL in what was said, followed; the registry's action for it
    // unless somebody listens.
    typedef boost::signals2::signal<void(const std::string& url)> url_signal_t;
    boost::signals2::connection onUrlChosen(const url_signal_t::slot_type& slot) { return mUrlChosen.connect(slot); }

    // The lines an entry is shown as, for whoever lays a test on them.
    static std::string format(const Entry& entry);

protected:
    friend class LLUICtrlFactory;
    ALOutputView(const Params& p);

    void tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors) override;

private:
    // One entry as shown: which, by serial, and over how many lines.
    struct Shown
    {
        U32 serial = 0;
        S32 lines  = 0;
        // Where the pieces of its first line lie, as byte offsets: the
        // time stamp up to `stamp`, the source, the kind in its brackets,
        // and what was said from `text`.
        S32 stamp       = 0;
        S32 sourceBegin = 0;
        S32 sourceEnd   = 0;
        S32 kindBegin   = 0;
        S32 kindEnd     = 0;
        S32 text        = 0;
        Hang hang       = Hang::Text;
        // How wide the first line is up to what was said, measured once
        // in the faces it is shown in and kept until the font changes.
        mutable F32             prefix   = -1.f;
        mutable const LLFontGL* measured = nullptr;
    };
    void refill();
    void show(const Entry& entry, U32 serial);
    // An entry's lines put into the text at a line -- before what is
    // there, or after the last, or as the whole where nothing is shown
    // -- with its links, and what was shown; and so many lines from a
    // line taken out again.
    Shown showAt(const Entry& entry, U32 serial, S32 line, bool among_others);
    void  hideAt(S32 line, S32 lines);
    bool passes(const Entry& entry) const { return !mFilter || mFilter(entry); }
    // Whether the last row is in sight, which is when a new one should be.
    bool atTail();
    void followTail();
    // The shown entry a line is in, or null.
    const Shown* shownAt(S32 line, S32* first_line) const;
    // How wide an entry's first line is up to what was said, in the
    // faces the pieces are shown in: what its other rows hang under.
    // Measured once and kept on the entry.
    F32          prefixWidth(const Shown& shown, S32 first) const;
    // The time stamp's width alone, for a narrow pane's hang.
    F32          stampWidth(const Shown& shown, S32 first) const;
    const Entry* entryOf(U32 serial) const;
    void         followed(const Substitution& link);
    // Narrower than this, and what was said hangs under the source.
    S32          narrowWidth() const;

    std::deque<Entry> mEntries;
    std::deque<U32>   mSerials;
    std::deque<Shown> mShown;
    U32               mNextSerial = 1;
    filter_t          mFilter;
    S32               mCapacity = 500;
    LLUIColor         mTimeColor;
    const LLFontGL*   mTimeFont = nullptr;
    LLUIColor         mSourceColor;
    LLUIColor         mKindColor;
    S32               mNarrowColumns = 48;
    entry_signal_t    mEntryChosen;
    url_signal_t      mUrlChosen;
};
