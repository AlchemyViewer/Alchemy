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

#include "llbutton.h"
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
        std::vector<std::string> table;
        table.reserve(descriptor.mNamedParams.size());
        for (const auto& [name, param] : descriptor.mNamedParams)
        {
            table.push_back(name + ':' + std::to_string(param->mMinCount)
                                 + ':' + std::to_string(param->mMaxCount));
        }
        // mNamedParams is a hash map, so impose an order of our own.
        std::sort(table.begin(), table.end());

        std::ostringstream table_text;
        for (const std::string& entry : table)
        {
            table_text << entry << '\n';
        }

        out << " params=" << descriptor.mNamedParams.size()
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

    std::string census()
    {
        return LLInitParam::BlockDescriptor::getStatsReport();
    }
}
