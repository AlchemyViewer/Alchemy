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
        // The time stamps, dimmer than the text unless a skin says.
        Optional<LLUIColor> time_color;
        Params();
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
        S32 stamp  = 0;  // the bytes of the time stamp on its first line
    };
    void refill();
    void show(const Entry& entry, U32 serial);
    bool passes(const Entry& entry) const { return !mFilter || mFilter(entry); }
    // Whether the last row is in sight, which is when a new one should be.
    bool atTail();
    void followTail();
    // The shown entry a line is in, or null.
    const Shown* shownAt(S32 line, S32* first_line) const;
    const Entry* entryOf(U32 serial) const;
    void         followed(const Substitution& link);

    std::deque<Entry> mEntries;
    std::deque<U32>   mSerials;
    std::deque<Shown> mShown;
    U32               mNextSerial = 1;
    filter_t          mFilter;
    S32               mCapacity = 500;
    LLUIColor         mTimeColor;
    entry_signal_t    mEntryChosen;
    url_signal_t      mUrlChosen;
};
