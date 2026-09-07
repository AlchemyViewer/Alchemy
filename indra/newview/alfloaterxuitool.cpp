/**
 * @file alfloaterxuitool.cpp
 * @brief The XUI tool: catalog, preview, hierarchy, inspectors and diagnostics for XUI files.
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

#include "alfloaterxuitool.h"

#include "alxmldocument.h"
#include "alxmllayermerge.h"
#include "alxuishellbuild.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcombobox.h"
#include "lldir.h"
#include "llexternaleditor.h"
#include "llfile.h"
#include "llfiltereditor.h"
#include "llimagebmp.h"
#include "llimagej2c.h"
#include "llimagejpeg.h"
#include "llimagepng.h"
#include "llimagetga.h"
#include "llfolderview.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "lllivefile.h"
#include "llmenugl.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "llscrolllistctrl.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltimer.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewermenufile.h"
#include "llviewerwindow.h"

#include <algorithm>
#include <set>

// ===========================================================================
// The pieces the floater builds on
// ===========================================================================

// The skin and language the directory object answers with, switched for a
// build and switched back after it. The factory's cached defaults are keyed
// by parameter block type, so they are dropped on both sides of the switch.
class ALXUISkinScope
{
public:
    ALXUISkinScope(const std::string& skin, const std::string& language)
    :   mSkin(gDirUtilp->getSkinFolder()),
        mLanguage(gDirUtilp->getLanguage()),
        mSwitched(skin != mSkin || language != mLanguage)
    {
        if (mSwitched)
        {
            gDirUtilp->setSkinFolder(skin, language);
            LLUICtrlFactory::instance().flushDefaults();
        }
    }

    ~ALXUISkinScope()
    {
        if (mSwitched)
        {
            gDirUtilp->setSkinFolder(mSkin, mLanguage);
            LLUICtrlFactory::instance().flushDefaults();
        }
    }

    ALXUISkinScope(const ALXUISkinScope&) = delete;
    ALXUISkinScope& operator=(const ALXUISkinScope&) = delete;

private:
    std::string mSkin;
    std::string mLanguage;
    bool        mSwitched;
};

// One file of the primary preview, watched for a change on disk. The
// first check counts as reading it; only a change after that reloads.
class ALXUILiveFile final : public LLLiveFile
{
public:
    ALXUILiveFile(const std::string& path, ALFloaterXUITool* tool)
    :   LLLiveFile(path, 1.f),
        mTool(tool)
    {
    }

protected:
    bool loadFile() override
    {
        if (!mPrimed)
        {
            mPrimed = true;
            return true;
        }
        mTool->fileChanged();
        return true;
    }

private:
    ALFloaterXUITool*   mTool;
    bool                mPrimed = false;
};

// A preview: a floater in the floater view that is the previewed floater,
// or hosts the previewed panel, menu or widget. It draws the tool's hover
// and selection over what it shows, answers a modifier click with a
// selection, and tells the tool when it goes.
class ALXUIPreviewHost final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALXUIPreviewHost, LLFloater);

    ALXUIPreviewHost(ALFloaterXUITool* tool, S32 which, const LLFloater::Params& p)
    :   LLFloater(LLSD(), p),
        mTool(tool),
        mWhich(which)
    {
    }

    ~ALXUIPreviewHost() override
    {
        if (mTool)
        {
            mTool->hostClosed(mWhich);
        }
    }

    void detach() { mTool = nullptr; }
    void setRoot(LLView* root) { mRoot = root; }

    void draw() override
    {
        LLFloater::draw();
        if (!mTool || !mRoot)
        {
            return;
        }
        const ALXUISelection& selection = mTool->selection();
        if (selection.hasHover() && mTool->hoverHighlight())
        {
            static const LLUIColor hover_color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
            if (LLView* view = ALXUISelection::resolve(mRoot, selection.hover()))
            {
                drawBox(view, hover_color.get(), true);
            }
        }
        if (selection.hasSelection())
        {
            if (LLView* view = ALXUISelection::resolve(mRoot, selection.selection()))
            {
                drawBox(view, LLColor4::red, true);
                if (gKeyboard && (gKeyboard->currentMask(false) & MASK_ALT))
                {
                    drawGuides(view);
                }
            }
        }
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (mTool && mRoot && (mask & MASK_CONTROL))
        {
            mTool->canvasSelect(mWhich, hitTest(x, y));
            return true;
        }
        return LLFloater::handleMouseDown(x, y, mask);
    }

    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        if (mTool && mRoot)
        {
            mTool->canvasHover(mWhich, hitTest(x, y));
        }
        return LLFloater::handleHover(x, y, mask);
    }

    void onMouseLeave(S32 x, S32 y, MASK mask) override
    {
        LLFloater::onMouseLeave(x, y, mask);
        if (mTool)
        {
            mTool->canvasHover(mWhich, nullptr);
        }
    }

private:
    // The deepest visible view under a point in this floater's space, and
    // then the nearest one the file describes, since a widget's own
    // children are not what an author is pointing at.
    LLView* hitTest(S32 x, S32 y)
    {
        LLView* deepest = mRoot == this ? this : nullptr;
        LLView* start = mRoot == this ? this : mRoot;
        S32 px = x;
        S32 py = y;
        if (start != this)
        {
            // The point in the root's parent's space.
            LLRect screen;
            localRectToScreen(LLRect(x, y, x, y), &screen);
            LLRect in_parent;
            start->getParent()->screenRectToLocal(screen, &in_parent);
            px = in_parent.mLeft;
            py = in_parent.mBottom;
            if (!start->getVisible() || !start->pointInView(px - start->getRect().mLeft, py - start->getRect().mBottom))
            {
                return nullptr;
            }
            deepest = start;
            px -= start->getRect().mLeft;
            py -= start->getRect().mBottom;
        }
        for (bool found = true; found;)
        {
            found = false;
            for (LLView* child : *deepest->getChildList())
            {
                const LLRect& r = child->getRect();
                if (child->getVisible() && child->pointInView(px - r.mLeft, py - r.mBottom))
                {
                    deepest = child;
                    px -= r.mLeft;
                    py -= r.mBottom;
                    found = true;
                    break;
                }
            }
        }
        const ALXUISourceMap& map = mTool->sourceMap(mWhich);
        LLView* view = deepest;
        while (view && view != mRoot && !map.isFromXML(view))
        {
            view = view->getParent();
        }
        return view;
    }

    LLRect localRectOf(const LLView* view) const
    {
        LLRect local;
        screenRectToLocal(view->calcScreenRect(), &local);
        return local;
    }

    void drawBox(const LLView* view, const LLColor4& color, bool label)
    {
        const LLRect r = localRectOf(view);
        gl_rect_2d(r, color, false);
        LLRect outer(r);
        outer.stretch(1);
        LLColor4 faint(color);
        faint.mV[VALPHA] = 0.5f;
        gl_rect_2d(outer, faint, false);
        if (label)
        {
            const std::string text = std::to_string(r.getWidth()) + " x " + std::to_string(r.getHeight());
            LLFontGL::getFontSansSerifSmall()->renderUTF8(text, 0, (F32)r.mLeft, (F32)r.mTop + 2.f, color,
                                                          LLFontGL::LEFT, LLFontGL::BOTTOM, LLFontGL::NORMAL,
                                                          LLFontGL::DROP_SHADOW);
        }
    }

    void drawDistance(S32 x1, S32 y1, S32 x2, S32 y2, S32 value, const LLColor4& color)
    {
        if (value <= 0)
        {
            return;
        }
        gl_line_2d(x1, y1, x2, y2, color);
        const std::string text = std::to_string(value);
        LLFontGL::getFontSansSerifSmall()->renderUTF8(text, 0, (F32)((x1 + x2) / 2), (F32)((y1 + y2) / 2), color,
                                                      LLFontGL::HCENTER, LLFontGL::VCENTER, LLFontGL::NORMAL,
                                                      LLFontGL::DROP_SHADOW);
    }

    // The numbers left, top, right and bottom mean, drawn from the view to
    // its parent's edges, and left_pad and top_pad from the sibling created
    // before it.
    void drawGuides(const LLView* view)
    {
        const LLView* parent = view->getParent();
        if (!parent)
        {
            return;
        }
        static const LLUIColor guide_color = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
        const LLColor4 color = guide_color.get();
        const LLRect r = localRectOf(view);
        const LLRect p = localRectOf(parent);
        const S32 mid_y = (r.mTop + r.mBottom) / 2;
        const S32 mid_x = (r.mLeft + r.mRight) / 2;
        drawDistance(p.mLeft, mid_y, r.mLeft, mid_y, r.mLeft - p.mLeft, color);
        drawDistance(r.mRight, mid_y, p.mRight, mid_y, p.mRight - r.mRight, color);
        drawDistance(mid_x, r.mTop, mid_x, p.mTop, p.mTop - r.mTop, color);
        drawDistance(mid_x, p.mBottom, mid_x, r.mBottom, r.mBottom - p.mBottom, color);

        const LLView::child_list_t& siblings = *parent->getChildList();
        auto it = std::find(siblings.begin(), siblings.end(), view);
        if (it != siblings.end() && std::next(it) != siblings.end())
        {
            const LLRect s = localRectOf(*std::next(it));
            LLColor4 sibling_color = LLColor4::cyan;
            drawDistance(s.mRight, mid_y, r.mLeft, mid_y, r.mLeft - s.mRight, sibling_color);
            drawDistance(mid_x, r.mTop, mid_x, s.mBottom, s.mBottom - r.mTop, sibling_color);
        }
    }

    ALFloaterXUITool*   mTool;
    LLView*             mRoot = nullptr;
    S32                 mWhich;
};

namespace
{
    constexpr S32 MAX_FIND_ROWS = 500;

    std::string firstToken(const std::string& path)
    {
        const size_t dot = path.find('.');
        return dot == std::string::npos ? path : path.substr(0, dot);
    }

    // A child widget's attributes fail against the parent's block by
    // design and are parsed again by the child; those are not findings.
    bool isNoise(const ALXUIDiagnostics::Entry& e)
    {
        return e.kind == ALXUIDiagnostics::Kind::UnknownAttribute && e.depth > 0
            && ALXUICatalog::isWidgetTag(firstToken(e.path));
    }

    std::string followsText(U32 follows)
    {
        std::string text;
        auto add = [&](U32 flag, const char* name)
        {
            if (follows & flag)
            {
                if (!text.empty())
                {
                    text += '|';
                }
                text += name;
            }
        };
        add(FOLLOWS_LEFT, "left");
        add(FOLLOWS_TOP, "top");
        add(FOLLOWS_RIGHT, "right");
        add(FOLLOWS_BOTTOM, "bottom");
        return text.empty() ? std::string("none") : text;
    }

    std::string rectText(const LLRect& r)
    {
        return "left " + std::to_string(r.mLeft) + "  top " + std::to_string(r.mTop)
             + "  right " + std::to_string(r.mRight) + "  bottom " + std::to_string(r.mBottom)
             + "  (" + std::to_string(r.getWidth()) + " x " + std::to_string(r.getHeight()) + ")";
    }

    bool isBuilt(ALXUICatalog::Kind kind)
    {
        switch (kind)
        {
        case ALXUICatalog::Kind::Floater:
        case ALXUICatalog::Kind::Panel:
        case ALXUICatalog::Kind::Menu:
        case ALXUICatalog::Kind::Widget:
        case ALXUICatalog::Kind::Template:
            return true;
        default:
            return false;
        }
    }

    S32 countViews(const LLView* view)
    {
        S32 n = 1;
        for (const LLView* child : *view->getChildList())
        {
            n += countViews(child);
        }
        return n;
    }

    S32 widestLine(const LLFontGL* font, const std::string& text)
    {
        S32 widest = 0;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
            {
                end = text.size();
            }
            widest = llmax(widest, font->getWidth(std::string_view(text).substr(start, end - start)));
            start = end + 1;
        }
        return widest;
    }

    // From the first '<' on a line, the bytes of the element that starts
    // there: tags open and close it, and comments and declarations are
    // skipped over.
    std::string elementTextAt(const std::string& text, S32 line)
    {
        size_t pos = 0;
        for (S32 l = 1; l < line && pos != std::string::npos; ++l)
        {
            pos = text.find('\n', pos);
            if (pos != std::string::npos)
            {
                ++pos;
            }
        }
        if (pos == std::string::npos)
        {
            return std::string();
        }
        const size_t start = text.find('<', pos);
        if (start == std::string::npos)
        {
            return std::string();
        }
        S32 depth = 0;
        size_t i = start;
        while (i < text.size())
        {
            if (text.compare(i, 4, "<!--") == 0)
            {
                const size_t end = text.find("-->", i);
                i = end == std::string::npos ? text.size() : end + 3;
                continue;
            }
            if (text[i] == '<')
            {
                const bool closing = i + 1 < text.size() && text[i + 1] == '/';
                const bool declaration = i + 1 < text.size() && (text[i + 1] == '?' || text[i + 1] == '!');
                const size_t end = text.find('>', i);
                if (end == std::string::npos)
                {
                    break;
                }
                if (!declaration)
                {
                    const bool self_closing = end > 0 && text[end - 1] == '/';
                    if (closing || self_closing)
                    {
                        if (!closing)
                        {
                            ++depth;
                        }
                        --depth;
                    }
                    else
                    {
                        ++depth;
                    }
                    if (depth <= 0)
                    {
                        return text.substr(start, end + 1 - start);
                    }
                }
                i = end + 1;
                continue;
            }
            ++i;
        }
        return text.substr(start);
    }

    std::string numbered(const std::string& text, S32 first_line)
    {
        std::string out;
        S32 line = first_line;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            const bool last = end == std::string::npos;
            if (last)
            {
                end = text.size();
            }
            out += std::to_string(line++);
            out += "  ";
            out += text.substr(start, end - start);
            out += '\n';
            if (last)
            {
                break;
            }
            start = end + 1;
        }
        return out;
    }

    const char* KIND_FIELDS[] = { "any", "tag", "attribute", "value", "name", "text" };

    ALXUICatalog::Field fieldFrom(const std::string& value)
    {
        if (value == "tag") return ALXUICatalog::Field::Tag;
        if (value == "attribute") return ALXUICatalog::Field::Attribute;
        if (value == "value") return ALXUICatalog::Field::Value;
        if (value == "name") return ALXUICatalog::Field::Name;
        if (value == "text") return ALXUICatalog::Field::Text;
        return ALXUICatalog::Field::Any;
    }

    // The tags the UI library registers. A tag the viewer registers is
    // built by a viewer class, whose constructor is viewer code with the
    // expectations of viewer code: a notification to attach to, an agent,
    // a plugin. A shell build cannot meet them, so those tags are shown
    // from their files and searched, and built only by the viewer.
    bool isCoreWidgetTag(const std::string& tag)
    {
        static const std::set<std::string> core = {
            "accordion", "accordion_tab", "badge", "button", "chat_editor", "check_box", "combo_box",
            "console", "container_view", "context_menu", "filter_editor", "flat_list_view", "floater_view",
            "flyout_button", "folder_view_item", "fs_virtual_trackpad", "icon", "icons_combo_box",
            "layout_panel", "layout_stack", "line_editor", "loading_indicator", "locate", "menu",
            "menu_bar", "menu_button", "menu_item", "menu_item_call", "menu_item_check",
            "menu_item_separator", "menu_item_tear_off", "multi_slider", "multi_slider_bar", "panel",
            "placeholder", "progress_bar", "radio_group", "scroll_bar", "scroll_container", "scroll_list",
            "scrolling_panel_list", "search_editor", "simple_text_editor", "slider", "slider_bar",
            "spinner", "stat_bar", "stat_view", "sun_moon_trackball", "tab_container", "text", "time",
            "toggleable_menu", "tool_tip", "toolbar", "tooltip_view", "ui_ctrl", "view", "view_border",
            "window_shade", "xy_vector"
        };
        return core.count(tag) != 0;
    }

    // The core tags the gallery shows: the ones that stand on their own
    // with defaults. The rest need a parent of a kind, children, or
    // parameters a template does not give.
    bool galleryTag(const std::string& tag)
    {
        static const std::set<std::string> skipped = {
            "accordion", "accordion_tab", "chat_editor", "console", "container_view", "context_menu",
            "flat_list_view", "floater_view", "folder_view_item", "layout_panel", "layout_stack", "locate",
            "menu", "menu_bar", "menu_item", "menu_item_call", "menu_item_check", "menu_item_separator",
            "menu_item_tear_off", "panel", "placeholder", "scroll_container", "scrolling_panel_list",
            "stat_view", "tab_container", "toggleable_menu", "tool_tip", "toolbar", "tooltip_view",
            "ui_ctrl", "view", "window_shade"
        };
        return isCoreWidgetTag(tag) && skipped.count(tag) == 0;
    }
}

// ===========================================================================
// ALFloaterXUITool
// ===========================================================================
ALFloaterXUITool::ALFloaterXUITool(const LLSD& key)
:   LLFloater(key)
{
    mCommitCallbackRegistrar.add("XUITool.Tree", boost::bind(&ALFloaterXUITool::onTreeAction, this, _2));
    mEnableCallbackRegistrar.add("XUITool.TreeEnabled", boost::bind(&ALFloaterXUITool::onTreeActionEnabled, this, _2));
}

ALFloaterXUITool::~ALFloaterXUITool()
{
    closePreviews();
}

bool ALFloaterXUITool::postBuild()
{
    mCatalogFilter = getChild<LLFilterEditor>("catalog_filter");
    mFileList = getChild<LLScrollListCtrl>("file_list");
    mSkinCombo = getChild<LLComboBox>("skin_combo");
    mLanguageCombo = getChild<LLComboBox>("language_combo");
    mLanguageCombo2 = getChild<LLComboBox>("language_combo_2");
    mSecondaryCheck = getChild<LLCheckBoxCtrl>("secondary_check");
    mFindQuery = getChild<LLLineEditor>("find_query");
    mFindField = getChild<LLComboBox>("find_field");
    mFindResults = getChild<LLScrollListCtrl>("find_results");
    mTreeFilter = getChild<LLFilterEditor>("tree_filter");
    mTreePanel = getChild<LLPanel>("tree_host");
    mBreadcrumb = getChild<LLPanel>("breadcrumb");
    mFindings = getChild<LLScrollListCtrl>("findings");
    mInspectors = getChild<LLTabContainer>("inspector_tabs");
    mAttributes = getChild<LLScrollListCtrl>("attributes");
    mLayout = getChild<LLScrollListCtrl>("layout");
    mSourceLayers = getChild<LLTextBox>("source_layers");
    mSourceText = getChild<LLTextEditor>("source_text");
    mBindings = getChild<LLScrollListCtrl>("bindings");
    mState = getChild<LLScrollListCtrl>("state");
    mSelectionFindings = getChild<LLScrollListCtrl>("selection_findings");
    mStatus = getChild<LLTextBox>("status");

    loadState();
    scanCatalog();

    mCatalogFilter->setCommitCallback(boost::bind(&ALFloaterXUITool::onCatalogFilter, this));
    mFileList->setCommitCallback(boost::bind(&ALFloaterXUITool::onFileSelected, this));
    mFileList->setCommitOnSelectionChange(true);
    mSkinCombo->setCommitCallback(boost::bind(&ALFloaterXUITool::onSkinOrLanguage, this));
    mLanguageCombo->setCommitCallback(boost::bind(&ALFloaterXUITool::onSkinOrLanguage, this));
    mLanguageCombo2->setCommitCallback(boost::bind(&ALFloaterXUITool::onSkinOrLanguage, this));
    mSecondaryCheck->setCommitCallback(boost::bind(&ALFloaterXUITool::onToggleSecondary, this));
    mFindQuery->setCommitCallback(boost::bind(&ALFloaterXUITool::onFind, this));
    mFindField->setCommitCallback(boost::bind(&ALFloaterXUITool::onFind, this));
    mFindResults->setDoubleClickCallback(boost::bind(&ALFloaterXUITool::onFindResult, this));
    mTreeFilter->setCommitCallback(boost::bind(&ALFloaterXUITool::onTreeFilter, this));
    mFindings->setDoubleClickCallback(boost::bind(&ALFloaterXUITool::onFindingSelected, this));
    mInspectors->setCommitCallback(boost::bind(&ALFloaterXUITool::refreshInspectors, this));

    getChild<LLButton>("show_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::showPreviews, this));
    getChild<LLButton>("hide_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::closePreviews, this));
    getChild<LLButton>("reload_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::fileChanged, this));
    getChild<LLButton>("edit_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::onJumpToSource, this));
    getChild<LLButton>("jump_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::onJumpToSource, this));
    getChild<LLButton>("gallery_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::showGallery, this));
    getChild<LLButton>("lint_all_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::startLintAll, this));
    getChild<LLButton>("capture_btn")->setClickedCallback(boost::bind(&ALFloaterXUITool::capturePreview, this));

    LLCheckBoxCtrl* hover = getChild<LLCheckBoxCtrl>("hover_check");
    hover->setValue(mHoverHighlight);
    hover->setCommitCallback(boost::bind(&ALFloaterXUITool::onToggleHover, this));
    LLCheckBoxCtrl* code_built = getChild<LLCheckBoxCtrl>("code_built_check");
    code_built->setValue(mShowCodeBuilt);
    code_built->setCommitCallback(boost::bind(&ALFloaterXUITool::onToggleCodeBuilt, this));
    mSecondaryCheck->setValue(mShowSecondary);
    mLanguageCombo2->setEnabled(mShowSecondary);

    mSelection.onSelectionChanged(boost::bind(&ALFloaterXUITool::onSelectionChanged, this));
    mSelection.onHoverChanged(boost::bind(&ALFloaterXUITool::onHoverChanged, this));
    mModel.setHoverHandler(boost::bind(&ALFloaterXUITool::onTreeHover, this, _1));
    mModel.setBadgeProvider([this](const ALXUISelection::path_t& path)
                            { return mPreviews[PRIMARY].lint.countUnder(path); });
    mModel.getFilter().setShowCodeBuilt(mShowCodeBuilt);
    mModel.getFilter().setEmptyLookupMessage(getString("NoResults"));

    if (!mFile.empty())
    {
        showPreviews();
    }
    else
    {
        setStatus(getString("NoFile"));
    }
    return true;
}

void ALFloaterXUITool::onClose(bool app_quitting)
{
    saveState();
    closePreviews();
}

void ALFloaterXUITool::draw()
{
    if (!mLintQueue.empty())
    {
        stepLintAll();
    }
    if (mReloadPending)
    {
        mReloadPending = false;
        scanCatalog();
        showPreviews();
        setStatus(getString("Reloaded"));
    }
    if (mTree)
    {
        mTree->update();
    }
    if (mInspectors->getCurrentPanel() && mInspectors->getCurrentPanel()->getName() == "state_tab"
        && mStateTimer.getElapsedTimeF32() > 0.25f)
    {
        mStateTimer.reset();
        refreshState(selectedView());
    }
    LLFloater::draw();
}

bool ALFloaterXUITool::handleKeyHere(KEY key, MASK mask)
{
    if (key == 'F' && mask == MASK_CONTROL)
    {
        mTreeFilter->setFocus(true);
        return true;
    }
    return LLFloater::handleKeyHere(key, mask);
}

// ---------------------------------------------------------------------------
// The catalog pane
// ---------------------------------------------------------------------------
void ALFloaterXUITool::scanCatalog()
{
    mCatalog.scan(gDirUtilp->getSkinBaseDir());
    fillSkinsAndLanguages();
    fillCatalog();
}

void ALFloaterXUITool::fillSkinsAndLanguages()
{
    mSkinCombo->removeall();
    for (const std::string& skin : mCatalog.skins())
    {
        mSkinCombo->add(skin, LLSD(skin));
    }
    if (std::find(mCatalog.skins().begin(), mCatalog.skins().end(), mSkin) == mCatalog.skins().end())
    {
        mSkin = "default";
    }
    mSkinCombo->setValue(mSkin);

    for (LLComboBox* combo : { mLanguageCombo, mLanguageCombo2 })
    {
        combo->removeall();
        for (const std::string& language : mCatalog.languages())
        {
            combo->add(language, LLSD(language));
        }
    }
    const std::vector<std::string>& languages = mCatalog.languages();
    if (std::find(languages.begin(), languages.end(), mLanguage) == languages.end())
    {
        mLanguage = "en";
    }
    if (std::find(languages.begin(), languages.end(), mLanguage2) == languages.end())
    {
        mLanguage2 = "en";
    }
    mLanguageCombo->setValue(mLanguage);
    mLanguageCombo2->setValue(mLanguage2);
}

// static
LLSD ALFloaterXUITool::row(const LLSD& id, std::initializer_list<std::pair<const char*, std::string>> cells)
{
    LLSD r;
    r["id"] = id;
    S32 i = 0;
    for (const auto& [column, value] : cells)
    {
        r["columns"][i]["column"] = column;
        r["columns"][i]["value"] = value;
        ++i;
    }
    return r;
}

void ALFloaterXUITool::fillCatalog()
{
    const std::string filter = utf8str_tolower(mCatalogFilter->getText());
    mFileList->deleteAllItems();
    for (const ALXUICatalog::Entry& e : mCatalog.entries())
    {
        const char* kind = ALXUICatalog::kindName(e.kind);
        if (!filter.empty()
            && utf8str_tolower(e.name).find(filter) == std::string::npos
            && utf8str_tolower(e.title).find(filter) == std::string::npos
            && filter != kind)
        {
            continue;
        }
        const bool has_language = mLanguage != "en"
            && (e.layer(mSkin, mLanguage) || e.layer("default", mLanguage));
        const bool has_skin = mSkin != "default"
            && (e.layer(mSkin, "en") || e.layer(mSkin, mLanguage));
        mFileList->addElement(row(e.name, {
            { "kind", kind },
            { "name", e.name },
            { "lang", has_language ? "x" : "" },
            { "skin", has_skin ? "x" : "" } }));
    }
    if (!mFile.empty())
    {
        mFileList->setSelectedByValue(mFile, true);
    }
}

void ALFloaterXUITool::onCatalogFilter()
{
    fillCatalog();
}

void ALFloaterXUITool::onFileSelected()
{
    const std::string file = mFileList->getSelectedValue().asString();
    if (file.empty() || file == mFile)
    {
        return;
    }
    mFile = file;
    mSelection.clearSelection();
    saveState();
    showPreviews();
}

void ALFloaterXUITool::onSkinOrLanguage()
{
    mSkin = mSkinCombo->getValue().asString();
    mLanguage = mLanguageCombo->getValue().asString();
    mLanguage2 = mLanguageCombo2->getValue().asString();
    saveState();
    fillCatalog();
    if (!mFile.empty())
    {
        showPreviews();
    }
}

void ALFloaterXUITool::onFind()
{
    const std::string query = mFindQuery->getText();
    mFindResults->deleteAllItems();
    if (query.empty())
    {
        return;
    }
    std::vector<ALXUICatalog::Hit> hits = mCatalog.find(query, fieldFrom(mFindField->getValue().asString()));
    S32 shown = 0;
    for (const ALXUICatalog::Hit& hit : hits)
    {
        if (shown++ >= MAX_FIND_ROWS)
        {
            break;
        }
        LLSD id;
        id["file"] = hit.entry->name;
        id["skin"] = hit.layer->skin;
        id["language"] = hit.layer->language;
        id["line"] = hit.line;
        id["path"] = hit.path;
        mFindResults->addElement(row(id, {
            { "file", hit.entry->name },
            { "line", std::to_string(hit.line) },
            { "layer", hit.layer->skin + "/" + hit.layer->language },
            { "snippet", hit.snippet } }));
    }
    setStatus(std::to_string(hits.size()) + (hits.size() == 1 ? " match" : " matches")
              + (hits.size() > (size_t)MAX_FIND_ROWS ? ", the first " + std::to_string(MAX_FIND_ROWS) + " listed" : ""));
}

void ALFloaterXUITool::onFindResult()
{
    LLScrollListItem* item = mFindResults->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD id = item->getValue();
    const std::string file = id["file"].asString();
    const std::string language = id["language"].asString();
    if (language != "en" && language != mLanguage)
    {
        mLanguage = language;
        mLanguageCombo->setValue(mLanguage);
    }
    if (file != mFile)
    {
        mFile = file;
        mFileList->setSelectedByValue(mFile, true);
        showPreviews();
    }
    mSelection.select(ALXUISelection::fromString(id["path"].asString()));
    if (!selectedView())
    {
        // Not a built element: open the file at the line instead.
        openInEditor(id["path"].asString().empty() ? std::string() : std::string(), 0);
        const ALXUICatalog::Entry* entry = mCatalog.find(file);
        if (const ALXUICatalog::Layer* layer = entry ? entry->layer(id["skin"].asString(), language) : nullptr)
        {
            mSourcePath = layer->path;
            mSourceLine = id["line"].asInteger();
        }
    }
}

// ---------------------------------------------------------------------------
// Previews
// ---------------------------------------------------------------------------
void ALFloaterXUITool::closePreview(S32 which)
{
    Preview& pv = mPreviews[which];
    if (LLFloater* host = pv.host.get())
    {
        if (which == PRIMARY)
        {
            const LLRect r = host->calcScreenRect();
            mLastX = r.mLeft;
            mLastY = r.mBottom;
        }
        static_cast<ALXUIPreviewHost*>(host)->detach();
        host->closeFloater();
    }
    pv.host.markDead();
    pv.root = nullptr;
    pv.node = nullptr;
    pv.sourceMap.clear();
    pv.overlay.clear();
    pv.liveFiles.clear();
    pv.diagnostics.clear();
    pv.lint.clear();
    if (which == PRIMARY)
    {
        clearTree();
    }
}

void ALFloaterXUITool::closePreviews()
{
    for (S32 i = 0; i < PREVIEWS; ++i)
    {
        closePreview(i);
    }
}

void ALFloaterXUITool::hostClosed(S32 which)
{
    Preview& pv = mPreviews[which];
    if (which == PRIMARY && pv.root)
    {
        const LLRect r = pv.root->calcScreenRect();
        mLastX = r.mLeft;
        mLastY = r.mBottom;
    }
    pv.host.markDead();
    pv.root = nullptr;
    pv.node = nullptr;
    pv.sourceMap.clear();
    pv.overlay.clear();
    pv.liveFiles.clear();
    if (which == PRIMARY)
    {
        clearTree();
        refreshBreadcrumb();
        refreshInspectors();
    }
}

void ALFloaterXUITool::showPreviews()
{
    showPreview(PRIMARY);
    if (mShowSecondary)
    {
        showPreview(SECONDARY);
    }
    else
    {
        closePreview(SECONDARY);
    }
}

void ALFloaterXUITool::placeHost(S32 which, LLFloater* host)
{
    if (which == PRIMARY)
    {
        if (mLastX >= 0)
        {
            host->setOrigin(mLastX, mLastY);
        }
        else
        {
            host->center();
        }
    }
    else if (LLFloater* primary = mPreviews[PRIMARY].host.get())
    {
        const LLRect p = primary->getRect();
        host->setOrigin(p.mRight + 8, p.mTop - host->getRect().getHeight());
    }
    else
    {
        host->center();
    }
    gFloaterView->adjustToFitScreen(host, false);
}

LLView* ALFloaterXUITool::buildRoot(S32 which, const ALXUICatalog::Entry& entry, ALXUIPreviewHost* host, LLXMLNodePtr& node)
{
    LLUICtrlFactory& factory = LLUICtrlFactory::instance();
    const std::string& file = entry.name;

    if (entry.kind == ALXUICatalog::Kind::Template)
    {
        // The widget the template is for, with nothing but its defaults.
        std::string tag = file.substr(file.rfind('/') + 1);
        tag = tag.substr(0, tag.size() - 4);
        const std::string xml = "<" + tag + " name=\"" + tag + "\" label=\"" + tag
                              + "\" layout=\"topleft\" left=\"8\" top=\"8\" width=\"200\" height=\"24\"/>";
        if (!LLXMLNode::parseBuffer(xml.data(), xml.size(), node))
        {
            return nullptr;
        }
    }
    else
    {
        // The viewer's own layers in the viewer's own order, merged by
        // its own call, with the tool's observer recording what each
        // layer wrote and what it dropped.
        std::vector<std::string> paths = gDirUtilp->findSkinnedFilenames(LLDir::XUI, file);
        if (paths.empty())
        {
            paths.push_back(file);
        }
        if (!ALXmlLayerMerge::load(paths, node, &mPreviews[which].overlay))
        {
            return nullptr;
        }
    }
    return buildFromNode(entry, host, node);
}

// The node as a view, by the kind of file it is. A floater is the host
// itself; everything else is hosted by it.
LLView* ALFloaterXUITool::buildFromNode(const ALXUICatalog::Entry& entry, ALXUIPreviewHost* host, LLXMLNodePtr node)
{
    LLUICtrlFactory& factory = LLUICtrlFactory::instance();
    const std::string& file = entry.name;

    LLView* root = nullptr;
    factory.pushFileName(file);
    switch (entry.kind)
    {
    case ALXUICatalog::Kind::Floater:
        if (host->initFloaterXML(node, gFloaterView, file))
        {
            root = host;
            host->setCanResize(host->isResizable());
        }
        break;

    case ALXUICatalog::Kind::Panel:
    {
        LLPanel::Params pp;
        LLPanel* panel = LLUICtrlFactory::create<LLPanel>(pp);
        if (panel->initPanelXML(node, host, LLUICtrlFactory::getDefaultParams<LLPanel>()))
        {
            panel->setOrigin(2, 2);
            panel->setUseBoundingRect(true);
            panel->updateBoundingRect();
            LLRect fit = panel->getRect();
            fit.unionWith(panel->getBoundingRect());
            panel->reshape(fit.getWidth(), fit.getHeight());
            host->reshape(fit.getWidth() + 4, fit.getHeight() + 4 + LLFloater::getDefaultParams().header_height);
            host->setCanResize(true);
            root = panel;
        }
        else
        {
            delete panel;
        }
        break;
    }

    case ALXUICatalog::Kind::Menu:
    {
        LLMenuHolderGL::Params hp;
        hp.name = "menu_holder";
        hp.rect = host->getLocalRect();
        hp.follows.flags = FOLLOWS_ALL;
        LLMenuHolderGL* holder = LLUICtrlFactory::create<LLMenuHolderGL>(hp);
        holder->setCanHide(false);
        host->addChild(holder);
        LLView* view = factory.createFromXML(node, holder, file, LLMenuHolderGL::child_registry_t::instance());
        if (LLMenuGL* menu = view ? view->as<LLMenuGL>() : nullptr)
        {
            menu->setVisible(true);
            if (!menu->as<LLMenuBarGL>())
            {
                menu->needsArrange();
                menu->arrangeAndClear();
            }
            const S32 header = LLFloater::getDefaultParams().header_height;
            const LLRect r = menu->getRect();
            host->reshape(llmax(r.getWidth() + 8, 120), r.getHeight() + 8 + header);
            holder->reshape(host->getRect().getWidth(), host->getRect().getHeight() - header);
            menu->setOrigin(4, holder->getRect().getHeight() - r.getHeight() - 4);
            root = menu;
        }
        else if (view)
        {
            root = view;
        }
        break;
    }

    case ALXUICatalog::Kind::Widget:
    case ALXUICatalog::Kind::Template:
    {
        LLView* view = factory.createFromXML(node, host, file, LLDefaultChildRegistry::instance());
        if (view)
        {
            const S32 header = LLFloater::getDefaultParams().header_height;
            LLRect r = view->getRect();
            host->reshape(llmax(r.getWidth() + 16, 120), llmax(r.getHeight() + 16, 40) + header);
            view->setOrigin(8, host->getRect().getHeight() - header - r.getHeight() - 8);
            host->setCanResize(true);
            root = view;
        }
        break;
    }

    default:
        break;
    }
    factory.popFileName();
    return root;
}

void ALFloaterXUITool::showPreview(S32 which)
{
    closePreview(which);
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry)
    {
        setStatus(getString("NoFile"));
        return;
    }
    Preview& pv = mPreviews[which];
    pv.skin = mSkin;
    pv.language = which == PRIMARY ? mLanguage : mLanguage2;

    // A widget file or template names its tag in its root or its name;
    // a viewer widget's constructor is not run by a shell build.
    std::string widget_tag;
    if (entry->kind == ALXUICatalog::Kind::Widget)
    {
        widget_tag = entry->rootTag;
    }
    else if (entry->kind == ALXUICatalog::Kind::Template)
    {
        widget_tag = mFile.substr(mFile.rfind('/') + 1);
        widget_tag = widget_tag.substr(0, widget_tag.size() - 4);
    }
    const bool viewer_widget = !widget_tag.empty() && !isCoreWidgetTag(widget_tag);

    if (!isBuilt(entry->kind) || viewer_widget)
    {
        if (which == PRIMARY)
        {
            LLStringUtil::format_map_t args;
            args["[TAG]"] = widget_tag;
            setStatus(viewer_widget ? getString("ViewerWidget", args) : getString("NotBuilt"));
            runLint();
            fillFindings();
            refreshBreadcrumb();
            refreshInspectors();
        }
        return;
    }

    ALXUIPreviewHost* host = nullptr;
    LLView* root = nullptr;
    LLXMLNodePtr node;
    LLTimer timer;
    {
        ALXUISkinScope scope(pv.skin, pv.language);
        ALXUIShellBuild shell;
        ALXUIDiagnostics sink;

        LLFloater::Params p(LLFloater::getDefaultParams());
        p.min_height = p.header_height;
        p.min_width = 10;
        host = new ALXUIPreviewHost(this, which, p);
        root = buildRoot(which, *entry, host, node);
        pv.diagnostics = sink.entries();
    }
    pv.seconds = timer.getElapsedTimeF32();

    if (!root)
    {
        host->detach();
        host->closeFloater();
        if (which == PRIMARY)
        {
            LLStringUtil::format_map_t args;
            args["[FILE]"] = mFile;
            setStatus(getString("BuildFailed", args));
            runLint();
            fillFindings();
        }
        return;
    }

    host->setRoot(root);
    std::string title = root == host ? host->getTitle() : mFile;
    title += " [" + pv.skin + "/" + pv.language + (which == PRIMARY ? "" : ", second") + "]";
    host->setTitle(title);
    pv.host = host->getHandle();
    pv.root = root;
    pv.node = node;
    pv.views = countViews(root);
    pv.sourceMap.build(root, node);
    placeHost(which, host);
    host->openFloater();

    if (which == PRIMARY)
    {
        watchFiles(*entry);
        // Before the tree: a row shows the findings under it, and the
        // rows are made once.
        runLint();
        rebuildTree();
        fillFindings();
        const S32 findings = (S32)pv.lint.findings().size();
        LLStringUtil::format_map_t args;
        args["[VIEWS]"] = std::to_string(pv.views);
        args["[MS]"] = std::to_string((S32)(pv.seconds * 1000.f));
        args["[DIAG]"] = std::to_string(findings);
        args["[SKIN]"] = pv.skin;
        args["[LANG]"] = pv.language;
        setStatus(getString("Built", args));
        // The selection is a path; it may name something in the new tree.
        onSelectionChanged();
    }
}

void ALFloaterXUITool::watchFiles(const ALXUICatalog::Entry& entry)
{
    Preview& pv = mPreviews[PRIMARY];
    pv.liveFiles.clear();
    for (const ALXUICatalog::Layer* layer : mCatalog.layersFor(entry, pv.skin, pv.language))
    {
        auto live = std::make_unique<ALXUILiveFile>(layer->path, this);
        live->checkAndReload();
        live->addToEventTimer();
        pv.liveFiles.push_back(std::move(live));
    }
}

void ALFloaterXUITool::fileChanged()
{
    // The check runs from a timer; the rebuild waits for the next frame.
    mReloadPending = true;
}

void ALFloaterXUITool::showGallery()
{
    ALXUIPreviewHost* host = nullptr;
    LLScrollContainer* scroller = nullptr;
    LLPanel* content = nullptr;
    S32 built = 0;
    {
        ALXUISkinScope scope(mSkin, mLanguage);
        ALXUIShellBuild shell;
        ALXUIDiagnostics sink;

        LLFloater::Params p(LLFloater::getDefaultParams());
        p.min_height = 100;
        p.min_width = 200;
        host = new ALXUIPreviewHost(this, SECONDARY, p);
        const S32 header = p.header_height;
        host->reshape(900, 640 + header);
        host->setCanResize(true);
        host->setTitle("Widget gallery [" + mSkin + "/" + mLanguage + "]");

        // A child registry keeps its static registrations in a scope of
        // their own; the widget type registry lists every tag, and the
        // default child registry says which of them it builds.
        std::vector<std::string> tags;
        const auto& registrar = LLWidgetTypeRegistry::instance().defaultRegistrar();
        for (auto it = registrar.beginItems(); it != registrar.endItems(); ++it)
        {
            if (galleryTag(it->first) && LLDefaultChildRegistry::instance().getValue(it->first))
            {
                tags.push_back(it->first);
            }
        }
        std::sort(tags.begin(), tags.end());

        constexpr S32 COLUMNS = 4;
        constexpr S32 CELL_W = 220;
        constexpr S32 CELL_H = 64;
        const S32 rows = ((S32)tags.size() + COLUMNS - 1) / COLUMNS;
        const S32 content_h = rows * CELL_H + 8;

        LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
        sp.name = "gallery_scroller";
        sp.rect = LLRect(0, host->getRect().getHeight() - header, host->getRect().getWidth(), 0);
        sp.follows.flags = FOLLOWS_ALL;
        scroller = LLUICtrlFactory::create<LLScrollContainer>(sp);
        host->addChild(scroller);

        LLPanel::Params cp;
        cp.name = "gallery";
        cp.rect = LLRect(0, content_h, COLUMNS * CELL_W + 8, 0);
        content = LLUICtrlFactory::create<LLPanel>(cp);
        scroller->addChild(content);

        LLUICtrlFactory& factory = LLUICtrlFactory::instance();
        S32 i = 0;
        for (const std::string& tag : tags)
        {
            const S32 col = i % COLUMNS;
            const S32 r = i / COLUMNS;
            const S32 left = 8 + col * CELL_W;
            const S32 top = 4 + r * CELL_H;
            ++i;

            const std::string label_xml = "<text name=\"label_" + tag + "\" layout=\"topleft\" left=\"" + std::to_string(left)
                + "\" top=\"" + std::to_string(top) + "\" width=\"" + std::to_string(CELL_W - 16) + "\" height=\"14\" font=\"SansSerifSmall\">"
                + tag + "</text>";
            LLXMLNodePtr label_node;
            if (LLXMLNode::parseBuffer(label_xml.data(), label_xml.size(), label_node))
            {
                factory.createFromXML(label_node, content, "gallery", LLDefaultChildRegistry::instance());
            }

            const std::string xml = "<" + tag + " name=\"" + tag + "\" label=\"" + tag + "\" layout=\"topleft\" left=\""
                + std::to_string(left) + "\" top=\"" + std::to_string(top + 16) + "\" width=\"" + std::to_string(CELL_W - 16)
                + "\" height=\"24\"/>";
            LLXMLNodePtr node;
            if (LLXMLNode::parseBuffer(xml.data(), xml.size(), node))
            {
                factory.pushFileName("gallery");
                built += factory.createFromXML(node, content, "gallery", LLDefaultChildRegistry::instance()) != nullptr;
                factory.popFileName();
            }
        }
    }
    host->setRoot(content);
    host->detach();
    host->center();
    gFloaterView->adjustToFitScreen(host, false);
    host->openFloater();
    setStatus("Gallery: " + std::to_string(built) + " widgets in " + mSkin + "/" + mLanguage);
}

// ---------------------------------------------------------------------------
// The canvas
// ---------------------------------------------------------------------------
void ALFloaterXUITool::canvasHover(S32 which, const LLView* view)
{
    ALXUISelection::path_t path;
    if (view && ALXUISelection::pathOf(view, mPreviews[which].root, path))
    {
        mSelection.setHover(path);
    }
    else
    {
        mSelection.clearHover();
    }
}

void ALFloaterXUITool::canvasSelect(S32 which, const LLView* view)
{
    ALXUISelection::path_t path;
    if (view && ALXUISelection::pathOf(view, mPreviews[which].root, path))
    {
        mSelection.select(path);
    }
}

// ---------------------------------------------------------------------------
// The tree pane
// ---------------------------------------------------------------------------
void ALFloaterXUITool::clearTree()
{
    mRows.clear();
    mModel.setCanvasHover(nullptr);
    mModel.clear();
    if (mTree)
    {
        mTreePanel->deleteAllChildren();
        mTree = nullptr;
    }
}

void ALFloaterXUITool::rebuildTree()
{
    clearTree();
    Preview& pv = mPreviews[PRIMARY];
    ALXUITreeItem* root_item = mModel.build(pv.root, pv.sourceMap);
    if (!root_item)
    {
        return;
    }

    LLFolderView::Params p(LLUICtrlFactory::getDefaultParams<LLFolderView>());
    p.name = "xui_tree";
    p.title = root_item->getName();
    p.rect = LLRect(0, 0, mTreePanel->getRect().getWidth(), 0);
    p.parent_panel = mTreePanel;
    p.listener = root_item;
    p.view_model = &mModel;
    p.root = nullptr;
    p.use_ellipses = true;
    p.options_menu = "menu_xui_tool_tree.xml";
    mTree = LLUICtrlFactory::create<LLFolderView>(p);
    mTree->setCallbackRegistrar(&mCommitCallbackRegistrar);
    mTree->setEnableRegistrar(&mEnableCallbackRegistrar);

    LLRect scroller_rect = mTreePanel->getLocalRect();
    LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLFolderViewScrollContainer>());
    sp.rect(scroller_rect);
    LLScrollContainer* scroller = LLUICtrlFactory::create<LLFolderViewScrollContainer>(sp);
    scroller->setFollowsAll();
    mTreePanel->addChild(scroller);
    scroller->addChild(mTree);
    mTree->setScrollContainer(scroller);
    mTree->setFollowsAll();
    mTree->addChild(mTree->mStatusTextBox);
    mTree->setSelectCallback(boost::bind(&ALFloaterXUITool::onTreeSelection, this, _1, _2));
    mModel.setFolderView(mTree);

    createRows(root_item, mTree);
    mTree->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_DOWN);
    mTree->arrangeAll();
    mModel.getFilter().setModified();
}

void ALFloaterXUITool::createRows(ALXUITreeItem* item, LLFolderViewFolder* parent_widget)
{
    static const LLUIColor from_xml_color = LLUIColorTable::instance().getColor("MenuItemEnabledColor", LLColor4::white);
    static const LLUIColor code_built_color = LLUIColorTable::instance().getColor("MenuItemDisabledColor", LLColor4::grey);
    static const LLUIColor highlight_color = LLUIColorTable::instance().getColor("MenuItemHighlightColor", LLColor4::white);

    for (auto it = item->getChildrenBegin(); it != item->getChildrenEnd(); ++it)
    {
        ALXUITreeItem* child = static_cast<ALXUITreeItem*>(it->get());
        LLFolderViewItem::Params params(LLUICtrlFactory::getDefaultParams<LLFolderViewItem>());
        params.name = child->getName();
        params.root = mTree;
        params.listener = child;
        params.tool_tip = ALXUISelection::toString(child->getPath());
        params.text_pad_right = ALXUITreeEye::WIDTH + 4;
        params.font_color = child->isFromXML() ? from_xml_color : code_built_color;
        params.font_highlight_color = highlight_color;

        LLFolderViewItem* widget;
        if (child->hasChildren())
        {
            ALXUITreeFolder* folder = LLUICtrlFactory::create<ALXUITreeFolder>(params);
            folder->setChildrenInited(true);
            widget = folder;
        }
        else
        {
            widget = LLUICtrlFactory::create<ALXUITreeRow>(params);
        }
        widget->addToFolder(parent_widget);
        mRows[ALXUISelection::toString(child->getPath())] = widget;
        if (child->hasChildren())
        {
            createRows(child, static_cast<LLFolderViewFolder*>(widget));
        }
    }
}

void ALFloaterXUITool::onTreeFilter()
{
    mModel.getFilter().setFilterSubString(mTreeFilter->getText());
}

void ALFloaterXUITool::onTreeSelection(const std::deque<LLFolderViewItem*>& items, bool user_action)
{
    if (mSyncingTree || items.empty() || !items.front())
    {
        return;
    }
    ALXUITreeItem* item = static_cast<ALXUITreeItem*>(items.front()->getViewModelItem());
    if (!item)
    {
        return;
    }
    mSyncingTree = true;
    mSelection.select(item->getPath());
    mSyncingTree = false;
}

void ALFloaterXUITool::onTreeHover(const ALXUITreeItem* item)
{
    if (item)
    {
        mSelection.setHover(item->getPath());
    }
    else
    {
        mSelection.clearHover();
    }
}

ALXUITreeItem* ALFloaterXUITool::selectedItem() const
{
    return mSelection.hasSelection() ? mModel.itemFor(mSelection.selection()) : nullptr;
}

bool ALFloaterXUITool::onTreeActionEnabled(const LLSD& param)
{
    const std::string action = param.asString();
    if (action == "reveal")
    {
        ALXUITreeItem* item = selectedItem();
        return item && item->isFromXML();
    }
    return true;
}

void ALFloaterXUITool::onTreeAction(const LLSD& param)
{
    const std::string action = param.asString();
    ALXUITreeItem* item = selectedItem();
    if (action == "expand_all" || action == "collapse_all")
    {
        if (mTree)
        {
            mTree->setOpenArrangeRecursively(action == "expand_all", LLFolderViewFolder::RECURSE_DOWN);
            mTree->arrangeAll();
        }
        return;
    }
    if (!item)
    {
        return;
    }
    if (action == "reveal")
    {
        onJumpToSource();
    }
    else if (action == "copy_path")
    {
        const std::string text = ALXUISelection::toString(item->getPath());
        LLClipboard::instance().copyToClipboard(text, 0, (S32)text.size());
    }
    else if (action == "copy_getchild")
    {
        std::string type = item->getView()->viewType()->mName;
        const std::string text = "getChild<" + type + ">(\"" + item->getName() + "\")";
        LLClipboard::instance().copyToClipboard(text, 0, (S32)text.size());
    }
    else if (action == "toggle_visible")
    {
        item->toggleShown();
    }
}

// The preview as it stands on screen, cropped out of a snapshot of the
// window with the UI drawn. The preview is brought to the front first,
// since what is over it is what would be captured.
void ALFloaterXUITool::capturePreview()
{
    LLFloater* host = mPreviews[PRIMARY].host.get();
    if (!host)
    {
        setStatus(getString("NoFile"));
        return;
    }
    host->setFrontmost(false);

    const S32 window_width = gViewerWindow->getWindowWidthRaw();
    const S32 window_height = gViewerWindow->getWindowHeightRaw();
    LLPointer<LLImageRaw> shot = new LLImageRaw;
    if (!gViewerWindow->rawSnapshot(shot, window_width, window_height, /*keep_window_aspect=*/true,
                                    /*is_texture=*/false, /*show_ui=*/true, /*show_hud=*/false))
    {
        setStatus("The window would not give a snapshot.");
        return;
    }

    // The floater's rect in the window, in the snapshot's own scale: a
    // snapshot may come back at a different size than the window.
    const LLRect screen = host->calcScreenRect();
    const F32 scale_x = (F32)shot->getWidth() / (F32)llmax(1, gViewerWindow->getWindowWidthScaled());
    const F32 scale_y = (F32)shot->getHeight() / (F32)llmax(1, gViewerWindow->getWindowHeightScaled());
    const S32 left = llclamp((S32)(screen.mLeft * scale_x), 0, shot->getWidth());
    const S32 right = llclamp((S32)(screen.mRight * scale_x), left, shot->getWidth());
    const S32 bottom = llclamp((S32)(screen.mBottom * scale_y), 0, shot->getHeight());
    const S32 top = llclamp((S32)(screen.mTop * scale_y), bottom, shot->getHeight());
    const S32 width = right - left;
    const S32 height = top - bottom;
    if (width <= 0 || height <= 0)
    {
        setStatus("The preview is off screen.");
        return;
    }

    // Row zero of the snapshot is the bottom of the window, which is
    // where the rect's bottom is too.
    const U8 components = shot->getComponents();
    LLPointer<LLImageRaw> cropped = new LLImageRaw(width, height, components);
    for (S32 row = 0; row < height; ++row)
    {
        memcpy(cropped->getData() + (size_t)row * width * components,
               shot->getData() + ((size_t)(bottom + row) * shot->getWidth() + left) * components,
               (size_t)width * components);
    }

    mCapture = cropped;

    // The file and the format are one question: the extension the author
    // types is what the image is written as.
    std::string name = mFile;
    for (char& c : name)
    {
        if (c == '/' || c == '\\' || c == '.')
        {
            c = '_';
        }
    }
    name += "_" + mPreviews[PRIMARY].skin + "_" + mPreviews[PRIMARY].language + ".png";
    LLFilePickerReplyThread::startPicker(boost::bind(&ALFloaterXUITool::writeCapture, this, _1),
                                         LLFilePicker::FFSAVE_ALL, name);
}

