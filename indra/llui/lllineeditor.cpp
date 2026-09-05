/**
 * @file lllineeditor.cpp
 * @brief LLLineEditor base class
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// Text editor widget to let users enter a single line.

#include "linden_common.h"

#define LLLINEEDITOR_CPP
#include "lllineeditor.h"

#include "lltexteditor.h"
#include "llmath.h"
#include "llfontgl.h"
#include "llgl.h"
#include "lltimer.h"

#include "llcalc.h"
//#include "llclipboard.h"
#include "llcontrol.h"
#include "llbutton.h"
#include "llfocusmgr.h"
#include "llkeyboard.h"
#include "llrect.h"
#include "llresmgr.h"
#include "llspellcheck.h"
#include "llstring.h"
#include "llwindow.h"
#include "llui.h"
#include "altextcaret.h"
#include "lluictrlfactory.h"
#include "llclipboard.h"
#include "llmenugl.h"

//
// Imported globals
//

//
// Constants
//

const F32   CURSOR_FLASH_DELAY = 1.0f;  // in seconds
const S32   SCROLL_INCREMENT_ADD = 0;   // make space for typing
const S32   SCROLL_INCREMENT_DEL = 4;   // make space for baskspacing
const F32   AUTO_SCROLL_TIME = 0.05f;
const F32   TRIPLE_CLICK_INTERVAL = 0.3f;   // delay between double and triple click. *TODO: make this equal to the double click interval?
const F32   SPELLCHECK_DELAY = 0.5f;    // delay between the last keypress and spell checking the word the cursor is on

const std::string PASSWORD_ASTERISK( "\xE2\x80\xA2" ); // U+2022 BULLET

namespace
{

// The scroll increments above count characters. Over UTF-8 that is a walk
// rather than an addition: adding four to a byte offset lands inside a
// character as often as not. Negative counts step back.
S32 step_chars(std::string_view text, S32 from, S32 chars)
{
    size_t pos = (size_t)llmax(0, from);
    for (S32 i = 0; i < chars; ++i)
        pos = utf8str_step_grapheme_forward(text, pos);
    for (S32 i = 0; i > chars; --i)
        pos = utf8str_step_grapheme_backward(text, pos);
    return (S32)pos;
}

}

static LLDefaultChildRegistry::Register<LLLineEditor> r1("line_editor");

// Compiler optimization, generate extern template
template class LLLineEditor* LLView::getChild<class LLLineEditor>(
    std::string_view name, bool recurse) const;

//
// Member functions
//

LLLineEditor::Params::Params()
:   max_length(""),
    keystroke_callback("keystroke_callback"),
    prevalidator("prevalidator"),
    input_prevalidator("input_prevalidator"),
    background_image("background_image"),
    background_image_disabled("background_image_disabled"),
    background_image_focused("background_image_focused"),
    bg_image_always_focused("bg_image_always_focused", false),
    show_label_focused("show_label_focused", false),
    select_on_focus("select_on_focus", false),
    revert_on_esc("revert_on_esc", true),
    spellcheck("spellcheck", false),
    commit_on_focus_lost("commit_on_focus_lost", true),
    ignore_tab("ignore_tab", true),
    is_password("is_password", false),
    allow_emoji("allow_emoji", true),
    draw_focus_border("draw_focus_border", true),
    cursor_color("cursor_color"),
    use_bg_color("use_bg_color", false),
    bg_color("bg_color"),
    text_color("text_color"),
    text_readonly_color("text_readonly_color"),
    text_tentative_color("text_tentative_color"),
    highlight_color("highlight_color"),
    preedit_bg_color("preedit_bg_color"),
    border(""),
    bg_visible("bg_visible"),
    text_pad_left("text_pad_left"),
    text_pad_right("text_pad_right"),
    default_text("default_text")
{
    changeDefault(mouse_opaque, true);
    addSynonym(prevalidator, "prevalidate_callback");
    addSynonym(input_prevalidator, "prevalidate_input_callback");
    addSynonym(select_on_focus, "select_all_on_focus_received");
    addSynonym(border, "border");
    addSynonym(label, "watermark_text");
    addSynonym(max_length.chars, "max_length");
}

LLLineEditor::LLLineEditor(const LLLineEditor::Params& p)
:   LLUICtrl(p),
    mDefaultText(p.default_text),
    mMaxLengthBytes(p.max_length.bytes),
    mMaxLengthChars(p.max_length.chars),
    mCursorPos( 0 ),
    mScrollHPos( 0 ),
    mTextPadLeft(p.text_pad_left),
    mTextPadRight(p.text_pad_right),
    mTextLeftEdge(0),       // computed in updateTextPadding() below
    mTextRightEdge(0),      // computed in updateTextPadding() below
    mCommitOnFocusLost( p.commit_on_focus_lost ),
    mKeystrokeOnEsc(false),
    mRevertOnEsc( p.revert_on_esc ),
    mKeystrokeCallback( p.keystroke_callback() ),
    mIsSelecting( false ),
    mSelectionStart( 0 ),
    mSelectionEnd( 0 ),
    mLastSelectionStart(-1),
    mLastSelectionEnd(-1),
    mBorderThickness( 0 ),
    mIgnoreArrowKeys( false ),
    mIgnoreTab( p.ignore_tab ),
    mDrawAsterixes( p.is_password ),
    mAllowEmoji( p.allow_emoji ),
    mDrawFocusBorder(p.draw_focus_border),
    mSpellCheck( p.spellcheck ),
    mSpellCheckStart(-1),
    mSpellCheckEnd(-1),
    mSelectAllonFocusReceived( p.select_on_focus ),
    mSelectAllonCommit( true ),
    mPassDelete(false),
    mReadOnly(false),
    mBgImage( p.background_image ),
    mBgImageDisabled( p.background_image_disabled ),
    mBgImageFocused( p.background_image_focused ),
    mShowImageFocused( p.bg_image_always_focused ),
    mShowLabelFocused( p.show_label_focused ),
    mUseBgColor(p.use_bg_color),
    mHaveHistory(false),
    mReplaceNewlinesWithSpaces( true ),
    mPrevalidator(p.prevalidator()),
    mInputPrevalidator(p.input_prevalidator()),
    mLabel(p.label),
    mCursorColor(p.cursor_color()),
    mBgColor(p.bg_color()),
    mFgColor(p.text_color()),
    mReadOnlyFgColor(p.text_readonly_color()),
    mTentativeFgColor(p.text_tentative_color()),
    mHighlightColor(p.highlight_color()),
    mPreeditBgColor(p.preedit_bg_color()),
    mGLFont(p.font),
    mContextMenuHandle(),
    mShowContextMenu(true),
    mAutoreplaceCallback()
{
    llassert( mMaxLengthBytes > 0 );

    LLUICtrl::setEnabled(true);
    setEnabled(p.enabled);

    mScrollTimer.reset();
    mTripleClickTimer.reset();
    setText(p.default_text());

    if (p.initial_value.isProvided()
        && !p.control_name.isProvided())
    {
        // Initial value often is descriptive, like "Type some ID here"
        // and can be longer than size limitation, ignore size
        setText(p.initial_value.getValue().asString(), false);
    }

    // Initialize current history line iterator
    mCurrentHistoryLine = mLineHistory.begin();

    LLRect border_rect(getLocalRect());
    // adjust for gl line drawing glitch
    border_rect.mTop -= 1;
    border_rect.mRight -=1;
    LLViewBorder::Params border_p(p.border);
    border_p.rect = border_rect;
    border_p.follows.flags = FOLLOWS_ALL;
    border_p.bevel_style = LLViewBorder::BEVEL_IN;
    mBorder = LLUICtrlFactory::create<LLViewBorder>(border_p);
    addChild( mBorder );

    // clamp text padding to current editor size
    updateTextPadding();

    // read-only fields anchor to start, editable ones to end of initial text
    setCursor(mReadOnly ? 0 : mText.lengthBytes());

    if (mSpellCheck)
    {
        LLSpellChecker::setSettingsChangeCallback(boost::bind(&LLLineEditor::onSpellCheckSettingsChange, this));
    }
    mSpellCheckTimer.reset();

    updateAllowingLanguageInput();
}

LLLineEditor::~LLLineEditor()
{
    mCommitOnFocusLost = false;

    // Make sure no context menu linger around once the widget is deleted
    LLContextMenu* menu = static_cast<LLContextMenu*>(mContextMenuHandle.get());
    if (menu)
    {
        menu->hide();
    }
    setContextMenu(NULL);

    // calls onCommit() while LLLineEditor still valid
    gFocusMgr.releaseFocusIfNeeded( this );
}

void LLLineEditor::initFromParams(const LLLineEditor::Params& params)
{
    LLUICtrl::initFromParams(params);
    LLUICtrl::setEnabled(true);
    setEnabled(params.enabled);
}

void LLLineEditor::onFocusReceived()
{
    gEditMenuHandler = this;
    LLUICtrl::onFocusReceived();
    updateAllowingLanguageInput();
}

void LLLineEditor::onFocusLost()
{
    // The call to updateAllowLanguageInput()
    // when loosing the keyboard focus *may*
    // indirectly invoke handleUnicodeCharHere(),
    // so it must be called before onCommit.
    updateAllowingLanguageInput();

    if( mCommitOnFocusLost && mText.getString() != mPrevText)
    {
        onCommit();
    }

    if( gEditMenuHandler == this )
    {
        gEditMenuHandler = NULL;
    }

    getWindow()->showCursorFromMouseMove();

    LLUICtrl::onFocusLost();
}

// virtual
void LLLineEditor::onCommit()
{
    // put current line into the line history
    updateHistory();

    setControlValue(getValue());
    LLUICtrl::onCommit();
    resetDirty();

    // Selection on commit needs to be turned off when evaluating maths
    // expressions, to allow indication of the error position
    if (mSelectAllonCommit) selectAll();
}

// Returns true if user changed value at all
// virtual
bool LLLineEditor::isDirty() const
{
    return mText.getString() != mPrevText;
}

// Clear dirty state
// virtual
void LLLineEditor::resetDirty()
{
    mPrevText = mText.getString();
}

// assumes UTF8 text
// virtual
void LLLineEditor::setValue(const LLSD& value )
{
    setText(value.asString());
}

//virtual
LLSD LLLineEditor::getValue() const
{
    return LLSD(getText());
}


// line history support
void LLLineEditor::updateHistory()
{
    // On history enabled line editors, remember committed line and
    // reset current history line number.
    // Be sure only to remember lines that are not empty and that are
    // different from the last on the list.
    if( mHaveHistory && getLengthBytes() )
    {
        if( !mLineHistory.empty() )
        {
            // When not empty, last line of history should always be blank.
            if( mLineHistory.back().empty() )
            {
                // discard the empty line
                mLineHistory.pop_back();
            }
            else
            {
                LL_WARNS("") << "Last line of history was not blank." << LL_ENDL;
            }
        }

        // Add text to history, ignoring duplicates
        if( mLineHistory.empty() || getText() != mLineHistory.back() )
        {
            mLineHistory.push_back( getText() );
        }

        // Restore the blank line and set mCurrentHistoryLine to point at it
        mLineHistory.push_back( "" );
        mCurrentHistoryLine = mLineHistory.end() - 1;
    }
}

void LLLineEditor::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    updateTextPadding(); // For clamping side-effect.
    setCursor(mCursorPos); // For clamping side-effect.
}

void LLLineEditor::setEnabled(bool enabled)
{
    mReadOnly = !enabled;
    setTabStop(!mReadOnly);
    updateAllowingLanguageInput();
}


void LLLineEditor::setMaxTextLength(S32 max_text_length)
{
    S32 max_len = llmax(0, max_text_length);
    mMaxLengthBytes = max_len;
}

void LLLineEditor::setMaxTextChars(S32 max_text_chars)
{
    S32 max_chars = llmax(0, max_text_chars);
    mMaxLengthChars = max_chars;
}

void LLLineEditor::getTextPadding(S32 *left, S32 *right)
{
    *left = mTextPadLeft;
    *right = mTextPadRight;
}

void LLLineEditor::setTextPadding(S32 left, S32 right)
{
    mTextPadLeft = left;
    mTextPadRight = right;
    updateTextPadding();
}

void LLLineEditor::updateTextPadding()
{
    mTextLeftEdge = llclamp(mTextPadLeft, 0, getRect().getWidth());
    mTextRightEdge = getRect().getWidth() - llclamp(mTextPadRight, 0, getRect().getWidth());
}


void LLLineEditor::setText(const LLStringExplicit &new_text)
{
    setText(new_text, true);
}

void LLLineEditor::setText(const LLStringExplicit &new_text, bool use_size_limit)
{
    // If new text is identical, don't copy and don't move insertion point
    if (mText.getString() == new_text)
    {
        return;
    }

    // Check to see if entire field is selected.
    S32 len = mText.lengthBytes();
    bool all_selected = (len > 0)
        && (( mSelectionStart == 0 && mSelectionEnd == len )
            || ( mSelectionStart == len && mSelectionEnd == 0 ));

    // Do safe truncation so we don't split multi-byte characters
    // also consider entire string selected when mSelectAllonFocusReceived is set on an empty, focused line editor
    all_selected = all_selected || (len == 0 && hasFocus() && mSelectAllonFocusReceived);

    std::string truncated_utf8 = new_text;
    // Whatever the caller was handed -- an object name off the wire, a value
    // out of an asset -- and nothing promises it is UTF-8. The conversion into
    // the UTF-32 this field used to hold ran simdutf and replaced what it
    // rejected; the field holds bytes now and that check went with it. The copy
    // is being made either way, so a clean string costs one validate pass.
    if (!utf8str_is_valid(truncated_utf8))
    {
        truncated_utf8 = utf8str_sanitize(truncated_utf8);
    }
    if (!mAllowEmoji)
    {
        // Cut emoji symbols if exist
        utf8str_remove_emojis(truncated_utf8);
    }
    if (use_size_limit && truncated_utf8.size() > (U32)mMaxLengthBytes)
    {
        // Cut what is being kept, not the argument: the emoji strip and the
        // repair above both happened to the copy, and truncating the original
        // instead would put back whatever they took out.
        truncated_utf8 = utf8str_truncate(truncated_utf8, mMaxLengthBytes);
    }
    mText.assign(truncated_utf8);

    if (use_size_limit && mMaxLengthChars)
    {
        mText.assign(utf8str_symbol_truncate(truncated_utf8, mMaxLengthChars));
    }
    mFontBufferPreSelection.reset();
    mFontBufferSelection.reset();
    mFontBufferPostSelection.reset();

    if (all_selected)
    {
        // ...keep whole thing selected
        selectAll();
    }
    else
    {
        // try to preserve insertion point, but deselect text
        deselect();
    }

    if (mReadOnly)
    {
        // display field, anchor to start so the value isn't scrolled off
        setCursor(0);
    }
    else
    {
        setCursor(llmin(mText.lengthBytes(), getCursor()));
    }

    // Set current history line to end of history.
    if (mLineHistory.empty())
    {
        mCurrentHistoryLine = mLineHistory.end();
    }
    else
    {
        mCurrentHistoryLine = mLineHistory.end() - 1;
    }

    mPrevText = mText;
}


// Picks a new cursor position based on the actual screen size of text being drawn.
void LLLineEditor::setCursorAtLocalPos( S32 local_mouse_x )
{
    S32 cursor_pos = calcCursorPos(local_mouse_x);

    S32 left_pos = llmin( mSelectionStart, cursor_pos );
    S32 length = llabs( mSelectionStart - cursor_pos );

    if (mIsSelecting && !prevalidateInput(std::string_view(mText.getString()).substr(left_pos, length)))
        return;

    setCursor(cursor_pos);
}

bool LLLineEditor::dragSelectCursorTo(S32 local_mouse_x)
{
    S32 new_pos = calcCursorPos(local_mouse_x);
    const S32 old_pos = getCursor();

    // Snap to a grapheme boundary up front. calcCursorPos / byteFromPixelOffset
    // can hand back a position mid-cluster (the past-end-of-text branch);
    // without alignment, the spans_cluster check below would miss and
    // setCursor's clamp-only behaviour would let mid-cluster placements through.
    const std::string& text = mText.getString();
    new_pos = llclamp(new_pos, 0, mText.lengthBytes());
    const bool fwd = new_pos >= old_pos;
    new_pos = (S32)(fwd
        ? utf8str_grapheme_align_forward(text, (size_t)new_pos)
        : utf8str_grapheme_align_backward(text, (size_t)new_pos));

    if (new_pos == old_pos)
        return false;

    const S32 lo = llmin(old_pos, new_pos);
    const S32 hi = llmax(old_pos, new_pos);
    const auto range = utf8str_emoji_range_at(text, (size_t)lo);
    const bool spans_cluster = range.first != range.second
                            && (S32)range.first  == lo
                            && (S32)range.second == hi;
    if (spans_cluster)
    {
        // Use the rendered text (bullets in password mode) so widths match
        // what the mouse is actually hitting on screen.
        std::string            drawn_buffer;
        const std::string_view drawn      = drawnText(drawn_buffer);
        const S32              drawn_lo   = toDrawnOffset((S32)range.first);
        const S32              drawn_hi   = toDrawnOffset((S32)range.second);
        const S32              drawn_left = toDrawnOffset(mScrollHPos);
        const S32 left_offset = drawn_lo - drawn_left;
        const S32 cluster_left_px = mTextLeftEdge
            + (left_offset > 0 ? mGLFont->getWidthBytes(drawn, drawn_left, left_offset) : 0);
        const S32 cluster_pixel_width =
            mGLFont->getWidthBytes(drawn, drawn_lo, drawn_hi - drawn_lo);

        if (ALTextCaret::holdsCluster(local_mouse_x, cluster_left_px,
                                      cluster_pixel_width,
                                      old_pos == (S32)range.second))
        {
            return false;
        }
    }

    if (mIsSelecting)
    {
        const S32 left_pos = llmin(mSelectionStart, new_pos);
        const S32 length = llabs(mSelectionStart - new_pos);
        if (!prevalidateInput(std::string_view(text).substr(left_pos, length)))
            return false;
    }

    setCursor(new_pos);
    return true;
}

void LLLineEditor::setCursor( S32 pos )
{
    S32 old_cursor_pos = getCursor();

    S32 new_pos = llclamp(pos, 0, mText.lengthBytes());
    if (new_pos != old_cursor_pos)
    {
        // Snap onto the nearest grapheme boundary so callers that hand in a
        // mid-cluster offset (autoreplace callbacks, IME caret positions,
        // calculator error positions, anything pre-dating the cluster work)
        // don't park the caret inside a ZWJ family / flag pair / keycap.
        // Mirrors LLTextBase::setCursorPos. No-op for already-aligned values.
        const std::string& text = mText.getString();
        const bool fwd = new_pos >= old_cursor_pos;
        new_pos = (S32)(fwd
            ? utf8str_grapheme_align_forward(text, (size_t)new_pos)
            : utf8str_grapheme_align_backward(text, (size_t)new_pos));
    }
    mCursorPos = new_pos;

    // position of end of next character after cursor
    S32 pixels_after_scroll = findPixelNearestPos();
    if( pixels_after_scroll > mTextRightEdge )
    {
        const std::string& text = mText.getString();
        S32 width_chars_to_left = textWidth(0, mScrollHPos);
        S32 last_visible_char = mGLFont->maxDrawableBytes(text, llmax(0.f, (F32)(mTextRightEdge - mTextLeftEdge + width_chars_to_left)));
        // character immediately to left of cursor should be last one visible (SCROLL_INCREMENT_ADD will scroll in more characters)
        // or first character if cursor is at beginning
        S32 new_last_visible_char = (S32)utf8str_step_grapheme_backward(text, (size_t)getCursor());
        S32 min_scroll = mGLFont->firstDrawableByte(text, (F32)(mTextRightEdge - mTextLeftEdge), new_last_visible_char);
        if (old_cursor_pos == last_visible_char)
        {
            mScrollHPos = llmin(mText.lengthBytes(),
                                llmax(min_scroll, step_chars(text, mScrollHPos, SCROLL_INCREMENT_ADD)));
        }
        else
        {
            mScrollHPos = min_scroll;
        }
    }
    else if (getCursor() < mScrollHPos)
    {
        if (old_cursor_pos == mScrollHPos)
        {
            mScrollHPos = llmax(0, llmin(getCursor(),
                                         step_chars(mText.getString(), mScrollHPos, -SCROLL_INCREMENT_DEL)));
        }
        else
        {
            mScrollHPos = getCursor();
        }
    }
}


void LLLineEditor::setCursorToEnd()
{
    setCursor(mText.lengthBytes());
    deselect();
}

void LLLineEditor::resetScrollPosition()
{
    mScrollHPos = 0;
    // make sure cursor says in visible range
    setCursor(getCursor());
}

bool LLLineEditor::canDeselect() const
{
    return hasSelection();
}

void LLLineEditor::deselect()
{
    mSelectionStart = 0;
    mSelectionEnd = 0;
    mIsSelecting = false;
}


void LLLineEditor::startSelection()
{
    mIsSelecting = true;
    mSelectionStart = getCursor();
    mSelectionEnd = getCursor();
}

void LLLineEditor::endSelection()
{
    if( mIsSelecting )
    {
        mIsSelecting = false;
        mSelectionEnd = getCursor();
    }
}

bool LLLineEditor::canSelectAll() const
{
    return true;
}

void LLLineEditor::selectAll()
{
    if (!prevalidateInput(mText.getString()))
    {
        return;
    }

    mSelectionStart = mText.lengthBytes();
    mSelectionEnd = 0;
    setCursor(mSelectionEnd);
    //mScrollHPos = 0;
    mIsSelecting = true;
    updatePrimary();
}

bool LLLineEditor::getSpellCheck() const
{
    return (LLSpellChecker::getUseSpellCheck()) && (!mReadOnly) && (mSpellCheck);
}

const std::string& LLLineEditor::getSuggestion(U32 index) const
{
    return (index < mSuggestionList.size()) ? mSuggestionList[index] : LLStringUtil::null;
}

U32 LLLineEditor::getSuggestionCount() const
{
    return static_cast<U32>(mSuggestionList.size());
}

void LLLineEditor::replaceWithSuggestion(U32 index)
{
    for (std::list<std::pair<U32, U32> >::const_iterator it = mMisspellRanges.begin(); it != mMisspellRanges.end(); ++it)
    {
        if ( (it->first <= (U32)mCursorPos) && (it->second >= (U32)mCursorPos) )
        {
            std::string suggestion = mSuggestionList[index];
            if (!mAllowEmoji)
            {
                // Cut emoji symbols if exist
                utf8str_remove_emojis(suggestion);
            }
            if (suggestion.empty())
                return;

            deselect();

            // Delete the misspelled word
            mText.erase(it->first, it->second - it->first);

            // Insert the suggestion in its place
            mText.insert(it->first, suggestion);
            setCursor(it->first + (S32)suggestion.size());

            mFontBufferPreSelection.reset();
            mFontBufferSelection.reset();
            mFontBufferPostSelection.reset();

            break;
        }
    }
    mSpellCheckStart = mSpellCheckEnd = -1;
}

void LLLineEditor::addToDictionary()
{
    if (canAddToDictionary())
    {
        LLSpellChecker::instance().addToCustomDictionary(getMisspelledWord(mCursorPos));
    }
}

bool LLLineEditor::canAddToDictionary() const
{
    return (getSpellCheck()) && (isMisspelledWord(mCursorPos));
}

void LLLineEditor::addToIgnore()
{
    if (canAddToIgnore())
    {
        LLSpellChecker::instance().addToIgnoreList(getMisspelledWord(mCursorPos));
    }
}

bool LLLineEditor::canAddToIgnore() const
{
    return (getSpellCheck()) && (isMisspelledWord(mCursorPos));
}

std::string LLLineEditor::getMisspelledWord(U32 pos) const
{
    for (std::list<std::pair<U32, U32> >::const_iterator it = mMisspellRanges.begin(); it != mMisspellRanges.end(); ++it)
    {
        if ( (it->first <= pos) && (it->second >= pos) )
        {
            return mText.getString().substr(it->first, it->second - it->first);
        }
    }
    return LLStringUtil::null;
}

bool LLLineEditor::isMisspelledWord(U32 pos) const
{
    for (std::list<std::pair<U32, U32> >::const_iterator it = mMisspellRanges.begin(); it != mMisspellRanges.end(); ++it)
    {
        if ( (it->first <= pos) && (it->second >= pos) )
        {
            return true;
        }
    }
    return false;
}

void LLLineEditor::onSpellCheckSettingsChange()
{
    // Recheck the spelling on every change
    mMisspellRanges.clear();
    mSpellCheckStart = mSpellCheckEnd = -1;
}

bool LLLineEditor::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    setFocus( true );
    mTripleClickTimer.setTimerExpirySec(TRIPLE_CLICK_INTERVAL);

    if (mSelectionEnd == 0 && mSelectionStart == mText.lengthBytes())
    {
        // if everything is selected, handle this as a normal click to change insertion point
        handleMouseDown(x, y, mask);
    }
    else
    {
        const std::string& text = mText.getString();

        bool doSelectAll = true;

        // If the cursor sits on (or just past) an emoji cluster, prefer
        // selecting that whole cluster — otherwise emojis fall through to
        // selectAll, which is wildly larger than the click implied.
        auto cluster = utf8str_emoji_range_at(text, (size_t)mCursorPos);
        if (cluster.first == cluster.second && mCursorPos > 0)
        {
            // Somewhere inside the character just passed; any position within
            // a cluster reports the whole of it.
            const size_t before = utf8str_grapheme_align_backward(text, (size_t)mCursorPos - 1);
            const auto prev = utf8str_emoji_range_at(text, before);
            if ((S32)prev.second == mCursorPos)
                cluster = prev;
        }

        if (cluster.first != cluster.second)
        {
            const S32 old_selection_start = mLastSelectionStart;
            const S32 old_selection_end = mLastSelectionEnd;

            mCursorPos = (S32)cluster.first;
            startSelection();
            mCursorPos = (S32)cluster.second;
            mSelectionEnd = mCursorPos;

            // Same expand-on-repeat behaviour as the word path below.
            doSelectAll = (old_selection_start == mSelectionStart) &&
                          (old_selection_end   == mSelectionEnd);
        }
        // Select the word we're on, as Unicode bounds it: a contraction comes
        // out whole, and a script without spaces still yields a word rather
        // than the run up to the next punctuation.
        else if (const auto word = utf8str_word_range_at(text, (size_t)mCursorPos);
                 word.first != word.second)
        {
            S32 old_selection_start = mLastSelectionStart;
            S32 old_selection_end = mLastSelectionEnd;

            mCursorPos = (S32)word.first;
            startSelection();
            mCursorPos = (S32)word.second;
            mSelectionEnd = mCursorPos;

            // If nothing changed, then the word was already selected.  Select the whole line.
            doSelectAll = (old_selection_start == mSelectionStart) &&
                          (old_selection_end   == mSelectionEnd);
        }

        if ( doSelectAll )
        {   // Select everything
            selectAll();
        }
    }

    // We don't want handleMouseUp() to "finish" the selection (and thereby
    // set mSelectionEnd to where the mouse is), so we finish the selection
    // here.
    mIsSelecting = false;

    // delay cursor flashing
    mKeystrokeTimer.reset();

    // take selection to 'primary' clipboard
    updatePrimary();

    return true;
}

bool LLLineEditor::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // Check first whether the "clear search" button wants to deal with this.
    if(childrenHandleMouseDown(x, y, mask) != NULL)
    {
        return true;
    }

    if (!mSelectAllonFocusReceived
        || gFocusMgr.getKeyboardFocus() == this)
    {
        mLastSelectionStart = -1;
        mLastSelectionStart = -1;

        if (mask & MASK_SHIFT)
        {
            // assume we're starting a drag select
            mIsSelecting = true;

            // Handle selection extension
            S32 old_cursor_pos = getCursor();
            setCursorAtLocalPos(x);

            if (hasSelection())
            {
                /* Mac-like behavior - extend selection towards the cursor
                if (getCursor() < mSelectionStart
                    && getCursor() < mSelectionEnd)
                {
                    // ...left of selection
                    mSelectionStart = llmax(mSelectionStart, mSelectionEnd);
                    mSelectionEnd = getCursor();
                }
                else if (getCursor() > mSelectionStart
                    && getCursor() > mSelectionEnd)
                {
                    // ...right of selection
                    mSelectionStart = llmin(mSelectionStart, mSelectionEnd);
                    mSelectionEnd = getCursor();
                }
                else
                {
                    mSelectionEnd = getCursor();
                }
                */
                // Windows behavior
                mSelectionEnd = getCursor();
            }
            else
            {
                mSelectionStart = old_cursor_pos;
                mSelectionEnd = getCursor();
            }
        }
        else
        {
            if (mTripleClickTimer.hasExpired())
            {
                // Save selection for word/line selecting on double-click
                mLastSelectionStart = mSelectionStart;
                mLastSelectionEnd = mSelectionEnd;

                // Move cursor and deselect for regular click
                setCursorAtLocalPos( x );
                deselect();
                startSelection();
            }
            else // handle triple click
            {
                selectAll();
                // We don't want handleMouseUp() to "finish" the selection (and thereby
                // set mSelectionEnd to where the mouse is), so we finish the selection
                // here.
                mIsSelecting = false;
            }
        }

        gFocusMgr.setMouseCapture( this );
    }

    setFocus(true);

    // delay cursor flashing
    mKeystrokeTimer.reset();

    if (mMouseDownSignal)
        (*mMouseDownSignal)(this,x,y,mask);

    return true;
}

