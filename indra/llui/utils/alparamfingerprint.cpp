/**
 * @file alparamfingerprint.cpp
 * @brief A stable fingerprint of the parameter block behind every llui widget.
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

#include "alparamfingerprint.h"

#include "hbxxh.h"
#include "llinitparam.h"
#include "llsd.h"
#include "llsdparam.h"
#include "llsdserialize.h"
#include "lluictrlfactory.h"
#include "llxuiparser.h"

#include "llbutton.h"
#include "llstyle.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llcontainerview.h"
#include "llfiltereditor.h"
#include "llflyoutbutton.h"
#include "lliconctrl.h"
#include "lllayoutstack.h"
#include "lllineeditor.h"
#include "llloadingindicator.h"
#include "llmenubutton.h"
#include "llmenugl.h"
#include "llmultislider.h"
#include "llmultisliderctrl.h"
#include "llpanel.h"
#include "llprogressbar.h"
#include "llradiogroup.h"
#include "llscrollcontainer.h"
#include "llscrollingpanellist.h"
#include "llscrolllistctrl.h"
#include "llsearcheditor.h"
#include "llslider.h"
#include "llsliderctrl.h"
#include "llspinctrl.h"
#include "llstatbar.h"
#include "llstatview.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltimectrl.h"
#include "lluictrl.h"
#include "llviewborder.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

// The widget types llui registers, mirroring LLWidgetReg::initClass(). A type
// only appears in the fingerprint if it is listed here, so a widget added to
// the registry needs adding here too or it silently goes unwatched.
#define AL_FINGERPRINT_WIDGETS(_)                       \
    _(LLButton,                 "button")               \
    _(LLCheckBoxCtrl,           "check_box")            \
    _(LLComboBox,               "combo_box")            \
    _(LLContainerView,          "container_view")       \
    _(LLContextMenu,            "context_menu")         \
    _(LLFilterEditor,           "filter_editor")        \
    _(LLFlyoutButton,           "flyout_button")        \
    _(LLIconCtrl,               "icon")                 \
    _(LLLayoutStack,            "layout_stack")         \
    _(LLLineEditor,             "line_editor")          \
    _(LLLoadingIndicator,       "loading_indicator")    \
    _(LLMenuBarGL,              "menu_bar")             \
    _(LLMenuButton,             "menu_button")          \
    _(LLMenuGL,                 "menu")                 \
    _(LLMenuItemCallGL,         "menu_item_call")       \
    _(LLMenuItemCheckGL,        "menu_item_check")      \
    _(LLMenuItemSeparatorGL,    "menu_item_separator")  \
    _(LLMultiSlider,            "multi_slider_bar")     \
    _(LLMultiSliderCtrl,        "multi_slider")         \
    _(LLPanel,                  "panel")                \
    _(LLProgressBar,            "progress_bar")         \
    _(LLRadioGroup,             "radio_group")          \
    _(LLScrollContainer,        "scroll_container")     \
    _(LLScrollListCtrl,         "scroll_list")          \
    _(LLScrollingPanelList,     "scrolling_panel_list") \
    _(LLSearchEditor,           "search_editor")        \
    _(LLSlider,                 "slider_bar")           \
    _(LLSliderCtrl,             "slider")               \
    _(LLSpinCtrl,               "spinner")              \
    _(LLStatBar,                "stat_bar")             \
    _(LLStatView,               "stat_view")            \
    _(LLTabContainer,           "tab_container")        \
    _(LLTextBox,                "text")                 \
    _(LLTextEditor,             "simple_text_editor")   \
    _(LLTimeCtrl,               "time")                 \
    _(LLUICtrl,                 "ui_ctrl")              \
    _(LLViewBorder,             "view_border")

namespace
{
    // Written through so the loops below cannot be optimized away.
    volatile std::size_t gBenchSink = 0;

    template<typename P>
    void benchBlock(const char* tag, std::ostream& out)
    {
        const int N = 20000;

        // The first block of a type registers the type's parameters, which
        // would otherwise land in whichever timed loop ran first.
        {
            P warm;
            gBenchSink += warm.getBlockDescriptor().mNamedParams.size();
        }

        const auto default_start = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i)
        {
            P block;
            gBenchSink += reinterpret_cast<std::size_t>(&block);
        }
        const auto default_end = std::chrono::steady_clock::now();

        P source;
        const auto copy_start = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i)
        {
            P block(source);
            gBenchSink += reinterpret_cast<std::size_t>(&block);
        }
        const auto copy_end = std::chrono::steady_clock::now();

        // What LLUICtrlFactory::create does before building the widget, and
        // the only thing that walks the whole parameter table.
        const auto fill_start = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i)
        {
            P block;
            block.fillFrom(source);
            gBenchSink += reinterpret_cast<std::size_t>(&block);
        }
        const auto fill_end = std::chrono::steady_clock::now();

        out << "  " << tag << " sizeof=" << sizeof(P)
            << " default=" << std::chrono::duration<double, std::nano>(default_end - default_start).count() / N
            << "ns copy=" << std::chrono::duration<double, std::nano>(copy_end - copy_start).count() / N
            << "ns default+fillFrom=" << std::chrono::duration<double, std::nano>(fill_end - fill_start).count() / N
            << "ns" << std::endl;
    }

    // Parsing is where a name is looked up, so this is what a block's table
    // layout costs at the moment it is used, rather than what it occupies.
    template<typename T>
    void benchParse(const char* tag, const char* widget_file, std::ostream& out)
    {
        LLXMLNodePtr root;
        if (!LLUICtrlFactory::getLayeredXMLNode(gDirUtilp->add("widgets", widget_file), root)
            || root.isNull())
        {
            out << "  " << tag << " (no template to read)" << std::endl;
            return;
        }

        const typename T::Params& defaults = LLUICtrlFactory::getDefaultParams<T>();

        const int N = 2000;
        {
            typename T::Params warm(defaults);
            LLXUIParser parser;
            parser.readXUI(root, warm, widget_file, true);
            gBenchSink += warm.getBlockDescriptor().mAllParams.size();
        }

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i)
        {
            typename T::Params block(defaults);
            LLXUIParser parser;
            parser.readXUI(root, block, widget_file, true);
            gBenchSink += reinterpret_cast<std::size_t>(&block);
        }
        const auto end = std::chrono::steady_clock::now();

        out << "  " << tag << " copy+parse "
            << std::chrono::duration<double, std::nano>(end - start).count() / N
            << "ns" << std::endl;
    }

    template<typename T>
    void fingerprintWidget(const char* tag, std::ostream& out)
    {
        // Name the widget and flush before doing any work, so a crash inside
        // one of these leaves a half-written line naming the culprit rather
        // than an empty buffer.
        out << tag << std::flush;

        // Building the defaults is the whole point: it reads the widget's
        // own template from the skin and then back-fills through every base
        // class, so the result exercises the same tables a real widget does.
        //
        // Report whether a skin template was actually there to read, the same
        // way loadWidgetTemplate looks for one. A registered tag alone does
        // not mean a file exists, and a run that silently read no templates
        // would otherwise look just like one that read them all.
        const std::string* tag_registered =
            LLWidgetNameRegistry::instance().getValue(std::type_index(typeid(typename T::Params)));
        const bool has_xml = tag_registered
            && !gDirUtilp->findSkinnedFilenames(
                    LLDir::XUI, gDirUtilp->add("widgets", *tag_registered + ".xml")).empty();

        const typename T::Params& params = LLUICtrlFactory::getDefaultParams<T>();

        // The value hash covers what the block holds. It only sees params that
        // are *provided*, so on its own it cannot tell two blocks apart when
        // the extra ones carry no default -- LLButton and LLMenuButton hash
        // alike. The table hash covers the shape of the parameter table
        // itself, which is what the descriptor rework actually puts at risk.
        LLSD sd;
        LLParamSDParser parser;
        parser.writeSD(sd, params);

        // LLSD maps iterate in sorted key order, so the notation is stable
        // across runs without any sorting of our own.
        std::ostringstream notation;
        LLSDSerialize::toNotation(sd, notation);

        const LLInitParam::BlockDescriptor& descriptor = T::Params::getBlockDescriptor();
        // namedParams(), not mNamedParams: a block's own table holds only what
        // it declares, and the fingerprint is over everything it answers to.
        const auto named = descriptor.namedParams();
        std::vector<std::string> table;
        table.reserve(named.size());
        for (const auto& [name, param] : named)
        {
            table.push_back(std::string(name) + ':' + std::to_string(param->mMinCount)
                                              + ':' + std::to_string(param->mMaxCount));
        }
        // The tables are hash maps, so impose an order of our own.
        std::sort(table.begin(), table.end());

        std::ostringstream table_text;
        for (const std::string& entry : table)
        {
            table_text << entry << '\n';
        }

        out << " params=" << named.size()
            << " xml=" << (has_xml ? "yes" : "no")
            << " table=" << std::hex << std::setw(16) << std::setfill('0')
            << HBXXH64::digest(table_text.str())
            << " value=" << std::setw(16) << std::setfill('0')
            << HBXXH64::digest(notation.str())
            << std::dec << std::setfill(' ') << std::endl;
    }
}

namespace ALParamFingerprint
{
    void collect(std::ostream& out)
    {
#define AL_FINGERPRINT_ONE(TYPE, TAG) fingerprintWidget<TYPE>(TAG, out);
        AL_FINGERPRINT_WIDGETS(AL_FINGERPRINT_ONE)
#undef AL_FINGERPRINT_ONE
    }

    std::string sizes()
    {
        // As census(): a block's parameter table is empty until something has
        // constructed one, and the counts below read from that table.
        std::ostringstream sink;
        collect(sink);

        std::ostringstream out;
        std::size_t total = 0;
        out << "Parameter block instance sizes" << std::endl;
#define AL_SIZE_ONE(TYPE, TAG)                                                      {                                                                               const std::size_t bytes = sizeof(TYPE::Params);                             total += bytes;                                                             out << "  " << TAG << ' ' << bytes << " bytes / "                               << TYPE::Params::getBlockDescriptor().namedParams().size()                   << " params" << std::endl;                                          }
        AL_FINGERPRINT_WIDGETS(AL_SIZE_ONE)
#undef AL_SIZE_ONE
        out << "  total across the set: " << total << " bytes" << std::endl;

        out << "Building blocks" << std::endl
            << "  LLInitParam::BaseBlock      " << sizeof(LLInitParam::BaseBlock) << std::endl
            << "  LLInitParam::Param          " << sizeof(LLInitParam::Param) << std::endl
            << "  ParamValue<LLRect>          " << sizeof(LLInitParam::ParamValue<LLRect>) << std::endl
            << "  ParamValue<LLUIColor>       " << sizeof(LLInitParam::ParamValue<LLUIColor>) << std::endl
            << "  LLView::Params              " << sizeof(LLView::Params) << std::endl
            << "  LLUICtrl::Params            " << sizeof(LLUICtrl::Params) << std::endl
            << "  LLButton::Params            " << sizeof(LLButton::Params) << std::endl
            << "  LLScrollListCtrl::Params    " << sizeof(LLScrollListCtrl::Params) << std::endl
            << "  CommitCallbackParam         " << sizeof(LLUICtrl::CommitCallbackParam) << std::endl
            << "  EnableCallbackParam         " << sizeof(LLUICtrl::EnableCallbackParam) << std::endl
            << "  LLUIColor                   " << sizeof(LLUIColor) << std::endl
            << "  LLRect                      " << sizeof(LLRect) << std::endl;
        return out.str();
    }

    std::string bench()
    {
        std::ostringstream out;
        out << "Parameter block construction" << std::endl;
        benchBlock<LLStyle::Params>("LLStyle::Params", out);
        benchBlock<LLScrollListCell::Params>("LLScrollListCell::Params", out);
        benchBlock<LLScrollListItem::Params>("LLScrollListItem::Params", out);
        benchBlock<LLTextBox::Params>("LLTextBox::Params", out);
        benchBlock<LLButton::Params>("LLButton::Params", out);
        benchBlock<LLView::Params>("LLView::Params", out);

        out << "Parameter block parsing" << std::endl;
        benchParse<LLButton>("LLButton::Params", "button.xml", out);
        benchParse<LLTextBox>("LLTextBox::Params", "text.xml", out);
        benchParse<LLPanel>("LLPanel::Params", "panel.xml", out);
        return out.str();
    }

    std::string census()
    {
        // A block type only registers its parameters once something has
        // constructed it, so the tables are nearly empty until the defaults
        // have been built. Build them and throw the fingerprint away rather
        // than report a count four times smaller than the truth.
        std::ostringstream sink;
        collect(sink);
        return LLInitParam::BlockDescriptor::getStatsReport();
    }
}