void ALFloaterXUITool::writeCapture(const std::vector<std::string>& filenames)
{
    if (filenames.empty() || mCapture.isNull())
    {
        return;
    }
    const std::string path = filenames.front();

    std::string extension = gDirUtilp->getExtension(path);
    LLStringUtil::toLower(extension);
    LLPointer<LLImageFormatted> image;
    if (extension == "jpg" || extension == "jpeg")
    {
        image = new LLImageJPEG(gSavedSettings.getS32("SnapshotQuality"));
    }
    else if (extension == "bmp")
    {
        image = new LLImageBMP;
    }
    else if (extension == "tga")
    {
        image = new LLImageTGA;
    }
    else if (extension == "j2c" || extension == "jp2")
    {
        image = new LLImageJ2C;
    }
    else
    {
        // Including no extension at all: a picture of a floater is a PNG
        // unless the author says otherwise.
        image = new LLImagePNG;
    }

    if (!image->encode(mCapture, 0.f) || !image->save(path))
    {
        setStatus("Could not write " + path);
        return;
    }
    setStatus("Captured " + std::to_string(mCapture->getWidth()) + " by "
              + std::to_string(mCapture->getHeight()) + " to " + path);
    mCapture = nullptr;
}

// Every file in the catalog, checked a few per frame. The status line
// counts down and the report lands beside the log, which is the form an
// author can read a whole tree's worth of findings in.
void ALFloaterXUITool::startLintAll()
{
    if (!mLintQueue.empty())
    {
        mLintQueue.clear();
        setStatus("Lint all: stopped.");
        return;
    }
    mLintReport.clear();
    mLintByRule.clear();
    mLintFiles = 0;
    mLintFindings = 0;
    for (const ALXUICatalog::Entry& entry : mCatalog.entries())
    {
        mLintQueue.push_back(entry.name);
    }
    mLintTotal = (S32)mLintQueue.size();

    // The rule that needs no build, once, before the files are walked.
    for (const ALXUILint::Finding& f : ALXUILint::checkCatalog(mCatalog))
    {
        ++mLintByRule[ALXUILint::ruleName(f.rule)];
        ++mLintFindings;
        mLintReport.push_back(std::string(ALXUILint::severityName(f.severity)) + " " + ALXUILint::ruleName(f.rule)
                              + " " + f.file + ":" + std::to_string(f.line) + " " + f.message);
    }
    setStatus("Lint all: " + std::to_string(mLintTotal) + " files...");
}