bool LLLineEditor::handleMiddleMouseDown(S32 x, S32 y, MASK mask)
{
        // LL_INFOS() << "MiddleMouseDown" << LL_ENDL;
    setFocus( true );
    if( canPastePrimary() )
    {
        setCursorAtLocalPos(x);
        pastePrimary();
    }
    return true;
}

bool LLLineEditor::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    setFocus(true);
    if (!LLUICtrl::handleRightMouseDown(x, y, mask) && getShowContextMenu())
    {
        showContextMenu(x, y);
    }
    return true;
}

bool LLLineEditor::handleHover(S32 x, S32 y, MASK mask)
{
    bool handled = false;
    // Check first whether the "clear search" button wants to deal with this.
    if(!hasMouseCapture())
    {
        if(childrenHandleHover(x, y, mask) != NULL)
        {
            return true;
        }
    }

    if( (hasMouseCapture()) && mIsSelecting )
    {
        // Scroll if mouse cursor outside of bounds
        if (mScrollTimer.hasExpired())
        {
            S32 increment = ll_round(mScrollTimer.getElapsedTimeF32() / AUTO_SCROLL_TIME);
            mScrollTimer.reset();
            mScrollTimer.setTimerExpirySec(AUTO_SCROLL_TIME);
            if( (x < mTextLeftEdge) && (mScrollHPos > 0 ) )
            {
                // Scroll to the left. The increment counts characters.
                mScrollHPos = llclamp(step_chars(mText.getString(), mScrollHPos, -increment),
                                      0, mText.lengthBytes());
            }
            else
            if( (x > mTextRightEdge) && (mCursorPos < mText.lengthBytes()) )
            {
                // If scrolling one pixel would make a difference... The offset
                // is a distance in bytes, so one character is the span of the
                // one at the cursor rather than the literal 1.
                const S32 one_char = (S32)utf8str_step_grapheme_forward(
                    mText.getString(), (size_t)getCursor()) - getCursor();
                S32 pixels_after_scrolling_one_char = findPixelNearestPos(one_char);
                if( pixels_after_scrolling_one_char >= mTextRightEdge )
                {
                    // ...scroll to the right
                    mScrollHPos = llclamp(step_chars(mText.getString(), mScrollHPos, increment),
                                          0, mText.lengthBytes());
                }
            }
        }

        dragSelectCursorTo(x);
        mSelectionEnd = getCursor();

        // delay cursor flashing
        mKeystrokeTimer.reset();

        getWindow()->setCursor(UI_CURSOR_IBEAM);
        LL_DEBUGS("UserInput") << "hover handled by " << getName() << " (active)" << LL_ENDL;
        handled = true;
    }

    if( !handled  )
    {
        getWindow()->setCursor(UI_CURSOR_IBEAM);
        LL_DEBUGS("UserInput") << "hover handled by " << getName() << " (inactive)" << LL_ENDL;
        handled = true;
    }

    return handled;
}


