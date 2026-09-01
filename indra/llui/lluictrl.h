/**
 * @file lluictrl.h
 * @author James Cook, Richard Nelson, Tom Yedwab
 * @brief Abstract base class for UI controls
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

#ifndef LL_LLUICTRL_H
#define LL_LLUICTRL_H

//#include "llboost.h"
#include "llrect.h"
#include "llsd.h"
#include <functional>
#include <boost/bind.hpp>
#include <boost/signals2.hpp>

#include "llinitparam.h"
#include "llview.h"
#include "llviewmodel.h"        // *TODO move dependency to .cpp file
#include "llsearchablecontrol.h"

constexpr bool TAKE_FOCUS_YES = true;
constexpr bool TAKE_FOCUS_NO  = false;
constexpr S32 DROP_SHADOW_FLOATER = 5;

class LLUICtrl
    : public LLView, public boost::signals2::trackable
{
public:
    using commit_callback_t = std::function<void (LLUICtrl* ctrl, const LLSD& param)>;
    using commit_signal_t = boost::signals2::signal<void (LLUICtrl* ctrl, const LLSD& param)>;
    // *TODO: add xml support for this type of signal in the future
    using mouse_signal_t = boost::signals2::signal<void (LLUICtrl* ctrl, S32 x, S32 y, MASK mask)>;

    using enable_callback_t = std::function<bool (LLUICtrl* ctrl, const LLSD& param)>;
    using enable_signal_t = boost::signals2::signal<bool (LLUICtrl* ctrl, const LLSD& param), boost_boolean_combiner>;

    struct CallbackParam : public LLInitParam::Block<CallbackParam>
    {
        Ignored                 name;

        Optional<std::string>   function_name;
        Optional<LLSD>          parameter;

        Optional<std::string>   control_name;

        CallbackParam();
    };

    struct CommitCallbackParam : public LLInitParam::Block<CommitCallbackParam, CallbackParam >
    {
        Optional<commit_callback_t> function;
    };

    // also used for visible callbacks
    struct EnableCallbackParam : public LLInitParam::Block<EnableCallbackParam, CallbackParam >
    {
        Optional<enable_callback_t> function;
    };

    struct EnableControls : public LLInitParam::ChoiceBlock<EnableControls>
    {
        Alternative<std::string> enabled;
        Alternative<std::string> disabled;

        EnableControls();
    };
    struct ControlVisibility : public LLInitParam::ChoiceBlock<ControlVisibility>
    {
        Alternative<std::string> visible;
        Alternative<std::string> invisible;

        ControlVisibility();
    };
    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Optional<std::string>           label;
        Optional<bool>                  tab_stop,
                                        chrome,
                                        requests_front;
        Optional<LLSD>                  initial_value;

        Optional<CommitCallbackParam>   init_callback,
                                        commit_callback;
        Optional<EnableCallbackParam>   validate_callback;

        Optional<CommitCallbackParam>   mouseenter_callback,
                                        mouseleave_callback;

        Optional<std::string>           control_name;
        Optional<EnableControls>        enabled_controls;
        Optional<ControlVisibility>     controls_visibility;

        // font params
        Optional<const LLFontGL*>       font;
        Optional<LLFontGL::HAlign>      font_halign;
        Optional<LLFontGL::VAlign>      font_valign;

        // cruft from LLXMLNode implementation
        Ignored                         type,
                                        length;

        Params();
    };

    enum ETypeTransparency
    {
        TT_DEFAULT,
        TT_ACTIVE,      // focused floater
        TT_INACTIVE,    // other floaters
        TT_FADING,      // fading toast
    };
    /*virtual*/ ~LLUICtrl();

    void initFromParams(const Params& p);
protected:
    friend class LLUICtrlFactory;
    static const Params& getDefaultParams();
    LLUICtrl(const Params& p = getDefaultParams(),
             const LLViewModelPtr& viewmodel=LLViewModelPtr());

    commit_signal_t::slot_type initCommitCallback(const CommitCallbackParam& cb);
    enable_signal_t::slot_type initEnableCallback(const EnableCallbackParam& cb);

    // We need this virtual so we can override it with derived versions
    virtual LLViewModel* getViewModel() const;
    // We shouldn't ever need to set this directly
    //virtual void    setViewModel(const LLViewModelPtr&);

    /*virtual*/ bool    postBuild() override;