void ALFloaterXUITool::stepLintAll()
{
    // A budget per frame rather than a count of files: the largest file
    // takes as long as twenty small ones.
    constexpr F32 BUDGET = 0.015f;
    LLTimer timer;
    while (!mLintQueue.empty() && timer.getElapsedTimeF32() < BUDGET)
    {
        const std::string name = mLintQueue.front();
        mLintQueue.pop_front();
        if (const ALXUICatalog::Entry* entry = mCatalog.find(name))
        {
            ++mLintFiles;
            mLintFindings += lintOneFile(*entry, mLintReport);
        }
    }

    if (mLintQueue.empty())
    {
        finishLintAll();
    }
    else
    {
        setStatus("Lint all: " + std::to_string(mLintTotal - (S32)mLintQueue.size()) + " of "
                  + std::to_string(mLintTotal) + " files, " + std::to_string(mLintFindings) + " findings...");
    }
}

S32 ALFloaterXUITool::lintOneFile(const ALXUICatalog::Entry& entry, std::vector<std::string>& lines)
{
    std::string widget_tag;
    if (entry.kind == ALXUICatalog::Kind::Widget)
    {
        widget_tag = entry.rootTag;
    }
    else if (entry.kind == ALXUICatalog::Kind::Template)
    {
        widget_tag = entry.name.substr(entry.name.rfind('/') + 1);
        widget_tag = widget_tag.substr(0, widget_tag.size() - 4);
    }
    if (!isBuilt(entry.kind) || (!widget_tag.empty() && !isCoreWidgetTag(widget_tag)))
    {
        return 0;
    }

    ALXUIPreviewHost* host = nullptr;
    LLView* root = nullptr;
    LLXMLNodePtr node;
    ALXUIOverlay overlay;
    std::vector<ALXUIDiagnostics::Entry> entries;
    {
        ALXUISkinScope scope(mSkin, mLanguage);
        ALXUIShellBuild shell;
        ALXUIDiagnostics sink;

        std::vector<std::string> paths = gDirUtilp->findSkinnedFilenames(LLDir::XUI, entry.name);
        if (paths.empty())
        {
            paths.push_back(entry.name);
        }
        if (entry.kind == ALXUICatalog::Kind::Template)
        {
            const std::string xml = "<" + widget_tag + " name=\"" + widget_tag + "\" layout=\"topleft\""
                                  + " left=\"8\" top=\"8\" width=\"200\" height=\"24\"/>";
            LLXMLNode::parseBuffer(xml.data(), xml.size(), node);
        }
        else
        {
            ALXmlLayerMerge::load(paths, node, &overlay);
        }

        if (node.notNull())
        {
            LLFloater::Params p(LLFloater::getDefaultParams());
            p.min_height = p.header_height;
            p.min_width = 10;
            host = new ALXUIPreviewHost(this, SECONDARY, p);
            host->detach();
            root = buildFromNode(entry, host, node);
        }
        entries = sink.entries();
    }

    S32 found = 0;
    if (root)
    {
        ALXUISourceMap map;
        map.build(root, node);

        ALXUILint lint;
        ALXUILint::Input input;
        input.root = root;
        input.sourceMap = &map;
        input.diagnostics = &entries;
        input.overlay = &overlay;
        input.catalog = &mCatalog;
        input.file = entry.name;
        input.callbacksAreDecisive = entry.kind == ALXUICatalog::Kind::Menu;
        std::vector<const ALXUICatalog::Layer*> layers = mCatalog.layersFor(entry, mSkin, mLanguage);
        if (!layers.empty())
        {
            input.authored = layers.front()->root();
        }
        lint.run(input);

        for (const ALXUILint::Finding& f : lint.findings())
        {
            ++mLintByRule[ALXUILint::ruleName(f.rule)];
            ++found;
            lines.push_back(std::string(ALXUILint::severityName(f.severity)) + " " + ALXUILint::ruleName(f.rule)
                            + " " + entry.name + ":" + std::to_string(f.line) + " "
                            + ALXUISelection::toString(f.path) + " " + f.what + ": " + f.message);
        }
    }
    if (host)
    {
        host->closeFloater();
    }
    return found;
}

