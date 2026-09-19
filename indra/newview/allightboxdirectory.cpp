/**
 * @file allightboxdirectory.cpp
 * @brief Where the Lightbox's sections and settings are, found once
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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
#include "allightboxdirectory.h"

#include "alcurveeditorctrl.h"
#include "alsettingrow.h"
#include "llaccordionctrl.h"
#include "llaccordionctrltab.h"
#include "llcheckboxctrl.h"
#include "llpanel.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "llviewercontrol.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
const std::string SECTION_PREFIX = "sec_";

/// A view's children in the order its XUI declares them. A view list is
/// filled from the front, so it is read backwards.
template <typename Fn>
void forEachChildInOrder(LLView* view, Fn&& fn)
{
    const LLView::child_list_t* children = view->getChildList();
    for (auto it = children->rbegin(); it != children->rend(); ++it)
    {
        fn(*it);
    }
}

/// A caption as it should read on its own: no padding, and no colon, which
/// is there to lead the eye to the control beside it.
std::string tidyCaption(std::string caption)
{
    LLStringUtil::trim(caption);
    while (!caption.empty() && caption.back() == ':')
    {
        caption.pop_back();
        LLStringUtil::trim(caption);
    }
    return caption;
}

/// A setting's key as words, for a row that captions itself in none of the
/// ways a person would read: the prefix every render setting shares goes,
/// and the words come apart. "RenderReferenceWipeMode" reads "Reference wipe
/// mode"; an acronym stays whole, so "RenderCASSharpness" reads "CAS
/// sharpness".
std::string readableKey(const std::string& key)
{
    std::string rest = key;
    for (const char* prefix : { "Alchemy", "Render" })
    {
        const size_t len = strlen(prefix);
        if (rest.size() > len && rest.compare(0, len, prefix) == 0 && isupper((unsigned char)rest[len]))
        {
            rest.erase(0, len);
        }
    }

    std::vector<std::string> words;
    for (size_t i = 0; i < rest.size(); ++i)
    {
        const bool upper = isupper((unsigned char)rest[i]) != 0;
        // A word starts at a capital after a lower-case letter, or at the last
        // capital of a run that a lower-case letter follows ("CASSharpness").
        const bool starts = i == 0 ||
            (upper && (islower((unsigned char)rest[i - 1]) ||
                       (i + 1 < rest.size() && isupper((unsigned char)rest[i - 1]) && islower((unsigned char)rest[i + 1]))));
        if (starts)
        {
            words.emplace_back();
        }
        words.back() += rest[i];
    }

    std::string readable;
    for (size_t w = 0; w < words.size(); ++w)
    {
        std::string word = words[w];
        const bool acronym = word.size() > 1 &&
            std::all_of(word.begin(), word.end(), [](char c) { return !islower((unsigned char)c); });
        if (w > 0 && !acronym)
        {
            LLStringUtil::toLower(word);
        }
        readable += (w > 0 ? " " : "") + word;
    }
    return readable.empty() ? key : readable;
}

/// The control's own label: a text box it holds and names as one ("slider
/// label", "checkbox label", "wheel_label"). Its value text is never named
/// so, which is the point of going by the name.
LLTextBox* ownLabelOf(LLUICtrl* ctrl)
{
    for (LLView* child : *ctrl->getChildList())
    {
        LLTextBox* box = ALViewType::as<LLTextBox>(child);
        if (!box)
        {
            continue;
        }
        std::string name = box->getName();
        LLStringUtil::toLower(name);
        if (name.size() >= 5 && name.compare(name.size() - 5, 5, "label") == 0 &&
            !tidyCaption(box->getText()).empty())
        {
            return box;
        }
    }
    return nullptr;
}

/// The text to the control's left on the same line, which is how a row
/// captions a dropdown, a colour or a vector: the one nearest the control
/// whose height takes in the control's middle.
LLTextBox* labelToLeftOf(LLUICtrl* ctrl)
{
    LLView* parent = ctrl->getParent();
    if (!parent)
    {
        return nullptr;
    }

    // A little slack on the right edge: a caption sized to the column can
    // touch the control it names.
    constexpr S32 TOUCHING = 4;
    const LLRect& rect = ctrl->getRect();
    const S32 middle = (rect.mTop + rect.mBottom) / 2;

    LLTextBox* nearest = nullptr;
    for (LLView* sibling : *parent->getChildList())
    {
        LLTextBox* box = ALViewType::as<LLTextBox>(sibling);
        if (!box || box == ctrl)
        {
            continue;
        }
        const LLRect& text = box->getRect();
        if (text.mBottom <= middle && middle <= text.mTop && text.mRight <= rect.mLeft + TOUCHING &&
            !tidyCaption(box->getText()).empty() &&
            (!nearest || text.mRight > nearest->getRect().mRight))
        {
            nearest = box;
        }
    }
    return nearest;
}
} // namespace

// static
bool ALLightboxDirectory::parseVec3Name(const std::string& name, std::string& setting, S32& component)
{
    static const std::string prefix = "vec3_";
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
    {
        return false;
    }
    size_t sep = name.rfind('_');
    if (sep <= prefix.size() || sep + 2 != name.size())
    {
        return false;
    }
    S32 comp = name[sep + 1] - '0';
    if (comp < 0 || comp > 2)
    {
        return false;
    }
    setting = name.substr(prefix.size(), sep - prefix.size());
    component = comp;
    return true;
}

// Descending into composites is safe because their internal children are
// named for their role ("Slider", "value") and cannot parse as
// vec3_<Setting>_<n>.
// static
void ALLightboxDirectory::collectVec3Controls(LLView* view, std::map<std::string, std::array<LLUICtrl*, 3>>& rows)
{
    for (LLView* child : *view->getChildList())
    {
        if (LLUICtrl* ctrl = ALViewType::as<LLUICtrl>(child))
        {
            std::string setting;
            S32 component = 0;
            if (parseVec3Name(ctrl->getName(), setting, component))
            {
                rows[setting][component] = ctrl;
            }
        }
        collectVec3Controls(child, rows);
    }
}

// static
void ALLightboxDirectory::collectBoundKeys(LLView* view, std::set<std::string>& keys)
{
    for (LLView* child : *view->getChildList())
    {
        if (LLUICtrl* ctrl = ALViewType::as<LLUICtrl>(child))
        {
            if (LLControlVariable* control = ctrl->getControlVariable())
            {
                // Only controls owned by gSavedSettings; enabled/visibility
                // bindings live in separate slots and are not touched here.
                if (gSavedSettings.getControl(control->getName()) == control)
                {
                    keys.insert(control->getName());
                }
            }
            std::string setting;
            S32 component = 0;
            if (parseVec3Name(ctrl->getName(), setting, component))
            {
                keys.insert(setting);
            }
            // A graph that edits settings not bound through control_name (an
            // LLSD point list cannot be) names them in XUI; enrol those too.
            if (auto* graph = dynamic_cast<ALCurveEditorCtrl*>(ctrl))
            {
                for (const std::string& name : graph->getEditedSettingNames())
                {
                    keys.insert(name);
                }
            }
        }
        collectBoundKeys(child, keys);
    }
}

void ALLightboxDirectory::build(const std::vector<LLPanel*>& pages)
{
    mSections.clear();
    mSettings.clear();
    mSectionIndex.clear();
    mSettingIndex.clear();

    // Sections do not nest, so a tab ends the search down its branch.
    std::function<void(LLView*, size_t)> find_sections = [&](LLView* view, size_t page)
    {
        forEachChildInOrder(view, [&](LLView* child)
        {
            if (LLAccordionCtrlTab* tab = ALViewType::as<LLAccordionCtrlTab>(child))
            {
                addSection(tab, page);
            }
            else
            {
                find_sections(child, page);
            }
        });
    };
    for (size_t page = 0; page < pages.size(); ++page)
    {
        if (pages[page])
        {
            find_sections(pages[page], page);
        }
    }
}

void ALLightboxDirectory::addSection(LLAccordionCtrlTab* tab, size_t page)
{
    // The section panel by name rather than getAccordionView(): the tab only
    // learns its container from a *visible* child, so a section that starts
    // collapsed can report none.
    LLPanel* panel = nullptr;
    forEachChildInOrder(tab, [&](LLView* child)
    {
        LLPanel* candidate = ALViewType::as<LLPanel>(child);
        if (!panel && candidate && candidate->getName().compare(0, SECTION_PREFIX.size(), SECTION_PREFIX) == 0)
        {
            panel = candidate;
        }
    });
    if (!panel || mSectionIndex.count(panel->getName()))
    {
        return;
    }

    Section section;
    section.mName = panel->getName();
    section.mTitle = tab->getTitle();
    section.mPage = page;
    section.mTab = tab;
    section.mPanel = panel;
    for (LLView* up = tab->getParent(); up && !section.mAccordion; up = up->getParent())
    {
        section.mAccordion = ALViewType::as<LLAccordionCtrl>(up);
    }

    const size_t index = mSections.size();
    mSections.push_back(section);
    mSectionIndex.emplace(section.mName, index);

    addSettings(panel, index);
    // A switch on the header is part of the section however it is drawn --
    // Color Grading's master is one -- and it has no caption but the title.
    if (LLCheckBoxCtrl* header = tab->getHeaderCheckBox())
    {
        addSettings(header, index);
    }
    // Last, so a row that shows a graph's setting as a number is the one that
    // names it and the one a jump goes to: the split-tone graph's three are
    // all rows as well. The tone curve's four have no row but the graph.
    addGraphSettings(panel, index);
}

void ALLightboxDirectory::addGraphSettings(LLView* view, size_t section)
{
    forEachChildInOrder(view, [&](LLView* child)
    {
        if (ALCurveEditorCtrl* graph = ALViewType::as<ALCurveEditorCtrl>(child))
        {
            for (const std::string& key : graph->getEditedSettingNames())
            {
                if (gSavedSettings.getControl(key).notNull())
                {
                    // A graph has no caption for any one of the settings it
                    // draws, and the text beside it is not about them.
                    addSetting(key, graph, section, false, readableKey(key));
                }
            }
        }
        addGraphSettings(child, section);
    });
}

void ALLightboxDirectory::addSettings(LLView* view, size_t section)
{
    // Parent first, so a composite that binds itself as well as its parts --
    // a slider and the bar inside it -- is found as the whole.
    forEachChildInOrder(view, [&](LLView* child)
    {
        if (LLUICtrl* ctrl = ALViewType::as<LLUICtrl>(child))
        {
            if (LLControlVariable* control = ctrl->getControlVariable())
            {
                if (gSavedSettings.getControl(control->getName()) == control)
                {
                    // A checkbox hands control_name down to its button; the
                    // checkbox is the thing with the caption and the focus.
                    LLUICtrl* whole = ctrl;
                    if (LLCheckBoxCtrl* check = ALViewType::as<LLCheckBoxCtrl>(ctrl->getParent()))
                    {
                        whole = check;
                    }
                    addSetting(control->getName(), whole, section, false);
                }
            }
            std::string setting;
            S32 component = 0;
            if (parseVec3Name(ctrl->getName(), setting, component))
            {
                addSetting(setting, ctrl, section, true);
            }
        }
        addSettings(child, section);
    });
}

void ALLightboxDirectory::addSetting(const std::string& key, LLUICtrl* ctrl, size_t section, bool vector_row,
                                     const std::string& caption)
{
    if (mSettingIndex.count(key))
    {
        return;
    }

    Setting setting;
    setting.mKey = key;
    setting.mSection = section;
    setting.mCtrl = ctrl;
    if (!caption.empty())
    {
        // Named by whoever added it: there is no label to go looking for.
        setting.mCaption = caption;
        mSettingIndex.emplace(key, mSettings.size());
        mSettings.push_back(setting);
        return;
    }

    // A vector row's spinners carry a channel letter each, which is no name
    // for the setting; the row's caption is the text to their left.
    LLTextBox* box = nullptr;
    if (ALSettingRow* row = ALViewType::as<ALSettingRow>(ctrl))
    {
        // A setting row says its own name; its label is inside the slider it
        // holds, a level below where a control's own label is looked for.
        // (This is also the viewer's reference to ALSettingRow, which is what
        // links the widget -- and its registrar -- in from llui: a static
        // library leaves out an object file nothing refers to, and the tag
        // would then build nothing. Keep a reference if this goes.)
        box = row->getLabelBox();
    }
    else if (!vector_row)
    {
        box = ownLabelOf(ctrl);
    }
    if (!box)
    {
        box = labelToLeftOf(ctrl);
    }
    if (box)
    {
        setting.mCaption = tidyCaption(box->getText());
        setting.mCaptionBox = box;
    }
    else if (ctrl == mSections[section].mTab->getHeaderCheckBox())
    {
        setting.mCaption = mSections[section].mTitle;
    }
    else
    {
        setting.mCaption = readableKey(key);
    }

    mSettingIndex.emplace(key, mSettings.size());
    mSettings.push_back(setting);
}

const ALLightboxDirectory::Section* ALLightboxDirectory::section(std::string_view name) const
{
    const auto found = mSectionIndex.find(name);
    return found != mSectionIndex.end() ? &mSections[found->second] : nullptr;
}

const ALLightboxDirectory::Setting* ALLightboxDirectory::setting(std::string_view key) const
{
    const auto found = mSettingIndex.find(key);
    return found != mSettingIndex.end() ? &mSettings[found->second] : nullptr;
}

std::string ALLightboxDirectory::captionFor(const std::string& key) const
{
    const Setting* found = setting(key);
    return found ? found->mCaption : readableKey(key);
}

const ALLightboxDirectory::Section* ALLightboxDirectory::sectionOf(std::string_view key) const
{
    const Setting* found = setting(key);
    return found ? &mSections[found->mSection] : nullptr;
}
