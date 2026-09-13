/**
 * @file llfocusmgr.cpp
 * @brief LLFocusMgr base class
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "linden_common.h"

#include "llfocusmgr.h"
#include "lluictrl.h"
#include "v4color.h"

const F32 FOCUS_FADE_TIME = 0.3f;

LLFocusableElement::LLFocusableElement()
{
}

LLFocusableElement::FocusCallbacks::~FocusCallbacks()
{
    auto free_signal = [](focus_signal_t*& signal)
        {
            if (signal)
            {
                signal->disconnect_all_slots();
                delete signal;
                signal = nullptr;
            }
        };

    free_signal(mFocusLost);
    free_signal(mFocusReceived);
    free_signal(mFocusChanged);
    free_signal(mTopLost);
}

LLFocusableElement::FocusCallbacks& LLFocusableElement::focusCallbacks()
{
    if (!mFocusCallbacks)
    {
        mFocusCallbacks = new FocusCallbacks();
    }
    return *mFocusCallbacks;
}

// virtual
bool LLFocusableElement::handleKey(KEY key, MASK mask, bool called_from_parent)
{
    return false;
}

// virtual
bool LLFocusableElement::handleKeyUp(KEY key, MASK mask, bool called_from_parent)
{
    return false;
}

// virtual
bool LLFocusableElement::handleUnicodeChar(llwchar uni_char, bool called_from_parent)
{
    return false;
}

// virtual
bool LLFocusableElement::wantsKeyUpKeyDown() const
{
    return false;
}

//virtual
bool LLFocusableElement::wantsReturnKey() const
{
    return false;
}

// virtual
LLFocusableElement::~LLFocusableElement()
{
    delete mFocusCallbacks;
    mFocusCallbacks = nullptr;
}

void LLFocusableElement::onFocusReceived()
{
    if (!mFocusCallbacks) return;
    if (mFocusCallbacks->mFocusReceived) (*mFocusCallbacks->mFocusReceived)(this);
    if (mFocusCallbacks->mFocusChanged) (*mFocusCallbacks->mFocusChanged)(this);
}

void LLFocusableElement::onFocusLost()
{
    if (!mFocusCallbacks) return;
    if (mFocusCallbacks->mFocusLost) (*mFocusCallbacks->mFocusLost)(this);
    if (mFocusCallbacks->mFocusChanged) (*mFocusCallbacks->mFocusChanged)(this);
}

void LLFocusableElement::onTopLost()
{
    if (mFocusCallbacks && mFocusCallbacks->mTopLost) (*mFocusCallbacks->mTopLost)(this);
}

bool LLFocusableElement::hasFocus() const
{
    return gFocusMgr.getKeyboardFocus() == this;
}

void LLFocusableElement::setFocus(bool b)
{
}

boost::signals2::connection LLFocusableElement::setFocusLostCallback( const focus_signal_t::slot_type& cb)
{
    focus_signal_t*& signal = focusCallbacks().mFocusLost;
    if (!signal) signal = new focus_signal_t();
    return signal->connect(cb);
}

boost::signals2::connection LLFocusableElement::setFocusReceivedCallback(const focus_signal_t::slot_type& cb)
{
    focus_signal_t*& signal = focusCallbacks().mFocusReceived;
    if (!signal) signal = new focus_signal_t();
    return signal->connect(cb);
}

boost::signals2::connection LLFocusableElement::setFocusChangedCallback(const focus_signal_t::slot_type& cb)
{
    focus_signal_t*& signal = focusCallbacks().mFocusChanged;
    if (!signal) signal = new focus_signal_t();
    return signal->connect(cb);
}

boost::signals2::connection LLFocusableElement::setTopLostCallback(const focus_signal_t::slot_type& cb)
{
    focus_signal_t*& signal = focusCallbacks().mTopLost;
    if (!signal) signal = new focus_signal_t();
    return signal->connect(cb);
}



typedef std::list<LLHandle<LLView> > view_handle_list_t;
typedef std::map<LLHandle<LLView>, LLHandle<LLView> > focus_history_map_t;
struct LLFocusMgr::Impl
{
    // caching list of keyboard focus ancestors for calling onFocusReceived and onFocusLost
    view_handle_list_t mCachedKeyboardFocusList;

    focus_history_map_t mFocusHistory;
};

LLFocusMgr gFocusMgr;

LLFocusMgr::LLFocusMgr()
:   mLockedView( nullptr ),
    mMouseCaptor( nullptr ),
    mMouseCaptorView( nullptr ),
    mKeyboardFocus( nullptr ),
    mKeyboardFocusView( nullptr ),
    mLastKeyboardFocus( nullptr ),
    mDefaultKeyboardFocus( nullptr ),
    mKeystrokesOnly(false),
    mTopCtrl( nullptr ),
    mAppHasFocus(true),   // Macs don't seem to notify us that we've gotten focus, so default to true
    mImpl(new LLFocusMgr::Impl)
{
}

LLFocusMgr::~LLFocusMgr()
{
    mImpl->mFocusHistory.clear();
    delete mImpl;
    mImpl = nullptr;
}

void LLFocusMgr::releaseFocusIfNeeded( LLView* view )
{
    if( childHasMouseCapture( view ) )
    {
        setMouseCapture( nullptr );
    }

    if( childHasKeyboardFocus( view ))
    {
        if (view == mLockedView)
        {
            mLockedView = nullptr;
            setKeyboardFocus( nullptr );
        }
        else
        {
            setKeyboardFocus( mLockedView );
        }
    }

    if(LLUI::instanceExists()) LLUI::getInstance()->removePopup(view);
}

void LLFocusMgr::setKeyboardFocus(LLFocusableElement* new_focus, bool lock, bool keystrokes_only)
{
    // notes if keyboard focus is changed again (by onFocusLost/onFocusReceived)
    // making the rest of our processing unnecessary since it will already be
    // handled by the recursive call
    static bool focus_dirty;
    focus_dirty = false;

    // Asked once per focus change. Everything that asks afterwards reads the
    // view kept beside the element.
    LLView* new_focus_view = new_focus ? new_focus->asView() : nullptr;

    if (mLockedView &&
        (new_focus == nullptr ||
            (new_focus != mLockedView
            && new_focus_view
            && !new_focus_view->hasAncestor(mLockedView))))
    {
        // don't allow focus to go to anything that is not the locked focus
        // or one of its descendants
        return;
    }

    mKeystrokesOnly = keystrokes_only;

    if( new_focus != mKeyboardFocus )
    {
        mLastKeyboardFocus = mKeyboardFocus;
        mKeyboardFocus = new_focus;
        mKeyboardFocusView = new_focus_view;

        // list of the focus and it's ancestors
        view_handle_list_t old_focus_list = mImpl->mCachedKeyboardFocusList;
        view_handle_list_t new_focus_list;

        // walk up the tree to root and add all views to the new_focus_list
        for (LLView* ctrl = mKeyboardFocusView; ctrl; ctrl = ctrl->getParent())
        {
            new_focus_list.push_back(ctrl->getHandle());
        }

        // remove all common ancestors since their focus is unchanged
        while (!new_focus_list.empty() &&
               !old_focus_list.empty() &&
               new_focus_list.back() == old_focus_list.back())
        {
            new_focus_list.pop_back();
            old_focus_list.pop_back();
        }

        // walk up the old focus branch calling onFocusLost
        // we bubble up the tree to release focus, and back down to add
        for (view_handle_list_t::iterator old_focus_iter = old_focus_list.begin();
             old_focus_iter != old_focus_list.end() && !focus_dirty;
             old_focus_iter++)
        {
            LLView* old_focus_view = old_focus_iter->get();
            if (old_focus_view)
            {
                mImpl->mCachedKeyboardFocusList.pop_front();
                old_focus_view->onFocusLost();
            }
        }

        // walk down the new focus branch calling onFocusReceived
        for (view_handle_list_t::reverse_iterator new_focus_riter = new_focus_list.rbegin();
             new_focus_riter != new_focus_list.rend() && !focus_dirty;
             new_focus_riter++)
        {
            LLView* new_focus_view = new_focus_riter->get();
            if (new_focus_view)
            {
                mImpl->mCachedKeyboardFocusList.push_front(new_focus_view->getHandle());
                new_focus_view->onFocusReceived();
            }
        }

        // if focus was changed as part of an onFocusLost or onFocusReceived call
        // stop iterating on current list since it is now invalid
        if (focus_dirty)
        {
            return;
        }

        // If we've got a default keyboard focus, and the caller is
        // releasing keyboard focus, move to the default.
        if (mDefaultKeyboardFocus != nullptr && mKeyboardFocus == nullptr)
        {
            mDefaultKeyboardFocus->setFocus(true);
        }

        LLView* focus_subtree = mKeyboardFocusView;
        LLView* viewp = mKeyboardFocusView;
        // find root-most focus root
        while(viewp)
        {
            if (viewp->isFocusRoot())
            {
                focus_subtree = viewp;
            }
            viewp = viewp->getParent();
        }


        if (focus_subtree)
        {
            mImpl->mFocusHistory[focus_subtree->getHandle()] = mKeyboardFocusView ? mKeyboardFocusView->getHandle() : LLHandle<LLView>();
        }
    }

    if (lock)
    {
        lockFocus();
    }

    focus_dirty = true;
}


// Returns true is parent or any descedent of parent has keyboard focus.
bool LLFocusMgr::childHasKeyboardFocus(const LLView* parent ) const
{
    LLView* focus_view = mKeyboardFocusView;
    while( focus_view )
    {
        if( focus_view == parent )
        {
            return true;
        }
        focus_view = focus_view->getParent();
    }
    return false;
}

// Returns true is parent or any descedent of parent is the mouse captor.
bool LLFocusMgr::childHasMouseCapture( const LLView* parent ) const
{
    for (LLView* captor_view = mMouseCaptorView; captor_view; captor_view = captor_view->getParent())
    {
        if( captor_view == parent )
        {
            return true;
        }
    }
    return false;
}

void LLFocusMgr::removeKeyboardFocusWithoutCallback( const LLFocusableElement* focus )
{
    // should be ok to unlock here, as you have to know the locked view
    // in order to unlock it
    if (focus == mLockedView)
    {
        mLockedView = nullptr;
    }

    if( mKeyboardFocus == focus )
    {
        mKeyboardFocus = nullptr;
        mKeyboardFocusView = nullptr;
    }
}

bool LLFocusMgr::keyboardFocusHasAccelerators() const
{
    LLView* focus_view = mKeyboardFocusView;
    while( focus_view )
    {
        if(focus_view->hasAccelerators())
        {
            return true;
        }

        focus_view = focus_view->getParent();
    }
    return false;
}

void LLFocusMgr::setMouseCapture( LLMouseHandler* new_captor )
{
    if( new_captor != mMouseCaptor )
    {
        LLMouseHandler* old_captor = mMouseCaptor;
        mMouseCaptor = new_captor;
        mMouseCaptorView = dynamic_cast<LLView*>(new_captor);

        if (LLView::sDebugMouseHandling)
        {
            if (new_captor)
            {
                LL_INFOS() << "New mouse captor: " << new_captor->getName() << LL_ENDL;
            }
            else
            {
                LL_INFOS() << "New mouse captor: NULL" << LL_ENDL;
            }
        }

        if( old_captor )
        {
            old_captor->onMouseCaptureLost();
        }

    }
}

void LLFocusMgr::removeMouseCaptureWithoutCallback( const LLMouseHandler* captor )
{
    if( mMouseCaptor == captor )
    {
        mMouseCaptor = nullptr;
        mMouseCaptorView = nullptr;
    }
}


bool LLFocusMgr::childIsTopCtrl( const LLView* parent ) const
{
    LLView* top_view = (LLView*)mTopCtrl;
    while( top_view )
    {
        if( top_view == parent )
        {
            return true;
        }
        top_view = top_view->getParent();
    }
    return false;
}



// set new_top = NULL to release top_view.
void LLFocusMgr::setTopCtrl( LLUICtrl* new_top  )
{
    LLUICtrl* old_top = mTopCtrl;
    if( new_top != old_top )
    {
        mTopCtrl = new_top;

        if (old_top)
        {
            old_top->onTopLost();
        }
    }
}

void LLFocusMgr::removeTopCtrlWithoutCallback( const LLUICtrl* top_view )
{
    if( mTopCtrl == top_view )
    {
        mTopCtrl = nullptr;
    }
}

LLUICtrl* LLFocusMgr::getKeyboardFocusCtrl() const
{
    return (mKeyboardFocusView && mKeyboardFocusView->isCtrl()) ? static_cast<LLUICtrl*>(mKeyboardFocusView) : nullptr;
}

void LLFocusMgr::lockFocus()
{
    mLockedView = getKeyboardFocusCtrl();
}

void LLFocusMgr::unlockFocus()
{
    mLockedView = nullptr;
}

F32 LLFocusMgr::getFocusFlashAmt() const
{
    return clamp_rescale(mFocusFlashTimer.getElapsedTimeF32(), 0.f, FOCUS_FADE_TIME, 1.f, 0.f);
}

S32 LLFocusMgr::getFocusFlashWidth() const
{
    return ll_round(lerp(1.f, 2.f, getFocusFlashAmt()));
}

LLColor4 LLFocusMgr::getFocusColor() const
{
    static LLUIColor focus_color_cached = LLUIColorTable::instance().getColor("FocusColor");
    LLColor4 focus_color = lerp(focus_color_cached, LLColor4::white, getFocusFlashAmt());
    // de-emphasize keyboard focus when app has lost focus (to avoid typing into wrong window problem)
    if (!mAppHasFocus)
    {
        focus_color.mV[VALPHA] *= 0.4f;
    }
    return focus_color;
}

void LLFocusMgr::triggerFocusFlash()
{
    mFocusFlashTimer.reset();
}

void LLFocusMgr::setAppHasFocus(bool focus)
{
    if (!mAppHasFocus && focus)
    {
        triggerFocusFlash();
    }

    // release focus from "top ctrl"s, which generally hides them
    if (!focus)
    {
        if(LLUI::instanceExists()) LLUI::getInstance()->clearPopups();
    }
    mAppHasFocus = focus;
}

LLView* LLFocusMgr::getLastFocusForGroup(LLView* subtree_root) const
{
    if (subtree_root)
    {
        focus_history_map_t::const_iterator found_it = mImpl->mFocusHistory.find(subtree_root->getHandle());
        if (found_it != mImpl->mFocusHistory.end())
        {
            // found last focus for this subtree
            return found_it->second.get();
        }
    }
    return nullptr;
}

void LLFocusMgr::clearLastFocusForGroup(LLView* subtree_root)
{
    if (subtree_root)
    {
        mImpl->mFocusHistory.erase(subtree_root->getHandle());
    }
}