void ALFloaterXUITool::finishLintAll()
{
    const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "xui_lint.txt");
    llofstream out(path, std::ios::binary);
    out << mLintFiles << " files built in " << mSkin << "/" << mLanguage << ", "
        << mLintFindings << " findings\n\n";
    for (const auto& [rule, count] : mLintByRule)
    {
        out << "  " << count << "\t" << rule << "\n";
    }
    out << "\n";
    for (const std::string& line : mLintReport)
    {
        out << line << "\n";
    }

    setStatus("Lint all: " + std::to_string(mLintFindings) + " findings over " + std::to_string(mLintFiles)
              + " files; the report is " + path);
    LL_INFOS("XUITool") << "lint all: " << mLintFindings << " findings over " << mLintFiles
                        << " files, report at " << path << LL_ENDL;
}

void ALFloaterXUITool::runLint()
{
    Preview& pv = mPreviews[PRIMARY];
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    ALXUILint::Input input;
    input.root = pv.root;
    input.sourceMap = &pv.sourceMap;
    input.diagnostics = &pv.diagnostics;
    input.overlay = &pv.overlay;
    input.catalog = &mCatalog;
    input.file = mFile;
    // A menu file's functions are registered at startup, so a name no
    // registry knows is decisive there and nowhere else in a shell build.
    input.callbacksAreDecisive = entry && entry->kind == ALXUICatalog::Kind::Menu;
    if (entry)
    {
        // The file as written, which is where the parameter elements the
        // parser consumed still are.
        std::vector<const ALXUICatalog::Layer*> layers = mCatalog.layersFor(*entry, pv.skin, pv.language);
        if (!layers.empty())
        {
            input.authored = layers.front()->root();
        }
    }
    pv.lint.run(input);
}