bool LLLineEditor::handleMouseUp(S32 x, S32 y, MASK mask)
{
    bool    handled = false;

    if( hasMouseCapture() )
    {
        gFocusMgr.setMouseCapture( NULL );
        handled = true;
    }

    // Check first whether the "clear search" button wants to deal with this.
    if(!handled && childrenHandleMouseUp(x, y, mask) != NULL)
    {
        return true;
    }

    if( mIsSelecting )
    {
        dragSelectCursorTo(x);
        mSelectionEnd = getCursor();

        handled = true;
    }

    if( handled )
    {
        // delay cursor flashing
        mKeystrokeTimer.reset();

        // take selection to 'primary' clipboard
        updatePrimary();
    }

    // We won't call LLUICtrl::handleMouseUp to avoid double calls of  childrenHandleMouseUp().Just invoke the signal manually.
    if (mMouseUpSignal)
        (*mMouseUpSignal)(this,x,y, mask);
    return handled;
}


// Remove a single character from the text
void LLLineEditor::removeChar()
{
    if( getCursor() > 0 )
    {
        const std::string& text = mText.getString();
        const S32 cursor = getCursor();
        const S32 new_cursor = (S32)utf8str_step_grapheme_backward(text, (size_t)cursor);
        const S32 span = cursor - new_cursor;
        if (!prevalidateInput(std::string_view(text).substr(new_cursor, span)))
            return;

        mText.erase(new_cursor, span);

        setCursor(new_cursor);

        mFontBufferPreSelection.reset();
        mFontBufferSelection.reset();
        mFontBufferPostSelection.reset();
    }
    else
    {
        LLUI::getInstance()->reportBadKeystroke();
    }
}