public:
    // LLView interface
    /*virtual*/ bool    setLabelArg( const std::string& key, const LLStringExplicit& text ) override;
    /*virtual*/ bool    isCtrl() const override;
    /*virtual*/ void    onMouseEnter(S32 x, S32 y, MASK mask) override;
    /*virtual*/ void    onMouseLeave(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    canFocusChildren() const override;
    /*virtual*/ bool    handleMouseDown(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    handleMouseUp(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    handleRightMouseDown(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    handleRightMouseUp(S32 x, S32 y, MASK mask) override;
    /*virtual*/ bool    handleDoubleClick(S32 x, S32 y, MASK mask) override;

    // From LLFocusableElement
    /*virtual*/ void    setFocus( bool b ) override;
    /*virtual*/ bool    hasFocus() const override;

    // New virtuals


    // Return NULL by default (overrride if the class has the appropriate interface)
    virtual class LLCtrlSelectionInterface* getSelectionInterface();
    virtual class LLCtrlListInterface* getListInterface();
    virtual class LLCtrlScrollInterface* getScrollInterface();

    bool setControlValue(const LLSD& value);
    void setControlVariable(LLControlVariable* control);
    virtual void setControlName(const std::string& control, LLView *context = nullptr);
    void removeControlVariable();

    LLControlVariable* getControlVariable() { return mControlVariables ? mControlVariables->mControlVariable : nullptr; }
    LLControlVariable* getEnabledControlVariable() { return mControlVariables ? mControlVariables->mEnabledControlVariable : nullptr; }

    void setEnabledControlVariable(LLControlVariable* control);
    void setDisabledControlVariable(LLControlVariable* control);
    void setMakeVisibleControlVariable(LLControlVariable* control);
    void setMakeInvisibleControlVariable(LLControlVariable* control);

    // The name of the registered callback this control fires, kept for the
    // UIUsage debug log and read by nothing else. A shipping build does not
    // carry it at all; inline, so the assignment goes away with the member
    // rather than becoming a call that discards its argument.
    void setFunctionName(const std::string& function_name)
    {
#if !LL_RELEASE_FOR_DOWNLOAD
        mFunctionName = function_name;
#endif
    }

    virtual void    setTentative(bool b);
    virtual bool    getTentative() const;
    virtual void    setValue(const LLSD& value);
    virtual LLSD    getValue() const;
    /// When two widgets are displaying the same data (e.g. during a skin
    /// change), share their ViewModel.
    virtual void    shareViewModelFrom(const LLUICtrl& other);

    virtual bool    setTextArg(  const std::string& key, const LLStringExplicit& text );
    virtual void    setIsChrome(bool is_chrome);

    virtual bool    acceptsTextInput() const; // Defaults to false

    // A control is dirty if the user has modified its value.
    // Editable controls should override this.
    virtual bool    isDirty() const; // Defauls to false
    virtual void    resetDirty(); //Defaults to no-op

    // Call appropriate callback
    virtual void    onCommit();

    // Default to no-op:
    virtual void    onTabInto();

    // Clear any user-provided input (text in a text editor, checked checkbox,
    // selected radio button, etc.).  Defaults to no-op.
    virtual void    clear();

    virtual void    setColor(const LLUIColor& color);

    // Ansariel: Changed to virtual. We might want to change the transparency ourself!
    virtual F32 getCurrentTransparency();

    void                setTransparencyType(ETypeTransparency type);
    ETypeTransparency   getTransparencyType() const {return mTransparencyType;}

    bool    focusNextItem(bool text_entry_only);
    bool    focusPrevItem(bool text_entry_only);
    bool    focusFirstItem(bool prefer_text_fields = false, bool focus_flash = true );

    // Non Virtuals
    LLHandle<LLUICtrl> getHandle() const { return getDerivedHandle<LLUICtrl>(); }
    bool            getIsChrome() const;

    void            setTabStop( bool b );
    bool            hasTabStop() const;

    LLUICtrl*       getParentUICtrl() const;

    // return true if help topic found by crawling through parents -
    // topic then put in help_topic_out
    bool                    findHelpTopic(std::string& help_topic_out);

    boost::signals2::connection setCommitCallback(const CommitCallbackParam& cb);
    boost::signals2::connection setValidateCallback(const EnableCallbackParam& cb);

    boost::signals2::connection setCommitCallback( const commit_signal_t::slot_type& cb );
    boost::signals2::connection setValidateCallback( const enable_signal_t::slot_type& cb );

    boost::signals2::connection setMouseEnterCallback( const commit_signal_t::slot_type& cb );
    boost::signals2::connection setMouseLeaveCallback( const commit_signal_t::slot_type& cb );

    boost::signals2::connection setMouseDownCallback( const mouse_signal_t::slot_type& cb );
    boost::signals2::connection setMouseUpCallback( const mouse_signal_t::slot_type& cb );
    boost::signals2::connection setRightMouseDownCallback( const mouse_signal_t::slot_type& cb );
    boost::signals2::connection setRightMouseUpCallback( const mouse_signal_t::slot_type& cb );

    boost::signals2::connection setDoubleClickCallback( const mouse_signal_t::slot_type& cb );

    // *TODO: Deprecate; for backwards compatability only:
    boost::signals2::connection setCommitCallback( std::function<void (LLUICtrl*,void*)> cb, void* data);
    boost::signals2::connection setValidateBeforeCommit( std::function<bool (const LLSD& data)> cb );

    LLUICtrl* findRootMostFocusRoot();

    class LLTextInputFilter : public LLQueryFilter, public LLSingleton<LLTextInputFilter>
    {
        LLSINGLETON_EMPTY_CTOR(LLTextInputFilter);
        /*virtual*/ filterResult_t operator() (const LLView* const view, bool has_children) const override
        {
            return filterResult_t(view->isCtrl() && static_cast<const LLUICtrl *>(view)->acceptsTextInput(), true);
        }
    };

    template <typename F, typename DERIVED> class CallbackRegistry : public LLRegistrySingleton<std::string, F, DERIVED >
    {};

    class CommitCallbackRegistry : public CallbackRegistry<commit_callback_t, CommitCallbackRegistry>
    {
        LLSINGLETON_EMPTY_CTOR(CommitCallbackRegistry);
    };
    // the enable callback registry is also used for visiblity callbacks
    class EnableCallbackRegistry : public CallbackRegistry<enable_callback_t, EnableCallbackRegistry>
    {
        LLSINGLETON_EMPTY_CTOR(EnableCallbackRegistry);
    };

protected:

    static bool controlListener(const LLSD& newvalue, LLHandle<LLUICtrl> handle, std::string type);

    // Almost every control has one of these; the viewer connects one at 858
    // call sites.
    commit_signal_t*        mCommitSignal;

    // The other eight, which almost no control has: the mouse callbacks are
    // connected at a couple of hundred call sites between them and the
    // validate callback at four. Eight pointers on every control to say so
    // cost more than the block the first of them allocates.
    struct RareSignals
    {
        enable_signal_t* mValidate { nullptr };
        commit_signal_t* mMouseEnter { nullptr };
        commit_signal_t* mMouseLeave { nullptr };
        mouse_signal_t*  mMouseDown { nullptr };
        mouse_signal_t*  mMouseUp { nullptr };
        mouse_signal_t*  mRightMouseDown { nullptr };
        mouse_signal_t*  mRightMouseUp { nullptr };
        mouse_signal_t*  mDoubleClick { nullptr };

        ~RareSignals();
    };

    RareSignals* mRareSignals { nullptr };
    RareSignals& rareSignals(); // allocates

    // Null unless something connected that callback.
    enable_signal_t* validateSignal() const       { return mRareSignals ? mRareSignals->mValidate : nullptr; }
    commit_signal_t* mouseEnterSignal() const     { return mRareSignals ? mRareSignals->mMouseEnter : nullptr; }
    commit_signal_t* mouseLeaveSignal() const     { return mRareSignals ? mRareSignals->mMouseLeave : nullptr; }
    mouse_signal_t*  mouseDownSignal() const      { return mRareSignals ? mRareSignals->mMouseDown : nullptr; }
    mouse_signal_t*  mouseUpSignal() const        { return mRareSignals ? mRareSignals->mMouseUp : nullptr; }
    mouse_signal_t*  rightMouseDownSignal() const { return mRareSignals ? mRareSignals->mRightMouseDown : nullptr; }
    mouse_signal_t*  rightMouseUpSignal() const   { return mRareSignals ? mRareSignals->mRightMouseUp : nullptr; }
    mouse_signal_t*  doubleClickSignal() const    { return mRareSignals ? mRareSignals->mDoubleClick : nullptr; }

    // Created on first use. The model holds the control's value, and most
    // controls never have one -- panels, icons, buttons that carry no value --
    // so the default argument on the constructor was allocating one apiece for
    // nothing. Named apart from what it was so that every place reaching past
    // it has to be looked at rather than silently reading a null.
    const LLViewModelPtr& viewModelPtr() const;
    LLViewModel* viewModel() const { return viewModelPtr().get(); }

    mutable LLViewModelPtr mViewModelStorage;

#if !LL_RELEASE_FOR_DOWNLOAD
    std::string mFunctionName;
#endif

    static F32 sActiveControlTransparency;
    static F32 sInactiveControlTransparency;

    /*virtual*/ void addInfo(LLSD & info) override;

private:

    bool            mIsChrome;
    bool            mRequestsFront;
    bool            mTabStop;
    bool            mTentative;

    ETypeTransparency mTransparencyType;

    // Binding a control to a setting variable is uncommon, so keep the
    // pointers and their scoped (auto-disconnecting) signal connections in a
    // block allocated only on first use. This both avoids ~120 bytes of
    // mostly-unused members on every control and ensures the connections are
    // disconnected when the control is destroyed (scoped_connection RAII).
    struct ControlVariables
    {
        LLControlVariable* mControlVariable{ nullptr };
        LLControlVariable* mEnabledControlVariable{ nullptr };
        LLControlVariable* mDisabledControlVariable{ nullptr };
        LLControlVariable* mMakeVisibleControlVariable{ nullptr };
        LLControlVariable* mMakeInvisibleControlVariable{ nullptr };
        boost::signals2::scoped_connection mControlConnection;
        boost::signals2::scoped_connection mEnabledControlConnection;
        boost::signals2::scoped_connection mDisabledControlConnection;
        boost::signals2::scoped_connection mMakeVisibleControlConnection;
        boost::signals2::scoped_connection mMakeInvisibleControlConnection;
    };
    ControlVariables*   mControlVariables{ nullptr };
    ControlVariables&   getControlVars(); // lazily allocates mControlVariables
};

// Build time optimization, generate once in .cpp file
#ifndef LLUICTRL_CPP
extern template class LLUICtrl* LLView::getChild<class LLUICtrl>(
    std::string_view name, bool recurse) const;
#endif

#endif  // LL_LLUICTRL_H