// One row per finding: the severity and rule, where it is, and what it
// says. The path is what a double-click selects by, since a finding from
// a layer carries that layer's line and not the base's.
void ALFloaterXUITool::fillFindings()
{
    mFindings->deleteAllItems();
    const Preview& pv = mPreviews[PRIMARY];
    for (const ALXUILint::Finding& f : pv.lint.findings())
    {
        std::string where = ALXUISelection::toString(f.path);
        std::string file = f.file;
        const size_t slash = file.find_last_of("/\\");
        if (slash != std::string::npos)
        {
            file = file.substr(slash + 1);
        }
        if (where.empty())
        {
            where = f.what;
        }
        if (!file.empty() && file != mFile)
        {
            where = file + ": " + where;
        }
        LLSD id;
        id["path"] = ALXUISelection::toString(f.path);
        id["line"] = f.line;
        mFindings->addElement(row(id, {
            { "severity", ALXUILint::severityName(f.severity) },
            { "rule", ALXUILint::ruleName(f.rule) },
            { "line", f.line > 0 ? std::to_string(f.line) : std::string() },
            { "where", where },
            { "message", f.what.empty() ? f.message : f.what + ": " + f.message } }));
    }
}

void ALFloaterXUITool::onFindingSelected()
{
    LLScrollListItem* item = mFindings->getFirstSelected();
    if (!item)
    {
        return;
    }
    const LLSD id = item->getValue();
    const Preview& pv = mPreviews[PRIMARY];
    const std::string path = id["path"].asString();
    if (!path.empty())
    {
        mSelection.select(ALXUISelection::fromString(path));
        return;
    }
    const LLView* view = pv.sourceMap.viewAtLine(id["line"].asInteger());
    ALXUISelection::path_t found;
    if (view && ALXUISelection::pathOf(view, pv.root, found))
    {
        mSelection.select(found);
    }
}