// Remove a word (set of characters up to next space/punctuation) from the text
void LLLineEditor::removeWord(bool prev)
{
    const S32 pos(getCursor());
    if (prev ? pos > 0 : pos < getLengthBytes())
    {
        // The helpers cross a line break themselves, so with the guard above
        // holding they always move. Anything that did not would erase a span of
        // zero and report the key as having worked.
        const S32 new_pos(prev ? prevWordPos(pos) : nextWordPos(pos));
        const S32 diff(llabs(pos - new_pos));
        if (0 == diff)
        {
            LLUI::getInstance()->reportBadKeystroke();
            return;
        }
        if (prev)
        {
            mText.erase(new_pos, diff);
            setCursor(new_pos);
        }
        else
        {
            mText.erase(pos, diff);
        }
        mFontBufferPreSelection.reset();
        mFontBufferSelection.reset();
        mFontBufferPostSelection.reset();
    }
    else
    {
        LLUI::getInstance()->reportBadKeystroke();
    }
}

void LLLineEditor::addChar(const llwchar uni_char)
{
    // The extenders go with the emoji they extend. Characters arrive here one
    // at a time, so rejecting only the emoji leaves a skin tone or a variation
    // selector to land as a mark on whatever preceded it. ZWJ is deliberately
    // not one of them and stays: Indic scripts need it.
    if (!mAllowEmoji
        && (LLStringOps::isEmoji(uni_char) || LLStringOps::isEmojiClusterExtender(uni_char)))
    {
        return;
    }

    llwchar new_c = uni_char;
    if (hasSelection())
    {
        deleteSelection();
    }
    else if (LL_KIM_OVERWRITE == gKeyboard->getInsertMode())
    {
        // Overwrite the whole grapheme cluster, not just one codepoint —
        // otherwise typing over an emoji like 🏳️‍⚧️ peels off only the base
        // (🏳) and leaves the variation selector / ZWJ / payload behind.
        // span == 0 (cursor at end of string) falls through to plain insert.
        const std::string& text = mText.getString();
        const S32 cur = getCursor();
        const S32 span = (S32)utf8str_step_grapheme_forward(text, (size_t)cur) - cur;
        if (span > 0)
        {
            if (!prevalidateInput(std::string_view(text).substr(cur, span)))
                return;

            mText.erase(cur, span);

            mFontBufferPreSelection.reset();
            mFontBufferSelection.reset();
            mFontBufferPostSelection.reset();
        }
    }

    S32 cur_bytes = static_cast<S32>(mText.getString().size());

    S32 new_bytes = wchar_utf8_length(new_c);

    bool allow_char = true;

    // Check byte length limit
    if ((new_bytes + cur_bytes) > mMaxLengthBytes)
    {
        allow_char = false;
    }
    else if (mMaxLengthChars)
    {
        // The limit counts characters, so the text has to be counted in them
        // too rather than measured by the bytes it now stores.
        if ((utf8str_codepoint_count(mText.getString()) + 1) > mMaxLengthChars)
        {
            allow_char = false;
        }
    }

    if (allow_char)
    {
        mText.insert(getCursor(), utf8str_from_cp(new_c));
        setCursor(getCursor() + new_bytes);

        mFontBufferPreSelection.reset();
        mFontBufferSelection.reset();
        mFontBufferPostSelection.reset();
    }
    else
    {
        LLUI::getInstance()->reportBadKeystroke();
    }

    if (!mReadOnly && mAutoreplaceCallback != nullptr)
    {
        // autoreplace the text, if necessary
        S32 replacement_start;
        S32 replacement_length;
        std::string replacement_string;
        S32 new_cursor_pos = mCursorPos;
        mAutoreplaceCallback(replacement_start, replacement_length, replacement_string, new_cursor_pos, getText());

        if (replacement_length > 0 || !replacement_string.empty())
        {
            mText.erase(replacement_start, replacement_length);
            mText.insert(replacement_start, replacement_string);
            setCursor(new_cursor_pos);

            mFontBufferPreSelection.reset();
            mFontBufferSelection.reset();
            mFontBufferPostSelection.reset();
        }
    }

    getWindow()->hideCursorUntilMouseMove();
}

// Extends the selection box to the new cursor position
void LLLineEditor::extendSelection( S32 new_cursor_pos )
{
    if( !mIsSelecting )
    {
        startSelection();
    }

    S32 left_pos = llmin( mSelectionStart, new_cursor_pos );
    S32 selection_length = llabs( mSelectionStart - new_cursor_pos );

    if (!prevalidateInput(std::string_view(mText.getString()).substr(left_pos, selection_length)))
        return;

    setCursor(new_cursor_pos);
    mSelectionEnd = getCursor();
}


void LLLineEditor::setSelection(S32 start, S32 end)
{
    S32 len = mText.lengthBytes();

    mIsSelecting = true;

    // JC, yes, this seems odd, but I think you have to presume a
    // selection dragged from the end towards the start.
    mSelectionStart = llclamp(end, 0, len);
    mSelectionEnd = llclamp(start, 0, len);
    setCursor(start);
}

void LLLineEditor::setDrawAsterixes(bool b)
{
    mDrawAsterixes = b;
    updateAllowingLanguageInput();
}

// A line editor is not guaranteed to hold only one line: pasteHelper
// substitutes a newline, but setText and setValue pass one straight through --
// which is why the caret forms are wanted here too.
S32 LLLineEditor::prevWordPos(S32 cursorPos) const
{
    return (S32)utf8str_caret_word_backward(mText.getString(), (size_t)llmax(cursorPos, 0));
}

S32 LLLineEditor::nextWordPos(S32 cursorPos) const
{
    return (S32)utf8str_caret_word_forward(mText.getString(), (size_t)llmax(cursorPos, 0));
}


bool LLLineEditor::handleSelectionKey(KEY key, MASK mask)
{
    bool handled = false;

    if( mask & MASK_SHIFT )
    {
        handled = true;

        switch( key )
        {
        case KEY_LEFT:
            if( 0 < getCursor() )
            {
                const S32 cursorPos = (mask & MASK_CONTROL)
                    ? prevWordPos(getCursor())
                    : (S32)utf8str_step_grapheme_backward(mText.getString(), (size_t)getCursor());
                extendSelection( cursorPos );
            }
            else
            {
                LLUI::getInstance()->reportBadKeystroke();
            }
            break;

        case KEY_RIGHT:
            if( getCursor() < mText.lengthBytes())
            {
                const S32 cursorPos = (mask & MASK_CONTROL)
                    ? nextWordPos(getCursor())
                    : (S32)utf8str_step_grapheme_forward(mText.getString(), (size_t)getCursor());
                extendSelection( cursorPos );
            }
            else
            {
                LLUI::getInstance()->reportBadKeystroke();
            }
            break;

        case KEY_PAGE_UP:
        case KEY_HOME:
            extendSelection( 0 );
            break;

        case KEY_PAGE_DOWN:
        case KEY_END:
            {
                S32 len = mText.lengthBytes();
                if( len )
                {
                    extendSelection( len );
                }
                break;
            }

        default:
            handled = false;
            break;
        }
    }

    if(handled)
    {
        // take selection to 'primary' clipboard
        updatePrimary();
    }

    return handled;
}

void LLLineEditor::deleteSelection()
{
    if( !mReadOnly && hasSelection() )
    {
        S32 left_pos, selection_length;
        getSelectionRange(&left_pos, &selection_length);

        if (!prevalidateInput(std::string_view(mText.getString()).substr(left_pos, selection_length)))
            return;

        mText.erase(left_pos, selection_length);
        deselect();
        setCursor(left_pos);

        mFontBufferPreSelection.reset();
        mFontBufferSelection.reset();
        mFontBufferPostSelection.reset();
    }
}

bool LLLineEditor::canCut() const
{
    return !mReadOnly && !mDrawAsterixes && hasSelection();
}

// cut selection to clipboard
void LLLineEditor::cut()
{
    if( canCut() )
    {
        S32 left_pos, length;
        getSelectionRange(&left_pos, &length);

        if (!prevalidateInput(std::string_view(mText.getString()).substr(left_pos, length)))
            return;

        // Prepare for possible rollback
        LLLineEditorRollback rollback( this );

        LLClipboard::instance().copyToClipboard( mText.getString(), left_pos, length );
        deleteSelection();

        // Validate new string and rollback the if needed.
        bool need_to_rollback = mPrevalidator && !mPrevalidator.validate(mText.getString());
        if (need_to_rollback)
        {
            rollback.doRollback( this );
            LLUI::getInstance()->reportBadKeystroke();
            mPrevalidator.showLastErrorUsingTimeout();
        }
        else
        {
            onKeystroke();
        }
    }
}

bool LLLineEditor::canCopy() const
{
    return !mDrawAsterixes && hasSelection();
}


// copy selection to clipboard
void LLLineEditor::copy()
{
    if( canCopy() )
    {
        S32 left_pos = llmin( mSelectionStart, mSelectionEnd );
        S32 length = llabs( mSelectionStart - mSelectionEnd );
        LLClipboard::instance().copyToClipboard( mText.getString(), left_pos, length );
    }
}

bool LLLineEditor::canPaste() const
{
    return !mReadOnly && LLClipboard::instance().isTextAvailable();
}

void LLLineEditor::paste()
{
    bool is_primary = false;
    pasteHelper(is_primary);
}

void LLLineEditor::pastePrimary()
{
    bool is_primary = true;
    pasteHelper(is_primary);
}

// paste from primary (is_primary==true) or clipboard (is_primary==false)
void LLLineEditor::pasteHelper(bool is_primary)
{
    bool can_paste_it;
    if (is_primary)
    {
        can_paste_it = canPastePrimary();
    }
    else
    {
        can_paste_it = canPaste();
    }

    if (can_paste_it)
    {
        std::string paste;
        LLClipboard::instance().pasteFromClipboard(paste, is_primary);

        if (!paste.empty())
        {
            if (!mAllowEmoji)
            {
                utf8str_remove_emojis(paste);
            }

            if (!prevalidateInput(paste))
                return;

            // Prepare for possible rollback
            LLLineEditorRollback rollback(this);

            // Delete any selected characters
            if ((!is_primary) && hasSelection())
            {
                deleteSelection();
            }

            // Clean up string (replace tabs and returns and remove characters that our fonts don't support.)
            std::string clean_string(paste);
            LLStringUtil::replaceTabsWithSpaces(clean_string, 1);
            // The paragraph character is two bytes, so this is a substitution
            // rather than the character-for-character swap UTF-32 allowed.
            LLStringUtil::replaceString(clean_string, "\n",
                                        mReplaceNewlinesWithSpaces ? " " : "\xC2\xB6");

            // Insert the string

            // Check to see that the size isn't going to be larger than the max number of bytes.
            // Signed, and floored at zero: the text can already be over the
            // limit -- setText was asked to skip it, or setMaxTextLength
            // lowered it afterwards -- and an unsigned subtraction there wraps
            // to a number no paste can exceed, letting the whole thing in.
            const S32 available_bytes = llmax(0, mMaxLengthBytes - mText.lengthBytes());

            if (available_bytes < (S32)clean_string.size())
            {   // Doesn't all fit. Cut at the budget and back off to a whole
                // character, so the cut cannot land between a letter and its
                // accent or inside a flag.
                clean_string.resize(
                    utf8str_grapheme_align_backward(clean_string, (size_t)available_bytes));
                LLUI::getInstance()->reportBadKeystroke();
            }

            if (mMaxLengthChars)
            {
                // A limit counted in characters, so both sides of the
                // comparison are counted in characters rather than in the
                // bytes the text now stores.
                const S32 available_chars =
                    llmax(0, mMaxLengthChars - (S32)utf8str_codepoint_count(mText.getString()));

                if ((size_t)available_chars < utf8str_codepoint_count(clean_string))
                {
                    // Counted in characters and backed off to a whole one --
                    // which is what setText enforces the same limit with.
                    clean_string = utf8str_symbol_truncate(clean_string, available_chars);
                    LLUI::getInstance()->reportBadKeystroke();
                }
            }

            mText.insert(getCursor(), clean_string);
            setCursor( getCursor() + (S32)clean_string.size() );
            deselect();

            mFontBufferPreSelection.reset();
            mFontBufferSelection.reset();
            mFontBufferPostSelection.reset();

            // Validate new string and rollback the if needed.
            bool need_to_rollback = mPrevalidator && !mPrevalidator.validate(mText.getString());
            if (need_to_rollback)
            {
                rollback.doRollback( this );
                LLUI::getInstance()->reportBadKeystroke();
                mPrevalidator.showLastErrorUsingTimeout();
            }
            else
            {
                onKeystroke();
            }
        }
    }
}