// The findings on the selected element and everything below it.
void ALFloaterXUITool::refreshSelectionFindings()
{
    mSelectionFindings->deleteAllItems();
    if (!mSelection.hasSelection())
    {
        return;
    }
    const ALXUISelection::path_t& selected = mSelection.selection();
    for (const ALXUILint::Finding& f : mPreviews[PRIMARY].lint.findings())
    {
        if (f.path.size() < selected.size()
            || !std::equal(selected.begin(), selected.end(), f.path.begin()))
        {
            continue;
        }
        const bool here = f.path.size() == selected.size();
        mSelectionFindings->addElement(row(ALXUISelection::toString(f.path), {
            { "severity", ALXUILint::severityName(f.severity) },
            { "rule", ALXUILint::ruleName(f.rule) },
            { "where", here ? std::string("here")
                            : ALXUISelection::toString(ALXUISelection::path_t(f.path.begin() + selected.size(), f.path.end())) },
            { "message", f.what.empty() ? f.message : f.what + ": " + f.message } }));
    }
}

void ALFloaterXUITool::refreshBreadcrumb()
{
    mBreadcrumb->deleteAllChildren();
    const Preview& pv = mPreviews[PRIMARY];
    if (!pv.root || !mSelection.hasSelection())
    {
        return;
    }

    // One crumb per ancestor from the root down, then the layer the
    // element came from. The selection's end matters more than its
    // start, so when the chain is wider than the panel the first crumbs
    // fold into one that selects the last of them.
    const ALXUISelection::path_t& path = mSelection.selection();
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    const ALXUICatalog::Layer* layer = nullptr;
    authoredElement(layer);
    const std::string layer_text = layer ? layer->skin + "/" + layer->language : std::string("code-built");
    const S32 layer_width = font->getWidth(layer_text) + 12;
    const S32 available = mBreadcrumb->getRect().getWidth() - layer_width;

    std::vector<std::string> labels;
    std::vector<S32> widths;
    S32 total = 0;
    for (size_t i = 0; i <= path.size(); ++i)
    {
        labels.push_back(i == 0 ? pv.root->getName() : path[i - 1]);
        widths.push_back(font->getWidth(labels.back()) + 12);
        total += widths.back() + 2;
    }
    size_t first = 0;
    const S32 fold_width = font->getWidth("...") + 12 + 2;
    while (first + 1 < labels.size() && total + (first ? fold_width : 0) > available)
    {
        total -= widths[first] + 2;
        ++first;
    }

    S32 x = 0;
    const S32 height = mBreadcrumb->getRect().getHeight();
    auto crumb = [&](const std::string& label, S32 width, size_t depth)
    {
        ALXUISelection::path_t prefix(path.begin(), path.begin() + depth);
        LLButton::Params bp;
        bp.name = "crumb_" + std::to_string(depth);
        bp.label = label;
        bp.rect = LLRect(x, height, x + width, 0);
        bp.font = font;
        bp.tab_stop = false;
        LLButton* button = LLUICtrlFactory::create<LLButton>(bp);
        button->setClickedCallback([this, prefix](LLUICtrl*, const LLSD&) { mSelection.select(prefix); });
        mBreadcrumb->addChild(button);
        x += width + 2;
    };
    if (first > 0)
    {
        crumb("...", fold_width - 2, first - 1);
    }
    for (size_t i = first; i < labels.size(); ++i)
    {
        crumb(labels[i], widths[i], i);
    }

    LLTextBox::Params tp;
    tp.name = "crumb_layer";
    tp.rect = LLRect(x + 6, height - 3, x + 6 + layer_width, 0);
    tp.font = font;
    tp.initial_value = layer_text;
    mBreadcrumb->addChild(LLUICtrlFactory::create<LLTextBox>(tp));
}

// ---------------------------------------------------------------------------
// The selection
// ---------------------------------------------------------------------------
LLView* ALFloaterXUITool::selectedView() const
{
    const Preview& pv = mPreviews[PRIMARY];
    if (!pv.root || !mSelection.hasSelection())
    {
        return nullptr;
    }
    return ALXUISelection::resolve(pv.root, mSelection.selection());
}

// The element the selection names, in the most specific layer that has
// it, which is the one whose values the built view shows.
pugi::xml_node ALFloaterXUITool::authoredElement(const ALXUICatalog::Layer*& layer) const
{
    layer = nullptr;
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry || !mSelection.hasSelection())
    {
        return pugi::xml_node();
    }
    const Preview& pv = mPreviews[PRIMARY];
    std::vector<const ALXUICatalog::Layer*> layers = mCatalog.layersFor(*entry, pv.skin, pv.language);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
    {
        if (pugi::xml_node node = ALXUICatalog::resolve((*it)->root(), mSelection.selection()))
        {
            layer = *it;
            return node;
        }
    }
    return pugi::xml_node();
}

void ALFloaterXUITool::onSelectionChanged()
{
    if (mTree && !mSyncingTree)
    {
        mSyncingTree = true;
        mTree->clearSelection();
        if (mSelection.hasSelection())
        {
            auto it = mRows.find(ALXUISelection::toString(mSelection.selection()));
            if (it != mRows.end())
            {
                mTree->setSelection(it->second, false, false);
                mTree->scrollToShowSelection();
            }
        }
        mSyncingTree = false;
    }
    refreshBreadcrumb();
    refreshInspectors();
}

void ALFloaterXUITool::onHoverChanged()
{
    mModel.setCanvasHover(mSelection.hasHover() ? mModel.itemFor(mSelection.hover()) : nullptr);
}

// ---------------------------------------------------------------------------
// The inspectors
// ---------------------------------------------------------------------------
void ALFloaterXUITool::refreshInspectors()
{
    LLView* view = selectedView();
    LLPanel* current = mInspectors->getCurrentPanel();
    const std::string tab = current ? current->getName() : std::string();
    if (tab == "attributes_tab")
    {
        refreshAttributes(view);
    }
    else if (tab == "layout_tab")
    {
        refreshLayout(view);
    }
    else if (tab == "source_tab")
    {
        refreshSource(view);
    }
    else if (tab == "bindings_tab")
    {
        refreshBindings(view);
    }
    else if (tab == "state_tab")
    {
        refreshState(view);
    }
    else if (tab == "findings_tab")
    {
        refreshSelectionFindings();
    }
}

void ALFloaterXUITool::refreshAttributes(LLView* view)
{
    mAttributes->deleteAllItems();
    if (!view)
    {
        return;
    }
    const Preview& pv = mPreviews[PRIMARY];
    const ALXUISourceMap::Origin* origin = pv.sourceMap.find(view);
    if (!origin)
    {
        return;
    }

    // Which layer last wrote each attribute, and the line in that layer's
    // file, as the merge's observer recorded it; the base wrote the rest.
    for (const auto& [name_entry, attribute] : origin->node->mAttributes)
    {
        const char* name = name_entry->mString;
        const ALXUIOverlay::Origin* from = pv.overlay.originOf(attribute.get());
        const S32 line = from ? from->line : attribute->getLineNumber();
        mAttributes->addElement(row(name, {
            { "attribute", name },
            { "value", attribute->getValue() },
            { "layer", layerLabel(PRIMARY, from ? from->layer : 0) },
            { "line", line > 0 ? std::to_string(line) : std::string() } }));
    }
    if (origin->node->hasTextContents())
    {
        const ALXUIOverlay::Origin* from = pv.overlay.originOf(origin->node.get());
        const S32 line = from ? from->line : origin->line;
        mAttributes->addElement(row("text()", {
            { "attribute", "(text)" },
            { "value", origin->node->getTextContents() },
            { "layer", layerLabel(PRIMARY, from ? from->layer : 0) },
            { "line", line > 0 ? std::to_string(line) : std::string() } }));
    }
}

// A layer's skin and language, read off its path: the segments around
// the xui directory.
std::string ALFloaterXUITool::layerLabel(S32 which, S32 layer) const
{
    const std::string& path = mPreviews[which].overlay.layerPath(layer);
    if (path.empty())
    {
        return std::string();
    }
    std::vector<std::string> segments;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t end = path.find_first_of("/\\", start);
        segments.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    for (size_t i = 1; i + 1 < segments.size(); ++i)
    {
        if (segments[i] == "xui")
        {
            return segments[i - 1] + "/" + segments[i + 1];
        }
    }
    return path;
}

void ALFloaterXUITool::refreshLayout(LLView* view)
{
    mLayout->deleteAllItems();
    if (!view)
    {
        return;
    }
    auto add = [&](const std::string& property, const std::string& value)
    {
        mLayout->addElement(row(property, { { "property", property }, { "value", value } }));
    };

    const LLRect& r = view->getRect();
    const LLView* parent = view->getParent();
    add("size", std::to_string(r.getWidth()) + " x " + std::to_string(r.getHeight()));
    if (parent)
    {
        const S32 ph = parent->getRect().getHeight();
        const S32 pw = parent->getRect().getWidth();
        add("left / top (from parent's top-left)", std::to_string(r.mLeft) + " / " + std::to_string(ph - r.mTop));
        add("right / bottom (from parent's top-left)", std::to_string(r.mRight) + " / " + std::to_string(ph - r.mBottom));
        add("gap to parent's right / bottom", std::to_string(pw - r.mRight) + " / " + std::to_string(r.mBottom));
        add("parent", parent->getName() + "  " + std::to_string(pw) + " x " + std::to_string(ph));
    }
    add("rect (parent space, bottom-left origin)", rectText(r));
    add("rect (screen)", rectText(view->calcScreenRect()));
    if (view->getUseBoundingRect() && view->getBoundingRect() != r)
    {
        add("bounding rect", rectText(view->getBoundingRect()));
    }
    add("follows", followsText(view->getFollows()));

    const ALXUICatalog::Layer* layer = nullptr;
    pugi::xml_node element = authoredElement(layer);
    if (element)
    {
        std::string form;
        for (const char* attr : { "left", "left_pad", "left_delta", "right", "top", "top_pad", "top_delta", "bottom", "width", "height" })
        {
            if (pugi::xml_attribute a = element.attribute(attr))
            {
                if (!form.empty())
                {
                    form += "  ";
                }
                form += std::string(attr) + "=\"" + a.value() + "\"";
            }
        }
        add("authored", form.empty() ? std::string("(none: the widget's defaults)") : form);
        add("layout", element.attribute("layout").as_string("(default)"));
        add("follows (authored)", element.attribute("follows").as_string("(none)"));
        if (element.attribute("left_pad") || element.attribute("left_delta")
            || element.attribute("top_pad") || element.attribute("top_delta"))
        {
            // The sibling the pads and deltas are measured from is the
            // widget element before this one.
            pugi::xml_node sibling = element.previous_sibling();
            while (sibling && (sibling.type() != pugi::node_element || !ALXUICatalog::isWidgetTag(sibling.name())))
            {
                sibling = sibling.previous_sibling();
            }
            add("relative to", sibling ? std::string(sibling.attribute("name").as_string("unnamed")) + " <" + sibling.name() + ">"
                                       : std::string("(no widget before it: the parent)"));
        }
    }

    if (const LLFloater* floater = view->as<LLFloater>())
    {
        add("resizable", floater->isResizable() ? "yes" : "no");
        add("min size", std::to_string(floater->getMinWidth()) + " x " + std::to_string(floater->getMinHeight()));
    }
}