// copy selection to primary
void LLLineEditor::copyPrimary()
{
    if( canCopy() )
    {
        S32 left_pos = llmin( mSelectionStart, mSelectionEnd );
        S32 length = llabs( mSelectionStart - mSelectionEnd );
        LLClipboard::instance().copyToClipboard( mText.getString(), left_pos, length, true);
    }
}

bool LLLineEditor::canPastePrimary() const
{
    return !mReadOnly && LLClipboard::instance().isTextAvailable(true);
}

void LLLineEditor::updatePrimary()
{
    if(canCopy() )
    {
        copyPrimary();
    }
}

bool LLLineEditor::handleSpecialKey(KEY key, MASK mask)
{
    bool handled = false;

    switch( key )
    {
    case KEY_INSERT:
        if (mask == MASK_NONE)
        {
            gKeyboard->toggleInsertMode();
        }

        handled = true;
        break;

    case KEY_BACKSPACE:
        if (!mReadOnly)
        {
            //LL_INFOS() << "Handling backspace" << LL_ENDL;
            if( hasSelection() )
            {
                deleteSelection();
            }
            else
            if( 0 < getCursor() )
            {
                if (mask == MASK_CONTROL)
                    removeWord(true);
                else
                    removeChar();
            }
            else
            {
                LLUI::getInstance()->reportBadKeystroke();
            }
        }
        handled = true;
        break;

    case KEY_DELETE:
        if (!mReadOnly && mask == MASK_CONTROL)
        {
            removeWord(false);
            handled = true;
        }
        break;

    case KEY_PAGE_UP:
    case KEY_HOME:
        if (!mIgnoreArrowKeys)
        {
            setCursor(0);
            handled = true;
        }
        break;

    case KEY_PAGE_DOWN:
    case KEY_END:
        if (!mIgnoreArrowKeys)
        {
            S32 len = mText.lengthBytes();
            if( len )
            {
                setCursor(len);
            }
            handled = true;
        }
        break;

    case KEY_LEFT:
        if (mIgnoreArrowKeys && mask == MASK_NONE)
            break;
        if ((mask & MASK_ALT) == 0)
        {
            if( hasSelection() )
            {
                // Collapse to the left edge of the selection. Matches
                // LLTextEditor's behaviour and avoids the previous
                // `getCursor() - 1` step landing mid-cluster on emoji at
                // selection edges (and going one past the edge on
                // reverse-direction drags).
                setCursor(llmin( mSelectionStart, mSelectionEnd ));
            }
            else
            if( 0 < getCursor() )
            {
                const S32 cursorPos = (mask & MASK_CONTROL)
                    ? prevWordPos(getCursor())
                    : (S32)utf8str_step_grapheme_backward(mText.getString(), (size_t)getCursor());
                setCursor(cursorPos);
            }
            else
            {
                LLUI::getInstance()->reportBadKeystroke();
            }
            handled = true;
        }
        break;

    case KEY_RIGHT:
        if (mIgnoreArrowKeys && mask == MASK_NONE)
            break;
        if ((mask & MASK_ALT) == 0)
        {
            if (hasSelection())
            {
                // Collapse to the right edge of the selection — same fix as
                // KEY_LEFT above.
                setCursor(llmax(mSelectionStart, mSelectionEnd));
            }
            else
            if (getCursor() < mText.lengthBytes())
            {
                const S32 cursorPos = (mask & MASK_CONTROL)
                    ? nextWordPos(getCursor())
                    : (S32)utf8str_step_grapheme_forward(mText.getString(), (size_t)getCursor());
                setCursor(cursorPos);
            }
            else
            {
                LLUI::getInstance()->reportBadKeystroke();
            }
            handled = true;
        }
        break;

    // handle ctrl-uparrow if we have a history enabled line editor.
    case KEY_UP:
        if (mHaveHistory && (!mIgnoreArrowKeys || (MASK_CONTROL == mask)))
        {
            if (mCurrentHistoryLine > mLineHistory.begin())
            {
                mText.assign(*(--mCurrentHistoryLine));
                setCursorToEnd();

                mFontBufferPreSelection.reset();
                mFontBufferSelection.reset();
                mFontBufferPostSelection.reset();
            }
            else
            {
                LLUI::getInstance()->reportBadKeystroke();
            }
            handled = true;
        }
        break;

    // handle [ctrl]-downarrow if we have a history enabled line editor
    case KEY_DOWN:
        if (mHaveHistory  && (!mIgnoreArrowKeys || (MASK_CONTROL == mask)))
        {
            if (!mLineHistory.empty() && mCurrentHistoryLine < mLineHistory.end() - 1)
            {
                mText.assign( *(++mCurrentHistoryLine) );
                setCursorToEnd();

                mFontBufferPreSelection.reset();
                mFontBufferSelection.reset();
                mFontBufferPostSelection.reset();
            }
            else
            {
                LLUI::getInstance()->reportBadKeystroke();
            }
            handled = true;
        }
        break;

    case KEY_RETURN:
        // store sent line in history
        updateHistory();
        break;

    case KEY_ESCAPE:
        if (mRevertOnEsc && mText.getString() != mPrevText)
        {
            setText(mPrevText);
            // Note, don't set handled, still want to loose focus (won't commit becase text is now unchanged)
            if (mKeystrokeOnEsc)
            {
                onKeystroke();
            }
        }
        break;

    default:
        break;
    }

    return handled;
}


bool LLLineEditor::handleKeyHere(KEY key, MASK mask )
{
    bool    handled = false;
    bool    selection_modified = false;

    if ( gFocusMgr.getKeyboardFocus() == this )
    {
        LLLineEditorRollback rollback( this );

        if( !handled )
        {
            handled = handleSelectionKey( key, mask );
            selection_modified = handled;
        }

        // Handle most keys only if the text editor is writeable.
        if ( !mReadOnly )
        {
            if( !handled )
            {
                handled = handleSpecialKey( key, mask );
            }
        }

        if( handled )
        {
            mKeystrokeTimer.reset();

            // Most keystrokes will make the selection box go away, but not all will.
            if( !selection_modified &&
                KEY_SHIFT != key &&
                KEY_CONTROL != key &&
                KEY_ALT != key &&
                KEY_CAPSLOCK != key)
            {
                deselect();
            }

            bool prevalidator_failed = false;

            // If read-only, don't allow changes
            bool need_to_rollback = mReadOnly && (mText.getString() == rollback.getText());

            // Validate new string and rollback the keystroke if needed.
            if (!need_to_rollback && mPrevalidator)
            {
                prevalidator_failed = !mPrevalidator.validate(mText.getString());
                need_to_rollback |= prevalidator_failed;
            }

            if (need_to_rollback)
            {
                rollback.doRollback(this);

                LLUI::getInstance()->reportBadKeystroke();
                if (prevalidator_failed)
                {
                    mPrevalidator.showLastErrorUsingTimeout();
                }
            }

            // Notify owner if requested
            if (!need_to_rollback && handled)
            {
                onKeystroke();
                if ( (!selection_modified) && (KEY_BACKSPACE == key) )
                {
                    mSpellCheckTimer.setTimerExpirySec(SPELLCHECK_DELAY);
                }
            }
        }
    }

    return handled;
}


bool LLLineEditor::handleUnicodeCharHere(llwchar uni_char)
{
    if ((uni_char < 0x20) || (uni_char == 0x7F)) // Control character or DEL
    {
        return false;
    }

    bool    handled = false;

    if ( (gFocusMgr.getKeyboardFocus() == this) && getVisible() && !mReadOnly)
    {
        handled = true;

        LLLineEditorRollback rollback( this );

        if (!prevalidateInput(utf8str_from_cp(uni_char)))
        {
            return handled;
        }

        addChar(uni_char);

        mKeystrokeTimer.reset();

        deselect();

        // Validate new string and rollback the keystroke if needed.
        bool need_to_rollback = mPrevalidator && !mPrevalidator.validate(mText.getString());
        if (need_to_rollback)
        {
            rollback.doRollback( this );

            LLUI::getInstance()->reportBadKeystroke();
            mPrevalidator.showLastErrorUsingTimeout();
        }

        // Notify owner if requested
        if (!need_to_rollback && handled)
        {
            // HACK! The only usage of this callback doesn't do anything with the character.
            // We'll have to do something about this if something ever changes! - Doug
            onKeystroke();

            mSpellCheckTimer.setTimerExpirySec(SPELLCHECK_DELAY);
        }
    }
    return handled;
}


bool LLLineEditor::canDoDelete() const
{
    return ( !mReadOnly && (!mPassDelete || (hasSelection() || (getCursor() < mText.lengthBytes()))) );
}

void LLLineEditor::doDelete()
{
    if (canDoDelete() && mText.lengthBytes() > 0)
    {
        // Prepare for possible rollback
        LLLineEditorRollback rollback( this );

        if (hasSelection())
        {
            deleteSelection();
        }
        else if ( getCursor() < mText.lengthBytes())
        {
            const std::string& text = mText.getString();
            const S32 cursor = getCursor();
            const S32 forward_pos = (S32)utf8str_step_grapheme_forward(text, (size_t)cursor);
            const S32 span = forward_pos - cursor;

            if (!prevalidateInput(std::string_view(text).substr(cursor, span)))
            {
                onKeystroke();
                return;
            }
            // Advance past the cluster, then backspace — removeChar is
            // cluster-aware and will remove the whole span in one shot.
            setCursor(forward_pos);
            removeChar();
        }

        // Validate new string and rollback the if needed.
        bool need_to_rollback = mPrevalidator && !mPrevalidator.validate(mText.getString());
        if (need_to_rollback)
        {
            rollback.doRollback(this);
            LLUI::getInstance()->reportBadKeystroke();
            mPrevalidator.showLastErrorUsingTimeout();
        }
        else
        {
            onKeystroke();

            mSpellCheckTimer.setTimerExpirySec(SPELLCHECK_DELAY);
        }
    }
}


void LLLineEditor::drawBackground()
{
    F32 alpha = getCurrentTransparency();
    if (mUseBgColor)
    {
        gl_rect_2d(getLocalRect(), mBgColor % alpha, true);
    }
    else
    {
        bool has_focus = hasFocus();
        LLUIImage* image;
        if (mReadOnly)
        {
            image = mBgImageDisabled;
        }
        else if (has_focus || mShowImageFocused)
        {
            image = mBgImageFocused;
        }
        else
        {
            image = mBgImage;
        }

        if (!image) return;
        // optionally draw programmatic border
        if (has_focus && mDrawFocusBorder)
        {
            LLColor4 tmp_color = gFocusMgr.getFocusColor();
            tmp_color.setAlpha(alpha);
            image->drawBorder(0, 0, getRect().getWidth(), getRect().getHeight(),
                tmp_color,
                gFocusMgr.getFocusFlashWidth());
        }
        LLColor4 tmp_color = UI_VERTEX_COLOR;
        tmp_color.setAlpha(alpha);
        image->draw(getLocalRect(), tmp_color);
    }
}

//virtual
void LLLineEditor::draw()
{
    F32 alpha = getDrawContext().mAlpha;
    static LLUICachedControl<S32> lineeditor_cursor_thickness ("UILineEditorCursorThickness", 0);
    static LLUICachedControl<F32> preedit_marker_brightness ("UIPreeditMarkerBrightness", 0);
    static LLUICachedControl<S32> preedit_marker_gap ("UIPreeditMarkerGap", 0);
    static LLUICachedControl<S32> preedit_marker_position ("UIPreeditMarkerPosition", 0);
    static LLUICachedControl<S32> preedit_marker_thickness ("UIPreeditMarkerThickness", 0);
    static LLUICachedControl<F32> preedit_standout_brightness ("UIPreeditStandoutBrightness", 0);
    static LLUICachedControl<S32> preedit_standout_gap ("UIPreeditStandoutGap", 0);
    static LLUICachedControl<S32> preedit_standout_position ("UIPreeditStandoutPosition", 0);
    static LLUICachedControl<S32> preedit_standout_thickness ("UIPreeditStandoutThickness", 0);

    // A password field draws bullets in place of the text, and the rest of
    // this function measures and positions against whatever mText holds. A
    // bullet is three bytes whatever character it stands in for, so the
    // offsets have to move into the bullets' own byte space alongside the text
    // itself, and back out again at the end. mPreeditPositions is left alone:
    // updateAllowingLanguageInput refuses the IME to a password field, so one
    // never has a preedit to draw.
    std::string saved_text;
    S32 saved_cursor = 0, saved_selection_start = 0, saved_selection_end = 0, saved_scroll = 0;
    if (mDrawAsterixes)
    {
        saved_text            = mText.getString();
        saved_cursor          = mCursorPos;
        saved_selection_start = mSelectionStart;
        saved_selection_end   = mSelectionEnd;
        saved_scroll          = mScrollHPos;

        mCursorPos      = toDrawnOffset(mCursorPos);
        mSelectionStart = toDrawnOffset(mSelectionStart);
        mSelectionEnd   = toDrawnOffset(mSelectionEnd);
        mScrollHPos     = toDrawnOffset(mScrollHPos);

        std::string buffer;
        mText = std::string(drawnText(buffer));

        // The font buffers were keyed on the real text; the bullet string
        // is a different string in the same object, so drop the cached
        // geometry now and let it rebuild from the drawn text below.
        mFontBufferPreSelection.reset();
        mFontBufferSelection.reset();
        mFontBufferPostSelection.reset();
    }

    S32 text_len = mText.lengthBytes();

    // draw rectangle for the background
    LLRect background( 0, getRect().getHeight(), getRect().getWidth(), 0 );
    background.stretch( -mBorderThickness );

    S32 lineeditor_v_pad = (background.getHeight() - mGLFont->getLineHeight()) / 2;
    if (mSpellCheck)
    {
        lineeditor_v_pad += 1;
    }

    drawBackground();

    // draw text

    // Cursor and selection-highlight extent must match the text's vertical
    // span — not the editor box. text_bottom is the descender baseline that
    // the glyph render uses with valign=BOTTOM, and text occupies exactly
    // [text_bottom, text_bottom + getLineHeight()] in screen space (ceil
    // ascender + ceil descender). Tying the cursor to the box height made
    // the caret extend past the glyphs for fonts with smaller line-height /
    // box-height ratios (Source Sans 3, etc.).
    F32 text_bottom = (F32)background.mBottom + (F32)lineeditor_v_pad;
    S32 cursor_bottom = (S32)text_bottom;
    S32 cursor_top = cursor_bottom + mGLFont->getLineHeight();

    LLColor4 text_color;
    if (!mReadOnly)
    {
        if (!getTentative())
        {
            text_color = mFgColor.get();
        }
        else
        {
            text_color = mTentativeFgColor.get();
        }
    }
    else
    {
        text_color = mReadOnlyFgColor.get();
    }
    text_color.setAlpha(alpha);
    LLColor4 label_color = mTentativeFgColor.get();
    label_color.setAlpha(alpha);

    if (hasPreeditString())
    {
        // Draw preedit markers.  This needs to be before drawing letters.
        for (U32 i = 0; i < mPreeditStandouts.size(); i++)
        {
            const S32 preedit_left = mPreeditPositions[i];
            const S32 preedit_right = mPreeditPositions[i + 1];
            if (preedit_right > mScrollHPos)
            {
                S32 preedit_pixels_left = findPixelNearestPos(llmax(preedit_left, mScrollHPos) - getCursor());
                S32 preedit_pixels_right = llmin(findPixelNearestPos(preedit_right - getCursor()), background.mRight);
                if (preedit_pixels_left >= background.mRight)
                {
                    break;
                }
                if (mPreeditStandouts[i])
                {
                    gl_rect_2d(preedit_pixels_left + preedit_standout_gap,
                        background.mBottom + preedit_standout_position,
                        preedit_pixels_right - preedit_standout_gap - 1,
                        background.mBottom + preedit_standout_position - preedit_standout_thickness,
                        (text_color * preedit_standout_brightness
                         + mPreeditBgColor * (1 - preedit_standout_brightness)).setAlpha(alpha/*1.0f*/));
                }
                else
                {
                    gl_rect_2d(preedit_pixels_left + preedit_marker_gap,
                        background.mBottom + preedit_marker_position,
                        preedit_pixels_right - preedit_marker_gap - 1,
                        background.mBottom + preedit_marker_position - preedit_marker_thickness,
                        (text_color * preedit_marker_brightness
                         + mPreeditBgColor * (1 - preedit_marker_brightness)).setAlpha(alpha/*1.0f*/));
                }
            }
        }
    }

    S32 rendered_text = 0;
    F32 rendered_pixels_right = (F32)mTextLeftEdge;

    if( (gFocusMgr.getKeyboardFocus() == this) && hasSelection() )
    {
        S32 select_left;
        S32 select_right;
        if (mSelectionStart < mSelectionEnd)
        {
            select_left = mSelectionStart;
            select_right = mSelectionEnd;
        }
        else
        {
            select_left = mSelectionEnd;
            select_right = mSelectionStart;
        }

        if( select_left > mScrollHPos )
        {
            // unselected, left side
            rendered_text = mFontBufferPreSelection.renderBytes(
                mGLFont,
                mText.getString(), mScrollHPos,
                rendered_pixels_right, text_bottom,
                text_color,
                LLFontGL::LEFT, LLFontGL::BOTTOM,
                0,
                LLFontGL::NO_SHADOW,
                select_left - mScrollHPos,
                mTextRightEdge - ll_round(rendered_pixels_right),
                &rendered_pixels_right);
        }

        if( (rendered_pixels_right < (F32)mTextRightEdge) && (rendered_text < text_len) )
        {
            LLColor4 color = mHighlightColor;
            color.setAlpha(alpha);
            // selected middle
            S32 width = textWidth(mScrollHPos + rendered_text, select_right - mScrollHPos - rendered_text);
            width = llmin(width, mTextRightEdge - ll_round(rendered_pixels_right));
            gl_rect_2d(ll_round(rendered_pixels_right), cursor_top, ll_round(rendered_pixels_right)+width, cursor_bottom, color);

            rendered_text += mFontBufferSelection.renderBytes(
                mGLFont,
                mText.getString(), mScrollHPos + rendered_text,
                rendered_pixels_right, text_bottom,
                LLColor4::black,
                LLFontGL::LEFT, LLFontGL::BOTTOM,
                0,
                LLFontGL::NO_SHADOW,
                select_right - mScrollHPos - rendered_text,
                mTextRightEdge - ll_round(rendered_pixels_right),
                &rendered_pixels_right);
        }

        if( (rendered_pixels_right < (F32)mTextRightEdge) && (rendered_text < text_len) )
        {
            // unselected, right side
            rendered_text += mFontBufferPostSelection.renderBytes(
                mGLFont,
                mText.getString(), mScrollHPos + rendered_text,
                rendered_pixels_right, text_bottom,
                text_color,
                LLFontGL::LEFT, LLFontGL::BOTTOM,
                0,
                LLFontGL::NO_SHADOW,
                S32_MAX,
                mTextRightEdge - ll_round(rendered_pixels_right),
                &rendered_pixels_right);
        }
    }
    else
    {
        rendered_text = mFontBufferPreSelection.renderBytes(
            mGLFont,
            mText.getString(), mScrollHPos,
            rendered_pixels_right, text_bottom,
            text_color,
            LLFontGL::LEFT, LLFontGL::BOTTOM,
            0,
            LLFontGL::NO_SHADOW,
            S32_MAX,
            mTextRightEdge - ll_round(rendered_pixels_right),
            &rendered_pixels_right);
    }
#if 1 // for when we're ready for image art.
    mBorder->setVisible(false); // no more programmatic art.
#endif

    if ( (getSpellCheck()) && (mText.lengthBytes() > 2) )
    {
        // Calculate start and end indices for the first and last visible word
        U32 start = prevWordPos(mScrollHPos), end = nextWordPos(mScrollHPos + rendered_text);

        if ( (mSpellCheckStart != start) || (mSpellCheckEnd != end) )
        {
            const std::string text = mText.getString().substr(start, end - start);

            // Iterate over all words in the text block and check them one by
            // one. Word bounds come from Unicode, so a contraction arrives
            // whole -- the loop this replaced re-derived that rule by hand,
            // testing the characters either side of an apostrophe.
            mMisspellRanges.clear();
            size_t word_at = 0;
            while (word_at < text.length())
            {
                const auto word_range = utf8str_next_word_range(text, word_at);
                if (word_range.first >= word_range.second)
                {
                    break;
                }

                // Don't process words shorter than 3 characters
                std::string word = text.substr(word_range.first, word_range.second - word_range.first);
                if ( (word.length() >= 3) && (!LLSpellChecker::instance().checkSpelling(word)) )
                {
                    mMisspellRanges.push_back(std::pair<U32, U32>(
                        start + (U32)word_range.first, start + (U32)word_range.second));
                }

                word_at = word_range.second;
            }

            mSpellCheckStart = start;
            mSpellCheckEnd = end;
        }

        // Draw squiggly lines under any (visible) misspelled words
        for (std::list<std::pair<U32, U32> >::const_iterator it = mMisspellRanges.begin(); it != mMisspellRanges.end(); ++it)
        {
            // Skip over words that aren't (partially) visible
            if ( ((it->first < start) && (it->second < start)) || (it->first > end) )
            {
                continue;
            }

            // Skip the current word if the user is still busy editing it
            if ( (!mSpellCheckTimer.hasExpired()) && (it->first <= (U32)mCursorPos) && (it->second >= (U32)mCursorPos) )
            {
                continue;
            }

            S32 pxWidth = getRect().getWidth();
            S32 pxStart = findPixelNearestPos(it->first - getCursor());
            if (pxStart > pxWidth)
            {
                continue;
            }
            S32 pxEnd = findPixelNearestPos(it->second - getCursor());
            if (pxEnd > pxWidth)
            {
                pxEnd = pxWidth;
            }

            S32 pxBottom = (S32)(text_bottom + mGLFont->getDescenderHeight());

            gGL.color4ub(255, 0, 0, 200);
            while (pxStart + 1 < pxEnd)
            {
                gl_line_2d(pxStart, pxBottom, pxStart + 2, pxBottom - 2);
                if (pxStart + 3 < pxEnd)
                {
                    gl_line_2d(pxStart + 2, pxBottom - 3, pxStart + 4, pxBottom - 1);
                }
                pxStart += 4;
            }
        }
    }

    // If we're editing...
    if( hasFocus())
    {
        //mBorder->setVisible(true); // ok, programmer art just this once.
        // (Flash the cursor every half second)
        if (!mReadOnly && gFocusMgr.getAppHasFocus())
        {
            F32 elapsed = mKeystrokeTimer.getElapsedTimeF32();
            if( (elapsed < CURSOR_FLASH_DELAY ) || (S32(elapsed * 2) & 1) )
            {
                S32 cursor_left = findPixelNearestPos();
                cursor_left -= lineeditor_cursor_thickness / 2;
                S32 cursor_right = cursor_left + lineeditor_cursor_thickness;
                if (LL_KIM_OVERWRITE == gKeyboard->getInsertMode() && !hasSelection())
                {
                    S32 wswidth = mGLFont->getWidthBytes(" ", 0, S32_MAX);
                    const S32 cursor_span = (S32)utf8str_step_grapheme_forward(
                        mText.getString(), (size_t)getCursor()) - getCursor();
                    S32 width = textWidth(getCursor(), cursor_span) + 1;
                    cursor_right = cursor_left + llmax(wswidth, width);
                }
                // Use same color as text for the Cursor
                gl_rect_2d(cursor_left, cursor_top,
                    cursor_right, cursor_bottom, text_color);
                if (LL_KIM_OVERWRITE == gKeyboard->getInsertMode() && !hasSelection())
                {
                    LLColor4 tmp_color( 1.f - text_color.mV[0], 1.f - text_color.mV[1], 1.f - text_color.mV[2], alpha );
                    const S32 cursor_span = (S32)utf8str_step_grapheme_forward(
                        mText.getString(), (size_t)getCursor()) - getCursor();
                    mGLFont->renderBytes(mText.getString(), getCursor(), (F32)(cursor_left + lineeditor_cursor_thickness / 2), text_bottom,
                        tmp_color,
                        LLFontGL::LEFT, LLFontGL::BOTTOM,
                        0,
                        LLFontGL::NO_SHADOW,
                        cursor_span);
                }

                // Make sure the IME is in the right place
                S32 pixels_after_scroll = findPixelNearestPos();    // RCalculcate for IME position
                LLRect screen_pos = calcScreenRect();
                LLCoordGL ime_pos( screen_pos.mLeft + pixels_after_scroll, screen_pos.mTop - lineeditor_v_pad );

                ime_pos.mX = (S32) (ime_pos.mX * LLUI::getScaleFactor().mV[VX]);
                ime_pos.mY = (S32) (ime_pos.mY * LLUI::getScaleFactor().mV[VY]);
                getWindow()->setLanguageTextInput( ime_pos );
            }
        }

        //draw label if no text is provided
        //but we should draw it in a different color
        //to give indication that it is not text you typed in
        if (0 == mText.lengthBytes() && (mReadOnly || mShowLabelFocused))
        {
            mFontBufferLabel.renderBytes(mGLFont,
                            mLabel.getString(), 0,
                            (F32)mTextLeftEdge, (F32)text_bottom,
                            label_color,
                            LLFontGL::LEFT,
                            LLFontGL::BOTTOM,
                            0,
                            LLFontGL::NO_SHADOW,
                            S32_MAX,
                            mTextRightEdge - ll_round(rendered_pixels_right),
                            &rendered_pixels_right, false);
        }


        // Draw children (border)
        //mBorder->setVisible(true);
        mBorder->setKeyboardFocusHighlight( true );
        LLView::draw();
        mBorder->setKeyboardFocusHighlight( false );
        //mBorder->setVisible(false);
    }
    else // does not have keyboard input
    {
        // draw label if no text provided
        if (0 == mText.lengthBytes())
        {
            mFontBufferLabel.renderBytes(mGLFont,
                            mLabel.getString(), 0,
                            (F32)mTextLeftEdge, (F32)text_bottom,
                            label_color,
                            LLFontGL::LEFT,
                            LLFontGL::BOTTOM,
                            0,
                            LLFontGL::NO_SHADOW,
                            S32_MAX,
                            mTextRightEdge - ll_round(rendered_pixels_right),
                            &rendered_pixels_right);
        }
        // Draw children (border)
        LLView::draw();
    }

    if (mDrawAsterixes)
    {
        mText           = saved_text;
        mCursorPos      = saved_cursor;
        mSelectionStart = saved_selection_start;
        mSelectionEnd   = saved_selection_end;
        mScrollHPos     = saved_scroll;

        // Real text is back; drop bullet geometry so the next width
        // measurement or draw uses the correct string.
        mFontBufferPreSelection.reset();
        mFontBufferSelection.reset();
        mFontBufferPostSelection.reset();
    }
}