void ALFloaterXUITool::refreshSource(LLView* view)
{
    mSourceLayers->setText(std::string());
    mSourceText->setText(std::string());
    mSourcePath.clear();
    mSourceLine = 0;
    if (!view)
    {
        return;
    }
    const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
    if (!entry)
    {
        return;
    }
    const Preview& pv = mPreviews[PRIMARY];

    // Every layer of the file, with the line the element is on in each.
    std::string layers_text;
    std::string text;
    for (const ALXUICatalog::Layer* layer : mCatalog.layersFor(*entry, pv.skin, pv.language))
    {
        pugi::xml_node node = ALXUICatalog::resolve(layer->root(), mSelection.selection());
        const S32 line = ALXUICatalog::lineOf(*layer, node);
        if (!layers_text.empty())
        {
            layers_text += "   ";
        }
        layers_text += layer->skin + "/" + layer->language + ": " + (node ? std::to_string(line) : getString("LayerMissing"));
        if (!node)
        {
            continue;
        }
        const std::string file_text = LLFile::getContents(layer->path);
        text += "--- " + layer->path + ":" + std::to_string(line) + "\n";
        text += numbered(elementTextAt(file_text, line), line);
        text += "\n";
        mSourcePath = layer->path;
        mSourceLine = line;
    }
    mSourceLayers->setText(layers_text);
    mSourceText->setText(text);
}

void ALFloaterXUITool::refreshBindings(LLView* view)
{
    mBindings->deleteAllItems();
    if (!view)
    {
        return;
    }
    const ALXUICatalog::Layer* layer = nullptr;
    pugi::xml_node element = authoredElement(layer);
    if (!element)
    {
        return;
    }
    S32 n = 0;
    auto add = [&](const std::string& kind, const std::string& name, const std::string& status)
    {
        mBindings->addElement(row(n++, { { "kind", kind }, { "name", name }, { "status", status } }));
    };

    // Callbacks are child elements with a function attribute; in shell
    // mode only the global registries answer, so a floater's own
    // registrar is reported as such.
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling())
    {
        if (child.type() != pugi::node_element)
        {
            continue;
        }
        pugi::xml_attribute function = child.attribute("function");
        if (!function)
        {
            continue;
        }
        std::string kind = child.name();
        const size_t dot = kind.rfind('.');
        if (dot != std::string::npos)
        {
            kind = kind.substr(dot + 1);
        }
        const bool commit = LLUICtrl::CommitCallbackRegistry::instance().getValue(function.value()) != nullptr;
        const bool enable = LLUICtrl::EnableCallbackRegistry::instance().getValue(function.value()) != nullptr;
        std::string status = commit ? "commit registry" : enable ? "enable registry" : "not global";
        std::string name = function.value();
        if (pugi::xml_attribute parameter = child.attribute("parameter"))
        {
            name += "  (" + std::string(parameter.value()) + ")";
        }
        add(kind, name, status);
    }

    auto control = [&](const char* attr)
    {
        if (pugi::xml_attribute a = element.attribute(attr))
        {
            const bool global = gSavedSettings.controlExists(a.value());
            const bool account = gSavedPerAccountSettings.controlExists(a.value());
            add(attr, a.value(), global ? "config" : account ? "account" : "no such control");
        }
    };
    control("control_name");
    control("control");
    control("enabled_control");
    control("disabled_control");
    control("visibility_control");
    control("invisibility_control");

    auto file = [&](const char* attr)
    {
        if (pugi::xml_attribute a = element.attribute(attr))
        {
            add(attr, a.value(), mCatalog.find(a.value()) ? "in the catalog" : "no such file");
        }
    };
    file("menu_filename");
    file("filename");
    if (pugi::xml_attribute a = element.attribute("help_topic"))
    {
        add("help_topic", a.value(), "");
    }
}

void ALFloaterXUITool::refreshState(LLView* view)
{
    // Rebuilt on a timer while the tab shows; the scroll position is kept.
    const S32 scroll = mState->getScrollPos();
    mState->deleteAllItems();
    if (!view)
    {
        return;
    }
    auto add = [&](const std::string& property, const std::string& value)
    {
        mState->addElement(row(property, { { "property", property }, { "value", value } }));
    };
    auto yes = [](bool b) { return std::string(b ? "yes" : "no"); };

    add("visible", yes(view->getVisible()));
    add("in visible chain", yes(view->isInVisibleChain()));
    add("enabled", yes(view->getEnabled()));
    S32 mx, my;
    LLUI::getInstance()->getMousePositionLocal(view, &mx, &my);
    add("mouse over", yes(view->pointInView(mx, my)));
    if (LLUICtrl* ctrl = view->as<LLUICtrl>())
    {
        add("focus", yes(ctrl->hasFocus()));
        add("value", ctrl->getValue().asString());
    }
    std::string text;
    bool truncated = false;
    if (const LLTextBox* box = view->as<LLTextBox>())
    {
        text = box->getText();
        const LLFontGL* font = box->getFont();
        truncated = font && !box->getWordWrap() && !text.empty()
            && widestLine(font, text) > box->getRect().getWidth() - 2 * box->getHPad();
    }
    else if (const LLButton* button = view->as<LLButton>())
    {
        text = button->getLabelUnselected();
        const LLFontGL* font = button->getFont();
        truncated = font && !text.empty() && font->getWidth(text) > button->getRect().getWidth() - 8;
    }
    if (!text.empty())
    {
        add("text", text);
        add("truncated", yes(truncated));
    }
    add("tooltip", view->getToolTip());
    add("name", view->getName());
    add("class", view->viewType()->mName);
    mState->setScrollPos(scroll);
}

void ALFloaterXUITool::onJumpToSource()
{
    if (mSourcePath.empty())
    {
        // The Source tab has not been shown for this selection.
        refreshSource(selectedView());
    }
    if (mSourcePath.empty())
    {
        const ALXUICatalog::Entry* entry = mCatalog.find(mFile);
        if (entry && !entry->layers.empty())
        {
            const ALXUICatalog::Layer* layer = entry->layer("default", "en");
            mSourcePath = (layer ? layer : &entry->layers.front())->path;
            mSourceLine = 1;
        }
    }
    if (!mSourcePath.empty())
    {
        openInEditor(mSourcePath, mSourceLine);
    }
}

void ALFloaterXUITool::openInEditor(const std::string& path, S32 line)
{
    if (path.empty())
    {
        return;
    }
    LLExternalEditor editor;
    LLExternalEditor::EErrorCode status = editor.setCommand("LL_XUI_EDITOR");
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        setStatus(status == LLExternalEditor::EC_NOT_SPECIFIED ? getString("ExternalEditorNotSet")
                                                               : LLExternalEditor::getErrorMessage(status));
        return;
    }
    status = editor.run(path, line);
    if (status != LLExternalEditor::EC_SUCCESS)
    {
        setStatus(LLExternalEditor::getErrorMessage(status));
        return;
    }
    setStatus("Opened " + path + ":" + std::to_string(line));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void ALFloaterXUITool::setStatus(const std::string& text)
{
    mStatus->setText(text);
}

void ALFloaterXUITool::onToggleHover()
{
    mHoverHighlight = getChild<LLCheckBoxCtrl>("hover_check")->getValue().asBoolean();
    saveState();
}

void ALFloaterXUITool::onToggleCodeBuilt()
{
    mShowCodeBuilt = getChild<LLCheckBoxCtrl>("code_built_check")->getValue().asBoolean();
    mModel.getFilter().setShowCodeBuilt(mShowCodeBuilt);
    saveState();
}

void ALFloaterXUITool::onToggleSecondary()
{
    mShowSecondary = mSecondaryCheck->getValue().asBoolean();
    mLanguageCombo2->setEnabled(mShowSecondary);
    saveState();
    if (!mFile.empty())
    {
        if (mShowSecondary)
        {
            showPreview(SECONDARY);
        }
        else
        {
            closePreview(SECONDARY);
        }
    }
}

void ALFloaterXUITool::saveState()
{
    LLSD state;
    state["file"] = mFile;
    state["skin"] = mSkin;
    state["language"] = mLanguage;
    state["language2"] = mLanguage2;
    state["secondary"] = mShowSecondary;
    state["hover"] = mHoverHighlight;
    state["code_built"] = mShowCodeBuilt;
    if (LLPanel* current = mInspectors ? mInspectors->getCurrentPanel() : nullptr)
    {
        state["tab"] = current->getName();
    }
    gSavedSettings.setLLSD("ALXUIToolState", state);
}

void ALFloaterXUITool::loadState()
{
    const LLSD state = gSavedSettings.getLLSD("ALXUIToolState");
    if (!state.isMap())
    {
        return;
    }
    mFile = state["file"].asString();
    if (state.has("skin"))
    {
        mSkin = state["skin"].asString();
    }
    if (state.has("language"))
    {
        mLanguage = state["language"].asString();
    }
    if (state.has("language2"))
    {
        mLanguage2 = state["language2"].asString();
    }
    mShowSecondary = state["secondary"].asBoolean();
    if (state.has("hover"))
    {
        mHoverHighlight = state["hover"].asBoolean();
    }
    if (state.has("code_built"))
    {
        mShowCodeBuilt = state["code_built"].asBoolean();
    }
    if (state.has("tab") && mInspectors)
    {
        mInspectors->selectTabByName(state["tab"].asString());
    }
}