std::string_view LLLineEditor::drawnText(std::string& buffer) const
{
    if (!mDrawAsterixes)
    {
        return mText.getString();
    }

    const size_t chars = utf8str_codepoint_count(mText.getString());
    buffer.clear();
    buffer.reserve(chars * PASSWORD_ASTERISK.size());
    for (size_t i = 0; i < chars; ++i)
    {
        buffer += PASSWORD_ASTERISK;
    }
    return buffer;
}

S32 LLLineEditor::toDrawnOffset(S32 text_offset) const
{
    if (!mDrawAsterixes)
    {
        return text_offset;
    }

    const std::string& text = mText.getString();
    const size_t clamped = llmin((size_t)llmax(0, text_offset), text.size());
    return (S32)(utf8str_codepoint_count(std::string_view(text).substr(0, clamped))
                 * PASSWORD_ASTERISK.size());
}

S32 LLLineEditor::fromDrawnOffset(S32 drawn_offset) const
{
    if (!mDrawAsterixes)
    {
        return drawn_offset;
    }

    return (S32)utf8str_offset_from_codepoint_index(
        mText.getString(), (size_t)llmax(0, drawn_offset) / PASSWORD_ASTERISK.size());
}

// Returns the local screen space X coordinate associated with the text cursor position.
// `cursor_offset` is a distance in bytes from the cursor, as every offset here is.
S32 LLLineEditor::textWidth(S32 offset, S32 max_bytes) const
{
    const std::string& text = mText.getString();
    mFontBufferPreSelection.setSource(&mText, mText.getGeneration());
    return llceil(mFontBufferPreSelection.getWidthBytes(mGLFont, text, offset, max_bytes, false));
}

S32 LLLineEditor::findPixelNearestPos(const S32 cursor_offset) const
{
    S32 dpos = getCursor() - mScrollHPos + cursor_offset;
    S32 result = textWidth(mScrollHPos, dpos) + mTextLeftEdge;
    return result;
}

S32 LLLineEditor::calcCursorPos(S32 mouse_x)
{
    std::string            buffer;
    const std::string_view drawn        = drawnText(buffer);
    const S32              drawn_scroll = toDrawnOffset(mScrollHPos);

    const S32 drawn_pos = drawn_scroll +
            mGLFont->byteFromPixelOffset(
                drawn, drawn_scroll,
                (F32)(mouse_x - mTextLeftEdge),
                (F32)(mTextRightEdge - mTextLeftEdge + 1)); // min-max range is inclusive

    return fromDrawnOffset(drawn_pos);
}
//virtual
void LLLineEditor::clear()
{
    mText.clear();
    setCursor(0);
    mFontBufferPreSelection.reset();
    mFontBufferSelection.reset();
    mFontBufferPostSelection.reset();
}

//virtual
void LLLineEditor::onTabInto()
{
    selectAll();
    LLUICtrl::onTabInto();
}

//virtual
bool LLLineEditor::acceptsTextInput() const
{
    return true;
}

// Start or stop the editor from accepting text-editing keystrokes
void LLLineEditor::setFocus( bool new_state )
{
    bool old_state = hasFocus();

    if (!new_state)
    {
        getWindow()->allowLanguageTextInput(this, false);
    }


    // getting focus when we didn't have it before, and we want to select all
    if (!old_state && new_state && mSelectAllonFocusReceived)
    {
        selectAll();
        // We don't want handleMouseUp() to "finish" the selection (and thereby
        // set mSelectionEnd to where the mouse is), so we finish the selection
        // here.
        mIsSelecting = false;
    }

    if( new_state )
    {
        gEditMenuHandler = this;

        // Don't start the cursor flashing right away
        mKeystrokeTimer.reset();
    }
    else
    {
        // Not really needed, since loss of keyboard focus should take care of this,
        // but limited paranoia is ok.
        if( gEditMenuHandler == this )
        {
            gEditMenuHandler = NULL;
        }

        endSelection();
    }

    LLUICtrl::setFocus( new_state );

    if (new_state)
    {
        // Allow Language Text Input only when this LineEditor has
        // no prevalidate function attached.  This criterion works
        // fine on 1.15.0.2, since all prevalidate func reject any
        // non-ASCII characters.  I'm not sure on future versions,
        // however.
        getWindow()->allowLanguageTextInput(this, !mPrevalidator);
    }
}

//virtual
void LLLineEditor::setRect(const LLRect& rect)
{
    LLUICtrl::setRect(rect);
    if (mBorder)
    {
        LLRect border_rect = mBorder->getRect();
        // Scalable UI somehow made these rectangles off-by-one.
        // I don't know why. JC
        border_rect.setOriginAndSize(border_rect.mLeft, border_rect.mBottom,
                rect.getWidth()-1, rect.getHeight()-1);
        mBorder->setRect(border_rect);
    }
}

void LLLineEditor::setPrevalidate(LLTextValidate::Validator validator)
{
    mPrevalidator = validator;
    updateAllowingLanguageInput();
}

void LLLineEditor::setPrevalidateInput(LLTextValidate::Validator validator)
{
    mInputPrevalidator = validator;
    updateAllowingLanguageInput();
}

bool LLLineEditor::prevalidateInput(std::string_view str)
{
    return mInputPrevalidator.validate(str);
}

// static
bool LLLineEditor::postvalidateFloat(const std::string &str)
{
    LLLocale locale(LLLocale::USER_LOCALE);

    bool success = true;
    bool has_decimal = false;
    bool has_digit = false;

    std::string trimmed(str);
    LLStringUtil::trim(trimmed);
    if( !trimmed.empty() )
    {
        // First character can be a negative sign
        size_t i = ('-' == trimmed.front()) ? 1 : 0;

        // May be a comma or period, depending on the locale
        const llwchar decimal_point = (llwchar)(unsigned char)LLResMgr::getInstance()->getDecimalPoint();

        // A character at a time: LLStringOps classifies codepoints, and one
        // byte of a multi-byte character is not one of those.
        while( i < trimmed.size() )
        {
            const LLCodepointAt at = utf8str_decode_at(trimmed, i);
            i = at.next;

            if( decimal_point == at.cp )
            {
                if( has_decimal )
                {
                    // can't have two
                    success = false;
                    break;
                }
                else
                {
                    has_decimal = true;
                }
            }
            else
            if( LLStringOps::isDigit( at.cp ) )
            {
                has_digit = true;
            }
            else
            {
                success = false;
                break;
            }
        }
    }

    // Gotta have at least one
    success = has_digit;

    return success;
}

bool LLLineEditor::evaluateFloat()
{
    bool success;
    F32 result = 0.f;
    std::string expr = getText();
    LLStringUtil::toUpper(expr);

    success = LLCalc::getInstance()->evalString(expr, result);

    if (!success)
    {
        // Move the cursor to near the error on failure
        setCursor(static_cast<S32>(LLCalc::getInstance()->getLastErrorPos()));
        // *TODO: Translated error message indicating the type of error? Select error text?
    }
    else
    {
        // Replace the expression with the result
        std::string result_str = llformat("%f",result);
        setText(result_str);
        selectAll();
    }

    return success;
}

void LLLineEditor::onMouseCaptureLost()
{
    endSelection();
}


void LLLineEditor::setSelectAllonFocusReceived(bool b)
{
    mSelectAllonFocusReceived = b;
}

void LLLineEditor::onKeystroke()
{
    if (mKeystrokeCallback)
    {
        mKeystrokeCallback(this);
    }

    mSpellCheckStart = mSpellCheckEnd = -1;
}

void LLLineEditor::setKeystrokeCallback(callback_t callback, void* user_data)
{
    mKeystrokeCallback = boost::bind(callback, _1, user_data);
}


bool LLLineEditor::setTextArg( const std::string& key, const LLStringExplicit& text )
{
    mText.setArg(key, text);
    mFontBufferPreSelection.reset();
    mFontBufferSelection.reset();
    mFontBufferPostSelection.reset();
    return true;
}

bool LLLineEditor::setLabelArg( const std::string& key, const LLStringExplicit& text )
{
    mLabel.setArg(key, text);
    mFontBufferLabel.reset();
    return true;
}


void LLLineEditor::updateAllowingLanguageInput()
{
    // Allow Language Text Input only when this LineEditor has
    // no prevalidate function attached (as long as other criteria
    // common to LLTextEditor).  This criterion works
    // fine on 1.15.0.2, since all prevalidate func reject any
    // non-ASCII characters.  I'm not sure on future versions,
    // however...
    LLWindow* window = getWindow();
    if (!window)
    {
        // test app, no window available
        return;
    }
    if (hasFocus() && !mReadOnly && !mDrawAsterixes && !mPrevalidator)
    {
        window->allowLanguageTextInput(this, true);
    }
    else
    {
        window->allowLanguageTextInput(this, false);
    }
}

bool LLLineEditor::hasPreeditString() const
{
    return (mPreeditPositions.size() > 1);
}

void LLLineEditor::resetPreedit()
{
    if (hasSelection())
    {
        if (hasPreeditString())
        {
            LL_WARNS() << "Preedit and selection!" << LL_ENDL;
            deselect();
        }
        else
        {
            deleteSelection();
        }
    }
    if (hasPreeditString())
    {
        const S32 preedit_pos = mPreeditPositions.front();
        const S32 end = mPreeditPositions.back();
        const S32 len = end - preedit_pos;
        const S32 size = mText.lengthBytes();
        if (preedit_pos < size
            && end <= size
            && preedit_pos >= 0
            && len > 0)
        {
            mText.erase(preedit_pos, len);
            mText.insert(preedit_pos, mPreeditOverwrittenString);
            setCursor(preedit_pos);
        }
        else
        {
            LL_WARNS() << "Index out of bounds. Start: " << preedit_pos
                << ", end:" << end
                << ", full string length: " << size << LL_ENDL;
        }

        mPreeditString.clear();
        mPreeditOverwrittenString.clear();
        mPreeditPositions.clear();

        // Don't reset key stroke timer nor invoke keystroke callback,
        // because a call to updatePreedit should be follow soon in
        // normal course of operation, and timer and callback will be
        // maintained there.  Doing so here made an odd sound.  (VWR-3410)
    }
}

void LLLineEditor::updatePreedit(std::string_view preedit_string,
        const segment_lengths_t &preedit_segment_lengths, const standouts_t &preedit_standouts, S32 caret_position)
{
    // Just in case.
    if (mReadOnly)
    {
        return;
    }

    // Note that call to updatePreedit is always preceeded by resetPreedit,
    // so we have no existing selection/preedit.

    S32 insert_preedit_at = getCursor();

    mPreeditString.assign(preedit_string);
    mPreeditPositions.resize(preedit_segment_lengths.size() + 1);
    S32 position = insert_preedit_at;
    for (segment_lengths_t::size_type i = 0; i < preedit_segment_lengths.size(); i++)
    {
        mPreeditPositions[i] = position;
        position += llmax(0, preedit_segment_lengths[i]);
    }
    mPreeditPositions.back() = position;
    if (LL_KIM_OVERWRITE == gKeyboard->getInsertMode())
    {
        // As much of the text as the preedit will cover, backed off to a whole
        // character so the overwrite cannot end inside one.
        const S32 overwritten = (S32)utf8str_grapheme_align_backward(
            mText.getString(),
            llmin((size_t)insert_preedit_at + preedit_string.length(),
                  (size_t)mText.lengthBytes())) - insert_preedit_at;
        mPreeditOverwrittenString = mText.getString().substr(insert_preedit_at, overwritten);
        mText.erase(insert_preedit_at, overwritten);
    }
    else
    {
        mPreeditOverwrittenString.clear();
    }
    mText.insert(insert_preedit_at, mPreeditString);
    mFontBufferPreSelection.reset();
    mFontBufferSelection.reset();
    mFontBufferPostSelection.reset();

    mPreeditStandouts = preedit_standouts;

    setCursor(position);
    setCursor(mPreeditPositions.front() + llmax(0, caret_position));

    // Update of the preedit should be caused by some key strokes.
    mKeystrokeTimer.reset();
    onKeystroke();

    mSpellCheckTimer.setTimerExpirySec(SPELLCHECK_DELAY);
}

bool LLLineEditor::getPreeditLocation(S32 query_offset, LLCoordGL *coord, LLRect *bounds, LLRect *control) const
{
    if (control)
    {
        LLRect control_rect_screen;
        localRectToScreen(getRect(), &control_rect_screen);
        LLUI::getInstance()->screenRectToGL(control_rect_screen, control);
    }

    S32 preedit_left_column, preedit_right_column;
    if (hasPreeditString())
    {
        preedit_left_column = mPreeditPositions.front();
        preedit_right_column = mPreeditPositions.back();
    }
    else
    {
        preedit_left_column = preedit_right_column = getCursor();
    }
    if (preedit_right_column < mScrollHPos)
    {
        // This should not occure...
        return false;
    }

    const S32 query = (query_offset >= 0 ? preedit_left_column + query_offset : getCursor());
    if (query < mScrollHPos || query < preedit_left_column || query > preedit_right_column)
    {
        return false;
    }

    if (coord)
    {
        S32 query_local = findPixelNearestPos(query - getCursor());
        S32 query_screen_x, query_screen_y;
        localPointToScreen(query_local, getRect().getHeight() / 2, &query_screen_x, &query_screen_y);
        LLUI::getInstance()->screenPointToGL(query_screen_x, query_screen_y, &coord->mX, &coord->mY);
    }

    if (bounds)
    {
        S32 preedit_left_local = findPixelNearestPos(llmax(preedit_left_column, mScrollHPos) - getCursor());
        S32 preedit_right_local = llmin(findPixelNearestPos(preedit_right_column - getCursor()), getRect().getWidth() - mBorderThickness);
        if (preedit_left_local > preedit_right_local)
        {
            // Is this condition possible?
            preedit_right_local = preedit_left_local;
        }

        LLRect preedit_rect_local(preedit_left_local, getRect().getHeight(), preedit_right_local, 0);
        LLRect preedit_rect_screen;
        localRectToScreen(preedit_rect_local, &preedit_rect_screen);
        LLUI::getInstance()->screenRectToGL(preedit_rect_screen, bounds);
    }

    return true;
}

void LLLineEditor::getSelectionRange(S32 *position, S32 *length) const
{
    if (hasSelection())
    {
        *position = llmin(mSelectionStart, mSelectionEnd);
        *length = llabs(mSelectionStart - mSelectionEnd);
    }
    else
    {
        *position = mCursorPos;
        *length = 0;
    }
}

void LLLineEditor::getPreeditRange(S32 *position, S32 *length) const
{
    if (hasPreeditString())
    {
        *position = mPreeditPositions.front();
        *length   = mPreeditPositions.back() - mPreeditPositions.front();
    }
    else
    {
        *position = mCursorPos;
        *length   = 0;
    }
}

void LLLineEditor::markAsPreedit(S32 position, S32 length)
{
    const S32 begin = position;
    const S32 end   = position + length;

    deselect();
    setCursor(begin);
    if (hasPreeditString())
    {
        LL_WARNS() << "markAsPreedit invoked when hasPreeditString is true." << LL_ENDL;
    }
    mPreeditString = mText.getString().substr(begin, end - begin);
    if (end > begin)
    {
        mPreeditPositions.resize(2);
        mPreeditPositions[0] = begin;
        mPreeditPositions[1] = end;
        mPreeditStandouts.resize(1);
        mPreeditStandouts[0] = false;
    }
    else
    {
        mPreeditPositions.clear();
        mPreeditStandouts.clear();
    }
    if (LL_KIM_OVERWRITE == gKeyboard->getInsertMode())
    {
        mPreeditOverwrittenString = mPreeditString;
    }
    else
    {
        mPreeditOverwrittenString.clear();
    }
}

S32 LLLineEditor::getPreeditFontSize() const
{
    return ll_round(mGLFont->getLineHeight() * LLUI::getScaleFactor().mV[VY]);
}

void LLLineEditor::setReplaceNewlinesWithSpaces(bool replace)
{
    mReplaceNewlinesWithSpaces = replace;
}

std::string LLLineEditor::getConvertedText() const
{
    std::string text = getText();
    LLStringUtil::trim(text);
    if (!mReplaceNewlinesWithSpaces)
    {
        // Convert paragraph symbols back into newlines. Two bytes to one, so
        // a substitution rather than a character-for-character swap.
        LLStringUtil::replaceString(text, "\xC2\xB6", "\n");
    }
    return text;
}

void LLLineEditor::showContextMenu(S32 x, S32 y)
{
    LLContextMenu* menu = static_cast<LLContextMenu*>(mContextMenuHandle.get());
    if (!menu)
    {
        llassert(LLMenuGL::sMenuContainer != NULL);
        menu = LLUICtrlFactory::createFromFile<LLContextMenu>
            ("menu_text_editor.xml",
                LLMenuGL::sMenuContainer,
                LLMenuHolderGL::child_registry_t::instance());
        setContextMenu(menu);
    }

    if (menu)
    {
        gEditMenuHandler = this;

        S32 screen_x, screen_y;
        localPointToScreen(x, y, &screen_x, &screen_y);

        setCursorAtLocalPos(x);
        if (hasSelection())
        {
            if ( (mCursorPos < llmin(mSelectionStart, mSelectionEnd)) || (mCursorPos > llmax(mSelectionStart, mSelectionEnd)) )
            {
                deselect();
            }
            else
            {
                setCursor(llmax(mSelectionStart, mSelectionEnd));
            }
        }

        bool use_spellcheck = getSpellCheck(), is_misspelled = false;
        if (use_spellcheck)
        {
            mSuggestionList.clear();

            // If the cursor is on a misspelled word, retrieve suggestions for it
            std::string misspelled_word = getMisspelledWord(mCursorPos);
            if ((is_misspelled = !misspelled_word.empty()))
            {
                LLSpellChecker::instance().getSuggestions(misspelled_word, mSuggestionList);
            }
        }

        menu->setItemVisible("Suggestion Separator", (use_spellcheck) && (!mSuggestionList.empty()));
        menu->setItemVisible("Add to Dictionary", (use_spellcheck) && (is_misspelled));
        menu->setItemVisible("Add to Ignore", (use_spellcheck) && (is_misspelled));
        menu->setItemVisible("Spellcheck Separator", (use_spellcheck) && (is_misspelled));
        menu->show(screen_x, screen_y, this);
    }
}

void LLLineEditor::setContextMenu(LLContextMenu* new_context_menu)
{
    LLContextMenu* menu = static_cast<LLContextMenu*>(mContextMenuHandle.get());
    if (menu)
    {
        menu->die();
        mContextMenuHandle.markDead();
    }

    if (new_context_menu)
    {
        mContextMenuHandle = new_context_menu->getHandle();
    }
}

void LLLineEditor::setFont(const LLFontGL* font)
{
    mGLFont = font;
}
